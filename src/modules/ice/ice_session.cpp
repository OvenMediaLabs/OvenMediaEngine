//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#include "ice_session.h"
#include "ice_private.h"

IceSession::IceSession(session_id_t session_id, IceSession::Role role, 
				const std::shared_ptr<const SessionDescription> &local_sdp, const std::shared_ptr<const SessionDescription> &peer_sdp,
				int expired_ms, uint64_t life_time_epoch_ms, 
				std::any user_data, const std::shared_ptr<IcePortObserver> &observer)
				: _session_id(session_id),  
				_local_sdp(local_sdp), _peer_sdp(peer_sdp), _role(role),
				_expire_after_ms(expired_ms), _lifetime_epoch_ms(life_time_epoch_ms), 
				_user_data(user_data), _observer(observer)
{
	Refresh();
}

ov::String IceSession::ToString() const
{
	auto active_candidate_pair = GetActiveCandidatePair();

	return ov::String::FormatString("IceSession: session_id=%u, role=%s, state=%s, local_ufrag=%s, expire_after_ms=%d, lifetime_epoch_ms=%" PRIu64 ", ActivePair=%s",
		_session_id, 
		_role == Role::CONTROLLED ? "CONTROLLED" : "CONTROLLING",
		IceConnectionStateToString(_state.load()),
		GetLocalUfrag().CStr(),
		_expire_after_ms,
		_lifetime_epoch_ms, 
		(active_candidate_pair != nullptr) ? active_candidate_pair->ToString().CStr() : "None");
}

void IceSession::Refresh()
{
	ov::ScopedLock lock(_expire_time_mutex);
	_expire_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(_expire_after_ms);
}

bool IceSession::IsExpired() const
{
	if (_lifetime_epoch_ms != 0 && _lifetime_epoch_ms < ov::Clock::NowMSec())
	{
		return true;
	}

	ov::SharedLockGuard lock(_expire_time_mutex);
	return (std::chrono::steady_clock::now() > _expire_time);
}

void IceSession::SetState(IceConnectionState state)
{
	_state.store(state);
}

IceConnectionState IceSession::GetState() const
{
	return _state.load();
}

IceSession::Role IceSession::GetRole() const
{
	return _role;
}

std::shared_ptr<const SessionDescription> IceSession::GetLocalSdp() const
{
	return _local_sdp;
}

std::shared_ptr<const SessionDescription> IceSession::GetPeerSdp() const
{
	return _peer_sdp;
}

uint32_t IceSession::GetSessionID() const
{
	return _session_id;
}

ov::String IceSession::GetLocalUfrag() const
{
	return _local_sdp->GetIceUfrag();
}

std::shared_ptr<IcePortObserver> IceSession::GetObserver() const
{
	return _observer;
}

std::any IceSession::GetUserData() const
{
	return _user_data;
}

void IceSession::SetCandidatePairTurnDataChannel(const ov::SocketAddressPair& address_pair, const std::shared_ptr<ov::Socket>& socket, uint16_t channel_number)
{
	auto candidate_pair = FindCandidatePair(address_pair);
	if (candidate_pair == nullptr)
	{
		// TURN ChannelBind/ChannelData can arrive before the relayed STUN
		// connectivity check that would otherwise create the pair.
		candidate_pair = CreateAndAddCandidatePair(address_pair, socket);
	}

	candidate_pair->SetTurnDataChannel(channel_number);
}

void IceSession::SetCandidatePairTurnSendIndication(const ov::SocketAddressPair& address_pair, const std::shared_ptr<ov::Socket>& socket, const ov::SocketAddress& turn_peer_address)
{
	auto candidate_pair = FindCandidatePair(address_pair);
	if (candidate_pair == nullptr)
	{
		candidate_pair = CreateAndAddCandidatePair(address_pair, socket);
	}

	candidate_pair->SetTurnSendIndication(turn_peer_address);
}

std::shared_ptr<IceCandidatePair> IceSession::GetActiveCandidatePair() const
{
	ov::SharedLockGuard lock(_active_candidate_pair_mutex);
	return _active_candidate_pair;
}

std::shared_ptr<ov::Socket> IceSession::GetActiveSocket() const
{
	auto active_candidate_pair = GetActiveCandidatePair();
	if (active_candidate_pair == nullptr)
	{
		return nullptr;
	}
	
	return active_candidate_pair->GetSocket();
}

std::shared_ptr<IceCandidatePair> IceSession::FindCandidatePair(const ov::SocketAddressPair& address_pair) const
{
	ov::SharedLockGuard lock(_candidate_pairs_mutex);

	auto it = _candidate_pairs.find(address_pair);
	if (it != _candidate_pairs.end())
	{
		return it->second;
	}

	return nullptr;
}

std::vector<std::shared_ptr<ov::Socket>> IceSession::GetCandidatePairSockets() const
{
	ov::SharedLockGuard lock(_candidate_pairs_mutex);

	std::vector<std::shared_ptr<ov::Socket>> sockets;
	for (const auto& [address_pair, candidate_pair] : _candidate_pairs)
	{
		auto socket = candidate_pair->GetSocket();
		if (socket == nullptr)
		{
			continue;
		}

		// Distinct sockets only (the same socket can back multiple pairs,
		// e.g. the shared UDP listener or one TCP connection).
		bool already_added = false;
		for (const auto& s : sockets)
		{
			if (s == socket)
			{
				already_added = true;
				break;
			}
		}

		if (already_added == false)
		{
			sockets.push_back(socket);
		}
	}

	return sockets;
}

