/* 
 * This file is part of the ezDV project (https://github.com/tmiw/ezDV).
 * Copyright (c) 2023 Mooneer Salem
 * 
 * This program is free software: you can redistribute it and/or modify  
 * it under the terms of the GNU General Public License as published by  
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU 
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <string>
#include <sstream>
#include <unistd.h>
#include <sys/socket.h>

#include "FlexTcpTask.h"

#include "esp_log.h"

#define CURRENT_LOG_TAG "FlexTcpTask"
#define MAX_PACKET_SIZE 1024

namespace ezdv
{

namespace network
{
    
namespace flex
{

FlexTcpTask::FlexTcpTask()
    : DVTask("FlexTcpTask", 10 /* TBD */, 8192, tskNO_AFFINITY, 1024, pdMS_TO_TICKS(10))
    , reconnectTimer_(this, std::bind(&FlexTcpTask::connect_, this), 10000000) /* reconnect every 10 seconds */
    , socket_(-1)
    , sequenceNumber_(0)
{
    registerMessageHandler(this, &FlexTcpTask::onFlexConnectRadioMessage_);
}

FlexTcpTask::~FlexTcpTask()
{
    disconnect_();
}

void FlexTcpTask::onTaskStart_()
{
    // nothing required, just waiting for a connect request.
}

void FlexTcpTask::onTaskWake_()
{
    // nothing required, just waiting for a connect request.
}

void FlexTcpTask::onTaskSleep_()
{
    ESP_LOGI(CURRENT_LOG_TAG, "Sleeping task");
    disconnect_();
}

void FlexTcpTask::onTaskTick_()
{
    if (socket_ <= 0)
    {
        // Skip tick if we don't have a valid connection yet.
        return;
    }
    
    fd_set readSet;
    struct timeval tv = {0, 0};
    
    FD_ZERO(&readSet);
    FD_SET(socket_, &readSet);
    
    // Process if there is pending data on the socket.
    while (select(socket_ + 1, &readSet, nullptr, nullptr, &tv) > 0)
    {    
        char buffer[MAX_PACKET_SIZE];
        
        auto rv = recv(socket_, buffer, MAX_PACKET_SIZE, 0);
        if (rv > 0)
        {
            // Append to input buffer. Then if we have a full line, we can process
            // accordingly.
            inputBuffer_.write(buffer, rv);
        }
        else
        {
            ESP_LOGE(CURRENT_LOG_TAG, "Detected disconnect from socket, reattempting connect");
            disconnect_();
            reconnectTimer_.start();
            
            return;
        }
    }
    
    std::string line;
    if (inputBuffer_.str().find("\n") != std::string::npos)
    {
        std::getline(inputBuffer_, line);

        if (line.length() > 0)
        {
            processCommand_(line);
        }
    }
}

void FlexTcpTask::connect_()
{
    // Stop any existing reconnection timers.
    reconnectTimer_.stop();
    
    // Clean up any existing connections before starting.
    disconnect_();

    struct sockaddr_in radioAddress;
    radioAddress.sin_addr.s_addr = inet_addr(ip_.c_str());
    radioAddress.sin_family = AF_INET;
    radioAddress.sin_port = htons(4992); // hardcoded as per Flex documentation
    
    socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == -1)
    {
        auto err = errno;
        ESP_LOGE(CURRENT_LOG_TAG, "Got socket error %d (%s) while creating socket", err, strerror(err));
    }
    assert(socket_ != -1);

    // Connect to the radio.
    ESP_LOGI(CURRENT_LOG_TAG, "Connecting to radio at IP %s", ip_.c_str());
    int rv = connect(socket_, (struct sockaddr*)&radioAddress, sizeof(radioAddress));
    if (rv == -1)
    {
        auto err = errno;
        ESP_LOGE(CURRENT_LOG_TAG, "Got socket error %d (%s) while connecting", err, strerror(err));
        
        // Try again in a few seconds
        close(socket_);
        socket_ = -1;
        reconnectTimer_.start();
    }
    else
    {
        ESP_LOGI(CURRENT_LOG_TAG, "Connected to radio successfully");
        sequenceNumber_ = 0;
    }
}

