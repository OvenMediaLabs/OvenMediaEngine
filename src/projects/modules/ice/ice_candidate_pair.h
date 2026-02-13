//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <base/ovsocket/ovsocket.h>
#include "ice_types.h"

class IceCandidatePair
{
public:
	IceCandidatePair(const ov::SocketAddressPair &pair, std::shared_ptr<ov::Socket> socket);

	std::shared_ptr<ov::Socket> GetSocket() const;

	// State management
	void SetState(IceConnectionState state);
	IceConnectionState GetState() const;

	// Socket Address Pair
    ov::SocketAddressPair GetAddressPair() const;

	ov::String ToString() const;

    void OnReceivedBindingRequest();
    void OnReceivedBindingResponse();

    // Valid candidate pair
    bool IsConnectable() const;

private:

	IceConnectionState _state = IceConnectionState::New;
    ov::SocketAddressPair _socket_address_pair;
	std::shared_ptr<ov::Socket> _socket = nullptr;

    bool _received_binding_request = false;
    bool _received_binding_response = false;
};