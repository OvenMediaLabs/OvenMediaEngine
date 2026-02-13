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

#include "ice_port_observer.h"
#include "ice_candidate_pair.h"

class IceSession
{
public:
	enum class Role : uint8_t
	{
		UNDEFINED,
		CONTROLLED,
		CONTROLLING
	};

	IceSession(session_id_t session_id, IceSession::Role role, 
				const std::shared_ptr<const SessionDescription> &local_sdp, const std::shared_ptr<const SessionDescription> &peer_sdp,
				int expired_ms, uint64_t life_time_epoch_ms, 
				std::any user_data, const std::shared_ptr<IcePortObserver> &observer);

	
	void Refresh();
    bool IsExpired() const;

	// State management
	void SetState(IceConnectionState state);
	IceConnectionState GetState() const;

	// Role
	IceSession::Role GetRole() const;

	// SDP
	std::shared_ptr<const SessionDescription> GetLocalSdp() const;
	std::shared_ptr<const SessionDescription> GetPeerSdp() const;

	// Session ID
	uint32_t GetSessionID() const;
	// Local ufrag, used for identifying StunBindingRequest
	ov::String GetLocalUfrag() const;

	// Observer
	std::shared_ptr<IcePortObserver> GetObserver() const;
	// User data
	std::any GetUserData() const;

	// TURN client
	void SetTurnClient(bool is_turn_client);
	bool IsTurnClient() const;

	// Is data channel enabled
	void SetDataChannelEnabled(bool is_data_channel_enabled);
	bool IsDataChannelEnabled() const;

	// Data channel number
	void SetDataChannelNumber(uint16_t data_channel_number);
	uint16_t GetDataChannelNumber() const;

	// TURN peer address
	void SetTurnPeerAddress(const ov::SocketAddress& peer_address);
	ov::SocketAddress GetTurnPeerAddress() const;

	std::shared_ptr<IceCandidatePair> FindCandidatePair(const ov::SocketAddressPair& address_pair) const;

	// Candidate pairs
	void OnReceivedStunBindingRequest(const ov::SocketAddressPair& address_pair, const std::shared_ptr<ov::Socket>& socket);
	void OnReceivedStunBindingResponse(const ov::SocketAddressPair& address_pair, const std::shared_ptr<ov::Socket>& socket);
	void OnReceivedStunBindingErrorResponse(const ov::SocketAddressPair& address_pair, const std::shared_ptr<ov::Socket>& socket);

	bool IsConnectable(const ov::SocketAddressPair& address_pair);
	bool IsConnected(const ov::SocketAddressPair& address_pair);

	// USE-CANDIDATE, used for controlling role
	bool UseCandidate(const ov::SocketAddressPair& address_pair);

	// Connected candidate pairs
	std::shared_ptr<IceCandidatePair> GetConnectedCandidatePair() const;
	// Socket
	std::shared_ptr<ov::Socket> GetConnectedSocket() const;

	ov::String ToString() const;

private:

	// Manage candidate pairs
	std::shared_ptr<IceCandidatePair> CreateAndAddCandidatePair(const ov::SocketAddressPair& address_pair, const std::shared_ptr<ov::Socket>& socket);
	void RemoveCandidatePair(const ov::SocketAddressPair& address_pair);

	uint32_t _session_id;
	std::shared_ptr<const SessionDescription> _local_sdp = nullptr;
	std::shared_ptr<const SessionDescription> _peer_sdp = nullptr;
	
	Role _role = Role::UNDEFINED;

	// Global state among all candidate pairs
    IceConnectionState _state = IceConnectionState::New;

    // Candidate pairs
	mutable std::shared_mutex _connected_candidate_pair_mutex;
    std::shared_ptr<IceCandidatePair> _connected_candidate_pair;

	mutable std::shared_mutex _candidate_pairs_mutex;
    std::map<ov::SocketAddressPair, std::shared_ptr<IceCandidatePair>> _candidate_pairs;

	std::chrono::time_point<std::chrono::system_clock> _expire_time;
	const int _expire_after_ms;
	const uint64_t _lifetime_epoch_ms;

    // interfaces
    std::any _user_data;
    std::shared_ptr<IcePortObserver> _observer;

	// Connection information with TURN server
	bool _is_turn_client = false;
	bool _is_data_channel_enabled = false;
	ov::SocketAddress _turn_peer_address;
	uint16_t _data_channle_number = 0;
};