std::shared_ptr<IceCandidatePair> IceSession::CreateAndAddCandidatePair(const ov::SocketAddressPair& address_pair, const std::shared_ptr<ov::Socket>& socket)
{
	ov::ScopedLock lock(_candidate_pairs_mutex);

	// Avoid TOCTOU: another thread may have inserted the same address_pair
	// after FindCandidatePair() and before this function acquires the lock.
	auto it = _candidate_pairs.find(address_pair);
	if (it != _candidate_pairs.end())
	{
		return it->second;
	}

	auto candidate_pair = std::make_shared<IceCandidatePair>(address_pair, socket);
	_candidate_pairs.insert(std::make_pair(address_pair, candidate_pair));

	return candidate_pair;
}

void IceSession::RemoveCandidatePair(const ov::SocketAddressPair& address_pair)
{
	ov::ScopedLock lock(_candidate_pairs_mutex);

	_candidate_pairs.erase(address_pair);
}

// Candidate pairs
void IceSession::OnReceivedStunBindingRequest(const ov::SocketAddressPair& address_pair, const std::shared_ptr<ov::Socket>& socket)
{
	auto candidate_pair = FindCandidatePair(address_pair);
	if (candidate_pair == nullptr)
	{
		// new candidate pair
		candidate_pair = CreateAndAddCandidatePair(address_pair, socket);
	}

	// candidate state
	candidate_pair->OnReceivedBindingRequest();

	if (candidate_pair->GetState() == IceConnectionState::New)
	{
		candidate_pair->SetState(IceConnectionState::Checking);	

		// Global state
		if (GetState() == IceConnectionState::New)
		{
			SetState(IceConnectionState::Checking);
		}
	}
}

void IceSession::OnReceivedStunBindingResponse(const ov::SocketAddressPair& address_pair, const std::shared_ptr<ov::Socket>& socket)
{
	auto candidate_pair = FindCandidatePair(address_pair);
	if (candidate_pair == nullptr)
	{
		// new candidate pair
		candidate_pair = CreateAndAddCandidatePair(address_pair, socket);
	}

	// candidate state
	candidate_pair->OnReceivedBindingResponse();

	if (candidate_pair->GetState() == IceConnectionState::New)
	{
		candidate_pair->SetState(IceConnectionState::Checking);	

		// Global state
		if (GetState() == IceConnectionState::New)
		{
			SetState(IceConnectionState::Checking);
		}
	}
}

void IceSession::OnReceivedStunBindingErrorResponse(const ov::SocketAddressPair& address_pair, const std::shared_ptr<ov::Socket>& socket)
{
	auto candidate_pair = FindCandidatePair(address_pair);
	if (candidate_pair == nullptr)
	{
		// Nothitng to do
		return;
	}

	candidate_pair->SetState(IceConnectionState::Failed);
}

bool IceSession::IsConnectable(const ov::SocketAddressPair& address_pair)
{
	auto candidate_pair = FindCandidatePair(address_pair);
	if (candidate_pair == nullptr)
	{
		return false;
	}

	return candidate_pair->IsConnectable();
}

bool IceSession::IsActive(const ov::SocketAddressPair& address_pair)
{
	auto active_candidate_pair = GetActiveCandidatePair();
	if (active_candidate_pair == nullptr)
	{
		return false;
	}

	return active_candidate_pair->GetAddressPair() == address_pair;
}

// Mark a STUN-validated pair as nominated/eligible. Multiple pairs may be
// nominated; the one we actually send on is chosen by SelectActiveCandidatePair()
// when the peer sends application data on it. Returns true only when the pair
// is newly nominated (idempotent: false if it was already nominated).
bool IceSession::MarkNominated(const ov::SocketAddressPair& address_pair)
{
	auto candidate_pair = FindCandidatePair(address_pair);
	if (candidate_pair == nullptr)
	{
		logtw("ICE session : %u | MarkNominated() | No candidate pair for %s", GetSessionID(), address_pair.ToString().CStr());
		return false;
	}

	// Atomically nominate only a freshly validated (Checking) pair. The CAS
	// makes this race-free against a concurrent MarkNominated() and against
	// OnReceivedStunBindingErrorResponse() setting Failed: an already Connected
	// pair is a no-op and a Failed pair is never resurrected. false means
	// "nothing changed" (the caller runs the quick-connect burst once per pair).
	return candidate_pair->CompareExchangeState(IceConnectionState::Checking, IceConnectionState::Connected);
}

// Make a nominated pair the active pair (the one we send on). Called when the
// peer sends application data on it, so OME follows the pair the peer chose
// and switches if the peer moves.
bool IceSession::SelectActiveCandidatePair(const ov::SocketAddressPair& address_pair)
{
	ov::ScopedLock lock(_active_candidate_pair_mutex);

	if (_active_candidate_pair != nullptr && _active_candidate_pair->GetAddressPair() == address_pair)
	{
		// Already the active pair
		return true;
	}

	// Must be a STUN-validated pair (candidate pairs are created only after
	// ufrag + MESSAGE-INTEGRITY pass) that has not failed its check.
	auto candidate_pair = FindCandidatePair(address_pair);
	if (candidate_pair == nullptr || candidate_pair->GetState() == IceConnectionState::Failed)
	{
		return false;
	}

	auto previous = _active_candidate_pair;

	_active_candidate_pair = candidate_pair;
	SetState(IceConnectionState::Connected);

	// ToString() includes the socket (TCP/UDP + addresses), useful for tracing
	// which path the peer moved to.
	if (previous == nullptr)
	{
		logti("ICE session %u | active pair selected: %s", GetSessionID(), candidate_pair->ToString().CStr());
	}
	else
	{
		logti("ICE session %u | active pair switched: %s -> %s",
			  GetSessionID(), previous->ToString().CStr(), candidate_pair->ToString().CStr());
	}

	return true;
}
