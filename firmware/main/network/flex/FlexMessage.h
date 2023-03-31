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

#ifndef FLEX_MESSAGE_H
#define FLEX_MESSAGE_H

#include <cstring>
#include "task/DVTaskMessage.h"

extern "C"
{
    DV_EVENT_DECLARE_BASE(FLEX_MESSAGE);
}

namespace ezdv
{

namespace network
{

namespace flex
{

using namespace ezdv::task;

enum FlexMessageTypes
{
    CONNECT_RADIO = 1,
};

class FlexConnectRadioMessage : public DVTaskMessageBase<CONNECT_RADIO, FlexConnectRadioMessage>
{
public:
    static const int STR_SIZE = 32;
    
    FlexConnectRadioMessage(
        char* ipProvided = nullptr)
        : DVTaskMessageBase<CONNECT_RADIO, FlexConnectRadioMessage>(FLEX_MESSAGE)
    {
        memset(ip, 0, STR_SIZE);
        
        if (ipProvided != nullptr)
        {
            strncpy(ip, ipProvided, STR_SIZE - 1);
        }
        
        ip[STR_SIZE - 1] = 0;
    }
    virtual ~FlexConnectRadioMessage() = default;

    char ip[STR_SIZE];
};

}

}

}

#endif // FLEX_MESSAGE_H