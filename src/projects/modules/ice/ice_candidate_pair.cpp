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
    _state = state;
}

IceConnectionState IceCandidatePair::GetState() const
{
    return _state;
}

// Socket Address Pair
ov::SocketAddressPair IceCandidatePair::GetAddressPair() const
{
    return _socket_address_pair;
}

ov::String IceCandidatePair::ToString() const
{
    return ov::String::FormatString("Socket: %s SocketAddressPair: %s State: %s", 
                        _socket->ToString().CStr(),
                        _socket_address_pair.ToString().CStr(),
                        IceConnectionStateToString(_state));
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
    return _received_binding_request && _received_binding_response;
}