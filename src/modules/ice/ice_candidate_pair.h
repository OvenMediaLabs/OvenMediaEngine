//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <atomic>

#include <base/ovlibrary/ovlibrary.h>
#include <base/ovsocket/ovsocket.h>
#include "ice_types.h"

class IceCandidatePair
{
public:
	// How OME must frame outgoing packets on this pair. This is a property of
	// the pair, not the session: a session may have a TURN-relayed pair and a
	// direct pair at the same time and switch the active pair between them.
	enum class TransportType : uint8_t
	{
		Direct,				 // send as-is (RFC 4571 framing for direct-TCP is decided by the socket)
		TurnDataChannel,	 // wrap in a TURN ChannelData message
		TurnSendIndication,	 // wrap in a TURN Send Indication to the TURN peer address
	};

	IceCandidatePair(const ov::SocketAddressPair &pair, std::shared_ptr<ov::Socket> socket);

	std::shared_ptr<ov::Socket> GetSocket() const;

	// State management
	void SetState(IceConnectionState state);
	IceConnectionState GetState() const;
	// Atomically transition expected -> desired. Returns true only if this call
	// performed the transition (state was exactly `expected`).
	bool CompareExchangeState(IceConnectionState expected, IceConnectionState desired);

	// Socket Address Pair
    ov::SocketAddressPair GetAddressPair() const;

	ov::String ToString() const;

    void OnReceivedBindingRequest();
    void OnReceivedBindingResponse();

    // Valid candidate pair
    bool IsConnectable() const;

	// Outgoing framing for this pair. Set when TURN ChannelData / Send
	// Indication is received on this pair; defaults to Direct.
	void SetTurnDataChannel(uint16_t channel_number);
	void SetTurnSendIndication(const ov::SocketAddress &turn_peer_address);
	TransportType GetTransportType() const;
	uint16_t GetTurnChannelNumber() const;
	ov::SocketAddress GetTurnPeerAddress() const;

private:

	std::atomic<IceConnectionState> _state{IceConnectionState::New};
    ov::SocketAddressPair _socket_address_pair;
	std::shared_ptr<ov::Socket> _socket = nullptr;

    bool _received_binding_request = false;
    bool _received_binding_response = false;

	std::atomic<TransportType> _transport_type{TransportType::Direct};
	std::atomic<uint16_t> _turn_channel_number{0};
	mutable ov::Mutex _turn_peer_address_mutex;
	ov::SocketAddress _turn_peer_address OV_GUARDED_BY(_turn_peer_address_mutex);
};