void FlexTcpTask::disconnect_()
{
    if (socket_ > 0)
    {
        cleanupWaveform_();
        close(socket_);
        socket_ = -1;
    
        responseHandlers_.clear();
        inputBuffer_.clear();
    }
}

void FlexTcpTask::initializeWaveform_()
{
    // Send needed commands to initialize the waveform. This is from the reference
    // waveform implementation.
    createWaveform_("FreeDV-USB", "FDVU", "DIGU");
    createWaveform_("FreeDV-LSB", "FDVL", "DIGL");
    
    // subscribe to slice updates, needed to detect when we enter FDVU/FDVL mode
    sendRadioCommand_("sub slice all");
}

void FlexTcpTask::cleanupWaveform_()
{
    sendRadioCommand_("waveform remove FreeDV-USB");
    sendRadioCommand_("waveform remove FreeDV-LSB");
}

void FlexTcpTask::createWaveform_(std::string name, std::string shortName, std::string underlyingMode)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Creating waveform %s (abbreviated %s in SmartSDR)", name.c_str(), shortName.c_str());
    
    // Actually create the waveform.
    std::string waveformCommand = "waveform create name=" + name + " mode=" + shortName + " underlying_mode=" + underlyingMode + " version=2.0.0";
    std::string setPrefix = "waveform set " + name + " ";
    sendRadioCommand_(waveformCommand, [&, setPrefix](unsigned int rv, std::string message) {
        if (rv == 0)
        {
            // Set the filter-related settings for the just-created waveform.
            sendRadioCommand_(setPrefix + "tx=1");
            sendRadioCommand_(setPrefix + "rx_filter depth=256");
            sendRadioCommand_(setPrefix + "tx_filter depth=256");
            
            // TBD: send handles to VITA task.
        }
    });
}

void FlexTcpTask::sendRadioCommand_(std::string command)
{
    sendRadioCommand_(command, std::function<void(int rv, std::string message)>());
}

void FlexTcpTask::sendRadioCommand_(std::string command, std::function<void(unsigned int rv, std::string message)> fn)
{
    std::ostringstream ss;
    
    responseHandlers_[sequenceNumber_] = fn;

    ESP_LOGI(CURRENT_LOG_TAG, "Sending '%s' as command %d", command.c_str(), sequenceNumber_);    
    ss << "C" << (sequenceNumber_++) << "|" << command << "\n";
    
    write(socket_, ss.str().c_str(), ss.str().length());
}

void FlexTcpTask::processCommand_(std::string& command)
{
    if (command[0] == 'V')
    {
        // Version information from radio
        ESP_LOGI(CURRENT_LOG_TAG, "Radio is using protocol version %s", &command.c_str()[1]);
    }
    else if (command[0] == 'H')
    {
        // Received connection's handle. We don't currently do anything with this other
        // than trigger waveform creation.
        ESP_LOGI(CURRENT_LOG_TAG, "Connection handle is %s", &command.c_str()[1]);
        initializeWaveform_();
    }
    else if (command[0] == 'R')
    {
        ESP_LOGI(CURRENT_LOG_TAG, "Received response %s", command.c_str());
        
        // Received response for a command.
        command.erase(0, 1);
        std::stringstream ss(command);
        int seq = 0;
        unsigned int rv = 0;
        char temp = 0;
        
        // Get sequence number and return value
        ss >> seq >> temp >> std::hex >> rv;
        
        if (rv != 0)
        {
            ESP_LOGE(CURRENT_LOG_TAG, "Command %d returned error %x", seq, rv);
        }
        
        // If we have a valid command handler, call it now
        if (responseHandlers_[seq])
        {
            responseHandlers_[seq](rv, ss.str());
        }
    }
    else
    {
        ESP_LOGW(CURRENT_LOG_TAG, "Got unhandled command %s", command.c_str());
    }
}

void FlexTcpTask::onFlexConnectRadioMessage_(DVTask* origin, FlexConnectRadioMessage* message)
{
    ip_ = message->ip;
    connect_();
}
    
}

}

}