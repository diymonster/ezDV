/* 
 * This file is part of the ezDV project (https://github.com/tmiw/ezDV).
 * Copyright (c) 2022 Mooneer Salem
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

#ifndef AUDIO_STATE_H
#define AUDIO_STATE_H

#include "task/DVTimer.h"
#include "TrackedPacketState.h"

namespace ezdv
{

namespace network
{

namespace icom
{

using namespace ezdv::task;

class AudioState : public TrackedPacketState
{
public:
    AudioState(IcomStateMachine* parent);
    virtual ~AudioState() = default;

    virtual void onEnterState() override;
    virtual void onExitState() override;

    virtual std::string getName() override;

    virtual void onReceivePacket(IcomPacket& packet) override;

private:
    DVTimer audioOutTimer_;
    uint16_t audioSequenceNumber_;

    void onAudioOutTimer_();
};

}

}

}

#endif // AUDIO_STATE_H