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

	// TURN framing is per candidate pair, not per session: a session can hold a
	// TURN-relayed pair and a direct pair simultaneously and switch the active
	// pair between them. These find-or-create the pair for address_pair and
	// record how to send on it (the inner packet is later routed by the same
	// address_pair, so this is the pair that becomes active for that path).
	void SetCandidatePairTurnDataChannel(const ov::SocketAddressPair& address_pair, const std::shared_ptr<ov::Socket>& socket, uint16_t channel_number);
	void SetCandidatePairTurnSendIndication(const ov::SocketAddressPair& address_pair, const std::shared_ptr<ov::Socket>& socket, const ov::SocketAddress& turn_peer_address);

	std::shared_ptr<IceCandidatePair> FindCandidatePair(const ov::SocketAddressPair& address_pair) const;

	// Distinct non-null sockets across all candidate pairs. Used on teardown:
	// the active pair may be UDP while the session still owns per-connection
	// TCP sockets (TURN-over-TCP / direct-TCP) from earlier path switches.
	std::vector<std::shared_ptr<ov::Socket>> GetCandidatePairSockets() const;

	// Candidate pairs
	void OnReceivedStunBindingRequest(const ov::SocketAddressPair& address_pair, const std::shared_ptr<ov::Socket>& socket);
	void OnReceivedStunBindingResponse(const ov::SocketAddressPair& address_pair, const std::shared_ptr<ov::Socket>& socket);
	void OnReceivedStunBindingErrorResponse(const ov::SocketAddressPair& address_pair, const std::shared_ptr<ov::Socket>& socket);

	bool IsConnectable(const ov::SocketAddressPair& address_pair);
	bool IsActive(const ov::SocketAddressPair& address_pair);

	// Mark a STUN-validated pair as nominated/eligible (multiple allowed).
	bool MarkNominated(const ov::SocketAddressPair& address_pair);

	// Make a STUN-validated pair the active pair (the one we send on).
	// False if the pair is unknown or has failed its STUN check.
	bool SelectActiveCandidatePair(const ov::SocketAddressPair& address_pair);

	// Active candidate pair (the single pair we send on)
	std::shared_ptr<IceCandidatePair> GetActiveCandidatePair() const;
	// Socket
	std::shared_ptr<ov::Socket> GetActiveSocket() const;

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
    std::atomic<IceConnectionState> _state{IceConnectionState::New};

    // Candidate pairs
	mutable ov::SharedMutex _active_candidate_pair_mutex;
    std::shared_ptr<IceCandidatePair> _active_candidate_pair OV_GUARDED_BY(_active_candidate_pair_mutex);

	mutable ov::SharedMutex _candidate_pairs_mutex;
    std::map<ov::SocketAddressPair, std::shared_ptr<IceCandidatePair>> _candidate_pairs OV_GUARDED_BY(_candidate_pairs_mutex);

	mutable ov::SharedMutex _expire_time_mutex;
	std::chrono::time_point<std::chrono::steady_clock> _expire_time OV_GUARDED_BY(_expire_time_mutex);
	const int _expire_after_ms;
	const uint64_t _lifetime_epoch_ms;

    // interfaces
    std::any _user_data;
    std::shared_ptr<IcePortObserver> _observer;
};
