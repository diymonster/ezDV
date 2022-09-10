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

#include "esp_log.h"
#include "AreYouReadyState.h"
#include "IcomStateMachine.h"

namespace ezdv
{

namespace network
{

namespace icom
{

AreYouReadyState::AreYouReadyState(IcomStateMachine* parent)
    : IcomProtocolState(parent)
{
    // empty
}

void AreYouReadyState::onEnterState()
{
    ESP_LOGI(parent_->getName().c_str(), "Entering state");

    auto packet = IcomPacket::CreateAreYouReadyPacket(parent_->getOurIdentifier(), parent_->getTheirIdentifier());
    parent_->sendUntracked(packet);
}

void AreYouReadyState::onExitState()
{
    ESP_LOGI(parent_->getName().c_str(), "Leaving state");
}

std::string AreYouReadyState::getName()
{
    return "AreYouReady";
}

void AreYouReadyState::onReceivePacket(IcomPacket& packet)
{
    if (packet.isIAmReady())
    {
        ESP_LOGI(parent_->getName().c_str(), "Received I Am Ready");
        onReceivePacketImpl_(packet);
    }
}

}

}

}