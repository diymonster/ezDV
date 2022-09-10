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

#ifndef ARE_YOU_READY_AUDIO_STATE_H
#define ARE_YOU_READY_AUDIO_STATE_H

#include "AreYouReadyState.h"

namespace ezdv
{

namespace network
{

namespace icom
{

class AreYouReadyAudioState : public AreYouReadyState
{
public:
    AreYouReadyAudioState(IcomStateMachine* parent);
    virtual ~AreYouReadyAudioState() = default;

    virtual void onEnterState() override;

protected:
    virtual void onReceivePacketImpl_(IcomPacket& packet) override;
};

}

}

}

#endif // ARE_YOU_READY_CONTROL_STATE_H