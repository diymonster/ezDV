// Simple ULP RISC-V application to monitor the GPIO corresponding to the
// Mode button. If it's pressed for >= 1 second, we trigger a wakeup of 
// the main processor.

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "ulp_riscv/ulp_riscv.h"
#include "ulp_riscv/ulp_riscv_utils.h"
#include "ulp_riscv/ulp_riscv_gpio.h"

volatile int num_cycles_with_gpio_on;

#define TURN_ON_GPIO_NUM GPIO_NUM_5
#define MIN_NUM_CYCLES (300000) /* found by experimentation to take 1s */

int main (void)
{
    while(1)
    {
        if (!ulp_riscv_gpio_get_level(TURN_ON_GPIO_NUM))
        {
            num_cycles_with_gpio_on++;
            if (num_cycles_with_gpio_on >= MIN_NUM_CYCLES)
            {
                ulp_riscv_wakeup_main_processor();
                break;
            }
        }
        else
        {
            num_cycles_with_gpio_on = 0;
        }
    }
    
    /* ulp_riscv_halt() is called automatically when main exits */
    return 0;
}