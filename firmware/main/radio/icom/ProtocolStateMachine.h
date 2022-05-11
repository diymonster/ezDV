#ifndef RADIO__ICOM__PROTOCOL_STATE_MACHINE_H
#define RADIO__ICOM__PROTOCOL_STATE_MACHINE_H

#include <map>
#include "smooth/core/Task.h"
#include "smooth/core/ipc/TaskEventQueue.h"
#include "smooth/core/fsm/StaticFSM.h"
#include "smooth/core/network/IPv4.h"
#include "smooth/core/timer/Timer.h"
#include "smooth/core/timer/TimerExpiredEvent.h"
#include "PacketTypes.h"
#include "UdpSocket.h"

namespace sm1000neo::radio::icom
{
    class ProtocolStateMachine;
    
    constexpr std::size_t LargestStateSize = 64;
    
    class BaseState
    {
    public:
        explicit BaseState(ProtocolStateMachine& sm)
            : sm_(sm)
        {
            // empty
        }
        
        virtual ~BaseState() = default;
        
        virtual void enter_state() { }
        
        virtual void leave_state() { }
        
        virtual std::string name() = 0;
        
        virtual void packetReceived(IcomPacket& packet) { }
        
        virtual void event(const smooth::core::network::event::DataAvailableEvent<IcomProtocol>& event) { }
        
    protected:
        ProtocolStateMachine& sm_;
    };
    
    class ProtocolStateMachine 
        : public smooth::core::fsm::StaticFSM<BaseState, LargestStateSize>
        , public smooth::core::ipc::IEventListener<smooth::core::network::event::TransmitBufferEmptyEvent>
        , public smooth::core::ipc::IEventListener<smooth::core::network::event::ConnectionStatusEvent>
        , public smooth::core::ipc::IEventListener<smooth::core::network::event::DataAvailableEvent<IcomProtocol>>
    {
    public:
        enum StateMachineType
        {
            CONTROL_SM,
            CIV_SM,
            AUDIO_SM,
        };
        
        ProtocolStateMachine(StateMachineType smType, smooth::core::Task& task);
        virtual ~ProtocolStateMachine() = default;
        
        StateMachineType getStateMachineType() const;
        
        [[nodiscard]] std::string get_name() const;
        
        void start(std::string ip, uint16_t controlPort, std::string username, std::string password);
        
        void event(const smooth::core::network::event::DataAvailableEvent<IcomProtocol>& event) override;
        
        void event(const smooth::core::network::event::TransmitBufferEmptyEvent& event) override { }
        void event(const smooth::core::network::event::ConnectionStatusEvent& event) override;
        
        smooth::core::Task& getTask() { return task_; }        
        uint32_t getOurIdentifier() const { return ourIdentifier_; }
        uint32_t getTheirIdentifier() const { return theirIdentifier_; }
        void setTheirIdentifier(uint32_t id) { theirIdentifier_ = id; }
        
        void sendUntracked(IcomPacket& packet);
        void sendPing();
        void sendLoginPacket();
        void sendTracked(IcomPacket& packet);
        
    private:
        StateMachineType smType_;
        smooth::core::Task& task_;
        uint32_t ourIdentifier_;
        uint32_t theirIdentifier_;
        uint16_t pingSequenceNumber_;
        uint16_t authSequenceNumber_;
        uint16_t sendSequenceNumber_;
        std::shared_ptr<smooth::core::network::BufferContainer<IcomProtocol>> buffer_;
        std::shared_ptr<smooth::core::network::Socket<IcomProtocol, IcomPacket>> socket_;
        std::shared_ptr<smooth::core::network::InetAddress> address_;
        
        std::map<uint16_t, std::pair<uint64_t, IcomPacket> > sentPackets_;
        
        std::string username_;
        std::string password_;
    };
    
    class AreYouThereState 
        : public BaseState
        , public smooth::core::ipc::IEventListener<smooth::core::timer::TimerExpiredEvent>
    {
        public:
            explicit AreYouThereState(ProtocolStateMachine& fsm)
                    : BaseState(fsm)
            {
                // empty
            }

            virtual ~AreYouThereState() = default;
            
            std::string name() override
            {
                return "AreYouThere";
            }

            virtual void enter_state() override;
            virtual void leave_state() override;
            
            void event(const smooth::core::timer::TimerExpiredEvent& event) override;
            virtual void packetReceived(IcomPacket& packet) override;
            
        private:
            smooth::core::timer::TimerOwner areYouThereRetransmitTimer_;
            std::shared_ptr<smooth::core::ipc::TaskEventQueue<smooth::core::timer::TimerExpiredEvent>> areYouThereTimerExpiredQueue_;
    };
    
    class AreYouReadyState 
        : public BaseState
    {
        public:
            explicit AreYouReadyState(ProtocolStateMachine& fsm)
                    : BaseState(fsm)
            {
                // empty
            }

            virtual ~AreYouReadyState() = default;
            
            std::string name() override
            {
                return "AreYouReady";
            }

            virtual void enter_state() override;
            virtual void leave_state() override;
            
            virtual void packetReceived(IcomPacket& packet) override;
    };
    
    class LoginState 
        : public BaseState
    {
        public:
            explicit LoginState(ProtocolStateMachine& fsm)
                    : BaseState(fsm)
            {
                // empty
            }

            virtual ~LoginState() = default;
            
            std::string name() override
            {
                return "Login";
            }

            virtual void enter_state() override;
            virtual void leave_state() override;
            
            virtual void packetReceived(IcomPacket& packet) override;
    };
}

#endif // RADIO__ICOM__PROTOCOL_STATE_MACHINE_H
