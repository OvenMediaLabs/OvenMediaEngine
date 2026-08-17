//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#include "ice_candidate_pair.h"

IceCandidatePair::IceCandidatePair(const ov::SocketAddressPair &pair, std::shared_ptr<ov::Socket> socket)
	: _socket_address_pair(pair), _socket(socket)
{
}

std::shared_ptr<ov::Socket> IceCandidatePair::GetSocket() const
{
    return _socket;
}

// State management
void IceCandidatePair::SetState(IceConnectionState state)
{
    _state.store(state);
}

IceConnectionState IceCandidatePair::GetState() const
{
    return _state.load();
}

bool IceCandidatePair::CompareExchangeState(IceConnectionState expected, IceConnectionState desired)
{
    return _state.compare_exchange_strong(expected, desired);
}

// Socket Address Pair
ov::SocketAddressPair IceCandidatePair::GetAddressPair() const
{
    return _socket_address_pair;
}

ov::String IceCandidatePair::ToString() const
{
    return ov::String::FormatString("Socket: %s SocketAddressPair: %s State: %s",
                        _socket != nullptr ? _socket->ToString().CStr() : "(null)",
                        _socket_address_pair.ToString().CStr(),
                        IceConnectionStateToString(GetState()));
}

void IceCandidatePair::OnReceivedBindingRequest()
{
    _received_binding_request = true;
}

void IceCandidatePair::OnReceivedBindingResponse()
{
    _received_binding_response = true;
}

// Valid candidate pair
bool IceCandidatePair::IsConnectable() const
{
    // A pair whose STUN check Failed (error response) must not be treated as
    // connectable: otherwise the CONTROLLING role keeps advertising
    // USE-CANDIDATE on it and the quick-connect path keeps retrying it, even
    // though SelectActiveCandidatePair() rejects Failed pairs.
    return _state.load() != IceConnectionState::Failed
        && _received_binding_request && _received_binding_response;
}

void IceCandidatePair::SetTurnDataChannel(uint16_t channel_number)
{
    _turn_channel_number.store(channel_number);
    _transport_type.store(TransportType::TurnDataChannel);
}

void IceCandidatePair::SetTurnSendIndication(const ov::SocketAddress &turn_peer_address)
{
    {
        ov::LockGuard<ov::Mutex> lock(_turn_peer_address_mutex);
        _turn_peer_address = turn_peer_address;
    }
    _transport_type.store(TransportType::TurnSendIndication);
}

IceCandidatePair::TransportType IceCandidatePair::GetTransportType() const
{
    return _transport_type.load();
}

uint16_t IceCandidatePair::GetTurnChannelNumber() const
{
    return _turn_channel_number.load();
}

ov::SocketAddress IceCandidatePair::GetTurnPeerAddress() const
{
    ov::LockGuard<ov::Mutex> lock(_turn_peer_address_mutex);
    return _turn_peer_address;
}