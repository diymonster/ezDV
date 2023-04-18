#include "Application.h"

#include "driver/rtc_io.h"
#include "driver/gpio.h"
#include "ulp_riscv.h"
#include "ulp_main.h"
#include "esp_sleep.h"
#include "esp_log.h"

#if defined(ENABLE_AUTOMATED_TX_RX_TEST)
#include "driver/ButtonMessage.h"
#include "audio/FreeDVMessage.h"
#endif // ENABLE_AUTOMATED_TX_RX_TEST

#define CURRENT_LOG_TAG ("app")

#define BOOTUP_VOL_DOWN_GPIO (GPIO_NUM_7)
#define BOOTUP_PTT_GPIO (GPIO_NUM_4)
#define TLV320_RESET_GPIO (GPIO_NUM_13)

extern "C"
{
    // Power off handler application
    extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");
    extern const uint8_t ulp_main_bin_end[]   asm("_binary_ulp_main_bin_end");
}

namespace ezdv
{
App::App()
    : ezdv::task::DVTask("MainApp", 1, 4096, tskNO_AFFINITY, 10)
    , max17048_(&i2cDevice_)
    , tlv320Device_(&i2cDevice_)
    , wirelessTask_(&freedvTask_, &tlv320Device_, &audioMixer_, &voiceKeyerTask_)
    , rfComplianceTask_(&ledArray_, &tlv320Device_)
    , voiceKeyerTask_(&tlv320Device_, &freedvTask_)
    , rfComplianceEnabled_(false)
{
    // Check to see if Vol Down is being held on startup. 
    // If so, force use of default Wi-Fi setup. Note that 
    // we have to duplicate the initial pin setup here 
    // since waiting until the UI is fully up may be too
    // late for Wi-Fi.
    ESP_ERROR_CHECK(gpio_reset_pin(BOOTUP_VOL_DOWN_GPIO));
    ESP_ERROR_CHECK(gpio_set_direction(BOOTUP_VOL_DOWN_GPIO, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_pull_mode(BOOTUP_VOL_DOWN_GPIO, GPIO_PULLUP_ONLY));
    ESP_ERROR_CHECK(gpio_pullup_en(BOOTUP_VOL_DOWN_GPIO));

    if (gpio_get_level(BOOTUP_VOL_DOWN_GPIO) == 0)
    {
        wirelessTask_.setWiFiOverride(true);
    }
    
    // Check to see if PTT is beind held on startup.
    // This triggers the RF compliance test system.
    ESP_ERROR_CHECK(gpio_reset_pin(BOOTUP_PTT_GPIO));
    ESP_ERROR_CHECK(gpio_set_direction(BOOTUP_PTT_GPIO, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_pull_mode(BOOTUP_PTT_GPIO, GPIO_PULLUP_ONLY));
    ESP_ERROR_CHECK(gpio_pullup_en(BOOTUP_PTT_GPIO));
    
    if (gpio_get_level(BOOTUP_PTT_GPIO) == 0)
    {
        rfComplianceEnabled_ = true;
        
        // RF compliance task should be piped to TLV320.
        rfComplianceTask_.setAudioOutput(
            audio::AudioInput::ChannelLabel::LEFT_CHANNEL,
            tlv320Device_.getAudioInput(audio::AudioInput::ChannelLabel::USER_CHANNEL)
        );
            
        rfComplianceTask_.setAudioOutput(
            audio::AudioInput::ChannelLabel::RIGHT_CHANNEL,
            tlv320Device_.getAudioInput(audio::AudioInput::ChannelLabel::RADIO_CHANNEL)
        );
    }
    else
    {
        // Link TLV320 output FIFOs to FreeDVTask
        tlv320Device_.setAudioOutput(
            audio::AudioInput::ChannelLabel::LEFT_CHANNEL, 
            freedvTask_.getAudioInput(audio::AudioInput::ChannelLabel::LEFT_CHANNEL)
        );

        tlv320Device_.setAudioOutput(
            audio::AudioInput::ChannelLabel::RIGHT_CHANNEL, 
            freedvTask_.getAudioInput(audio::AudioInput::ChannelLabel::RIGHT_CHANNEL)
        );

        // Link FreeDVTask output FIFOs to:
        //    * RX: AudioMixer left channel
        //    * TX: TLV320 right channel
        freedvTask_.setAudioOutput(
            audio::AudioInput::ChannelLabel::USER_CHANNEL, 
            audioMixer_.getAudioInput(audio::AudioInput::ChannelLabel::LEFT_CHANNEL)
        );

        freedvTask_.setAudioOutput(
            audio::AudioInput::ChannelLabel::RADIO_CHANNEL, 
            tlv320Device_.getAudioInput(audio::AudioInput::ChannelLabel::RADIO_CHANNEL)
        );

        // Link beeper output to AudioMixer right channel
        beeperTask_.setAudioOutput(
            audio::AudioInput::ChannelLabel::LEFT_CHANNEL,
            audioMixer_.getAudioInput(audio::AudioInput::ChannelLabel::RIGHT_CHANNEL)
        );

        // Link audio mixer to TLV320 left channel
        audioMixer_.setAudioOutput(
            audio::AudioInput::ChannelLabel::LEFT_CHANNEL,
            tlv320Device_.getAudioInput(audio::AudioInput::ChannelLabel::USER_CHANNEL)
        );
    }
}

void App::enablePeripheralPower_()
{
    // TLV320 related GPIOs need to be isolated prior to enabling
    // peripheral power. If we don't do this, the following happens
    // the first time after waking up from deep sleep:
    //
    // 1. Network (and potentially other LEDs) stop working, and
    // 2. Audio glitches occur on startup.
    std::vector<gpio_num_t> tlv320Gpios { 
        GPIO_NUM_3, GPIO_NUM_9, GPIO_NUM_10, GPIO_NUM_11,
        GPIO_NUM_12, GPIO_NUM_14, TLV320_RESET_GPIO };
    for (auto& gpio : tlv320Gpios)
    {
        rtc_gpio_init(gpio);
        rtc_gpio_set_direction(gpio, RTC_GPIO_MODE_INPUT_ONLY);
        rtc_gpio_set_direction_in_sleep(gpio, RTC_GPIO_MODE_INPUT_ONLY);
        rtc_gpio_pulldown_en(gpio);
        rtc_gpio_pullup_dis(gpio);
        rtc_gpio_hold_en(gpio);
    }
    
    // Sleep for above changes to take effect.
    vTaskDelay(pdMS_TO_TICKS(10));

    // Enable peripheral power (required for v0.4+). This will automatically
    // power down once we switch to the ULP processor on shutdown, reducing
    // "off" current considerably.
    rtc_gpio_init(GPIO_NUM_17);
    rtc_gpio_hold_dis(GPIO_NUM_17);
    rtc_gpio_set_direction(GPIO_NUM_17, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_level(GPIO_NUM_17, true);
    rtc_gpio_hold_en(GPIO_NUM_17);
    
    // Sleep until peripheral power activates.
    vTaskDelay(pdMS_TO_TICKS(10));

    // Now we can re-attach TLV320 related GPIOs and get
    // ready to configure it.
    for (auto& gpio : tlv320Gpios)
    {
        rtc_gpio_hold_dis(gpio);
        rtc_gpio_deinit(gpio);
        gpio_reset_pin(gpio);
    }
    
    // Sleep for GPIO reattach to take effect.
    vTaskDelay(pdMS_TO_TICKS(10));
}

void App::enterDeepSleep_()
{
    /* Initialize mode button GPIO as RTC IO, enable input, enable pullup */
    rtc_gpio_init(GPIO_NUM_5);
    rtc_gpio_set_direction(GPIO_NUM_5, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_dis(GPIO_NUM_5);
    rtc_gpio_pullup_en(GPIO_NUM_5);
    rtc_gpio_hold_en(GPIO_NUM_5);
    
    // Isolate TLV320 related GPIOs to prevent issues when coming back from sleep
    // (see app_start() for explanation).
    std::vector<gpio_num_t> tlv320Gpios { 
        GPIO_NUM_3, GPIO_NUM_9, GPIO_NUM_10, GPIO_NUM_11,
        GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_14, 
        TLV320_RESET_GPIO };
    for (auto& gpio : tlv320Gpios)
    {
        rtc_gpio_init(gpio);
        rtc_gpio_set_direction(gpio, RTC_GPIO_MODE_INPUT_ONLY);
        rtc_gpio_set_direction_in_sleep(gpio, RTC_GPIO_MODE_INPUT_ONLY);
        rtc_gpio_pulldown_en(gpio);
        rtc_gpio_pullup_dis(gpio);
        rtc_gpio_hold_en(gpio);
    }
    
    // Sleep for GPIO changes to take effect.
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Shut off peripheral power. */
    rtc_gpio_init(GPIO_NUM_17);
    rtc_gpio_hold_dis(GPIO_NUM_17);
    rtc_gpio_set_direction(GPIO_NUM_17, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_direction_in_sleep(GPIO_NUM_17, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_level(GPIO_NUM_17, false);
    rtc_gpio_hold_en(GPIO_NUM_17);
    
    // Sleep for power-down to take effect.
    vTaskDelay(pdMS_TO_TICKS(10));

    esp_err_t err = ulp_riscv_load_binary(ulp_main_bin_start, (ulp_main_bin_end - ulp_main_bin_start));
    ESP_ERROR_CHECK(err);

    /* Start the ULV program */
    ESP_ERROR_CHECK(ulp_set_wakeup_period(0, 100 * 1000)); // 100 ms * (1000 us/ms)
    err = ulp_riscv_run();
    ESP_ERROR_CHECK(err);
    
    // Halt application
    ESP_LOGI(CURRENT_LOG_TAG, "Halting system");
    
    /* Small delay to ensure the messages are printed */
    vTaskDelay(100);
    fflush(stdout);
    vTaskDelay(100);

    ESP_ERROR_CHECK(esp_sleep_enable_ulp_wakeup());
    esp_deep_sleep_start();    
}

void App::onTaskStart_()
{
    ESP_LOGI(CURRENT_LOG_TAG, "onTaskStart_");

    enablePeripheralPower_();

    // The battery driver should also be initialized early in case we
    // need to immediately sleep due to low power.
    start(&max17048_, pdMS_TO_TICKS(1000));

    if (max17048_.isLowSOC())
    {
        enterDeepSleep_();
    }

    // Initialize LED array early as we want all the LEDs lit during the boot process.
    start(&ledArray_, pdMS_TO_TICKS(1000));

    {
        ezdv::driver::SetLedStateMessage msg(ezdv::driver::SetLedStateMessage::LedLabel::SYNC, true);
        ledArray_.post(&msg);
        msg.led = ezdv::driver::SetLedStateMessage::LedLabel::OVERLOAD;
        ledArray_.post(&msg);
        msg.led = ezdv::driver::SetLedStateMessage::LedLabel::PTT;
        ledArray_.post(&msg);
        msg.led = ezdv::driver::SetLedStateMessage::LedLabel::NETWORK;
        ledArray_.post(&msg);
    }

    // Start device drivers
    start(&tlv320Device_, pdMS_TO_TICKS(10000));
    start(&buttonArray_, pdMS_TO_TICKS(1000));
    
    if (!rfComplianceEnabled_)
    {
        // Start audio processing
        start(&freedvTask_, pdMS_TO_TICKS(1000));
        start(&audioMixer_, pdMS_TO_TICKS(1000));
        start(&beeperTask_, pdMS_TO_TICKS(1000));

        // Start UI
        start(&voiceKeyerTask_, pdMS_TO_TICKS(1000));
        start(&uiTask_, pdMS_TO_TICKS(1000));
    
        // Start Wi-Fi
        wirelessTask_.start();

        // Start storage handling
        settingsTask_.start();
        softwareUpdateTask_.start();
        
        // Mark boot as successful, no need to rollback.
        esp_ota_mark_app_valid_cancel_rollback();
    }
    else
    {
        start(&rfComplianceTask_, pdMS_TO_TICKS(1000));
    }
}

void App::onTaskWake_()
{
    ESP_LOGI(CURRENT_LOG_TAG, "onTaskWake_");
    
    enablePeripheralPower_();

    // The battery driver should be initialized early in case we
    // need to immediately sleep due to low power.
    wake(&max17048_, pdMS_TO_TICKS(1000));

    if (max17048_.isLowSOC())
    {
        enterDeepSleep_();
    }
    
    // Initialize LED array early as we want all the LEDs lit during the boot process.
    wake(&ledArray_, pdMS_TO_TICKS(1000));

    {
        ezdv::driver::SetLedStateMessage msg(ezdv::driver::SetLedStateMessage::LedLabel::SYNC, true);
        ledArray_.post(&msg);
        msg.led = ezdv::driver::SetLedStateMessage::LedLabel::OVERLOAD;
        ledArray_.post(&msg);
        msg.led = ezdv::driver::SetLedStateMessage::LedLabel::PTT;
        ledArray_.post(&msg);
        msg.led = ezdv::driver::SetLedStateMessage::LedLabel::NETWORK;
        ledArray_.post(&msg);
    }

    // Wake up device drivers
    wake(&tlv320Device_, pdMS_TO_TICKS(10000));
    wake(&buttonArray_, pdMS_TO_TICKS(1000));

    if (!rfComplianceEnabled_)
    {
        // Wake audio processing
        wake(&freedvTask_, pdMS_TO_TICKS(1000));
        wake(&audioMixer_, pdMS_TO_TICKS(1000));
        wake(&beeperTask_, pdMS_TO_TICKS(1000));

        // Wake UI
        wake(&voiceKeyerTask_, pdMS_TO_TICKS(1000));
        wake(&uiTask_, pdMS_TO_TICKS(1000));
    
        // Wake Wi-Fi
        wake(&wirelessTask_, pdMS_TO_TICKS(1000));

        // Wake storage handling
        wake(&settingsTask_, pdMS_TO_TICKS(1000));

        // Wake SW update handling
        wake(&softwareUpdateTask_, pdMS_TO_TICKS(1000));
        
        // Mark boot as successful, no need to rollback.
        esp_ota_mark_app_valid_cancel_rollback();
    }
    else
    {
        wake(&rfComplianceTask_, pdMS_TO_TICKS(1000));
    }
}

void App::onTaskSleep_()
{
    ESP_LOGI(CURRENT_LOG_TAG, "onTaskSleep_");

    // Disable buttons
    sleep(&buttonArray_, pdMS_TO_TICKS(1000));

    if (!rfComplianceEnabled_)
    {
        // Sleep Wi-Fi
        sleep(&wirelessTask_, pdMS_TO_TICKS(5000));
    
        // Sleep UI
        sleep(&uiTask_, pdMS_TO_TICKS(1000));
        sleep(&voiceKeyerTask_, pdMS_TO_TICKS(1000));
    
        // Sleep storage handling
        sleep(&settingsTask_, pdMS_TO_TICKS(1000));
        
        // Sleep SW update
        sleep(&softwareUpdateTask_, pdMS_TO_TICKS(1000));

        // Delay a second or two to allow final beeper to play.
        sleep(&beeperTask_, pdMS_TO_TICKS(7000));

        // Sleep audio processing
        sleep(&freedvTask_, pdMS_TO_TICKS(1000));
        sleep(&audioMixer_, pdMS_TO_TICKS(3000));
    }
    else
    {
        sleep(&rfComplianceTask_, pdMS_TO_TICKS(1000));
    }

    // Sleep device drivers
    sleep(&tlv320Device_, pdMS_TO_TICKS(2000));
    sleep(&ledArray_, pdMS_TO_TICKS(1000));
    sleep(&max17048_, pdMS_TO_TICKS(1000));
    
    enterDeepSleep_();
}

}

ezdv::App* app;

// Global method to trigger sleep
void StartSleeping()
{
    app->sleep();
}
extern "C" void app_main()
{
    // Make sure the ULP program isn't running.
    ulp_riscv_timer_stop();
    ulp_riscv_halt();

    ulp_num_cycles_with_gpio_on = 0;

    // Note: mandatory before using DVTask.
    DVTask::Initialize();

    // Note: GPIO ISRs use per GPIO ISRs.
    ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_LOWMED));
    
    app = new ezdv::App();
    assert(app != nullptr);

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    /* not a wakeup from ULP, load the firmware */
    ESP_LOGI(CURRENT_LOG_TAG, "Wakeup reason: %d", cause);
    /*if (cause != ESP_SLEEP_WAKEUP_ULP) {
        // Perform initial startup actions because we may not be fully ready yet
        app->start();

        vTaskDelay(pdMS_TO_TICKS(2000));

        ESP_LOGI(CURRENT_LOG_TAG, "Starting power off application");
        app->sleep();
    }
    else*/
    {
        ESP_LOGI(CURRENT_LOG_TAG, "Woken up via ULP, booting...");
        app->wake();
    }
    
#if 0
    // infinite loop to track heap use
#if defined(ENABLE_AUTOMATED_TX_RX_TEST)
    bool ptt = false;
    bool hasChangedModes = false;
#endif // ENABLE_AUTOMATED_TX_RX_TEST

    for(;;)
    {
        vTaskDelay(pdMS_TO_TICKS(5000));
        
        ESP_LOGI(CURRENT_LOG_TAG, "heap free (8 bit): %d", heap_caps_get_free_size(MALLOC_CAP_8BIT));
        ESP_LOGI(CURRENT_LOG_TAG, "heap free (32 bit): %d", heap_caps_get_free_size(MALLOC_CAP_32BIT));
        ESP_LOGI(CURRENT_LOG_TAG, "heap free (32 - 8 bit): %d", heap_caps_get_free_size(MALLOC_CAP_32BIT) - heap_caps_get_free_size(MALLOC_CAP_8BIT));
        ESP_LOGI(CURRENT_LOG_TAG, "heap free (internal): %d", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        ESP_LOGI(CURRENT_LOG_TAG, "heap free (SPIRAM): %d", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        ESP_LOGI(CURRENT_LOG_TAG, "heap free (DMA): %d", heap_caps_get_free_size(MALLOC_CAP_DMA));

#if defined(ENABLE_AUTOMATED_TX_RX_TEST)
        ptt = !ptt;

        // Trigger PTT
        if (!hasChangedModes)
        {
            ezdv::audio::SetFreeDVModeMessage modeSetMessage(ezdv::audio::SetFreeDVModeMessage::FREEDV_700D);
            app->getFreeDVTask().post(&modeSetMessage);
            hasChangedModes = true;
        }

        if (ptt)
        {
            ezdv::driver::ButtonShortPressedMessage pressedMessage(ezdv::driver::PTT);
            app->getUITask().post(&pressedMessage);
        }
        else
        {
            ezdv::driver::ButtonReleasedMessage releasedMessage(ezdv::driver::PTT);
            app->getUITask().post(&releasedMessage);
        }
#endif // ENABLE_AUTOMATED_TX_RX_TEST
    }
#endif
}
