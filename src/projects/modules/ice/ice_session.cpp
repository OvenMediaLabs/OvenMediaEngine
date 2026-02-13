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
	return ov::String::FormatString("IceSession: session_id=%u, role=%s, state=%s, local_ufrag=%s, expire_after_ms=%d, lifetime_epoch_ms=%llu, ConnectedCandidatePair=%s",
		_session_id, 
		_role == Role::CONTROLLED ? "CONTROLLED" : "CONTROLLING",
		IceConnectionStateToString(_state),
		GetLocalUfrag().CStr(),
		_expire_after_ms,
		_lifetime_epoch_ms, 
		_connected_candidate_pair ? _connected_candidate_pair->ToString().CStr() : "None");
}

void IceSession::Refresh()
{
	_expire_time = std::chrono::system_clock::now() + std::chrono::milliseconds(_expire_after_ms);
}

bool IceSession::IsExpired() const
{
	if (_lifetime_epoch_ms != 0 && _lifetime_epoch_ms < ov::Clock::NowMSec())
	{
		return true;
	}

	return (std::chrono::system_clock::now() > _expire_time);
}

void IceSession::SetState(IceConnectionState state)
{
	_state = state;
}

IceConnectionState IceSession::GetState() const
{
	return _state;
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

void IceSession::SetTurnClient(bool is_turn_client)
{
	_is_turn_client = is_turn_client;
}

bool IceSession::IsTurnClient() const
{
	return _is_turn_client;
}

// Is data channel enabled
void IceSession::SetDataChannelEnabled(bool is_data_channel_enabled)
{
	_is_data_channel_enabled = is_data_channel_enabled;	
}

bool IceSession::IsDataChannelEnabled() const
{
	return _is_data_channel_enabled;
}

// Data channel number
void IceSession::SetDataChannelNumber(uint16_t data_channel_number)
{
	_data_channle_number = data_channel_number;
}

uint16_t IceSession::GetDataChannelNumber() const
{
	return _data_channle_number;
}

// TURN peer address
void IceSession::SetTurnPeerAddress(const ov::SocketAddress& peer_address)
{
	_turn_peer_address = peer_address;
}

ov::SocketAddress IceSession::GetTurnPeerAddress() const
{
	return _turn_peer_address;
}

std::shared_ptr<IceCandidatePair> IceSession::GetConnectedCandidatePair() const
{
	std::shared_lock<std::shared_mutex> lock(_connected_candidate_pair_mutex);
	return _connected_candidate_pair;
}

std::shared_ptr<ov::Socket> IceSession::GetConnectedSocket() const
{
	auto connected_candidate_pair = GetConnectedCandidatePair();
	if (connected_candidate_pair == nullptr)
	{
		return nullptr;
	}
	
	return connected_candidate_pair->GetSocket();
}

std::shared_ptr<IceCandidatePair> IceSession::FindCandidatePair(const ov::SocketAddressPair& address_pair) const
{
	std::shared_lock<std::shared_mutex> lock(_candidate_pairs_mutex);

	auto it = _candidate_pairs.find(address_pair);
	if (it != _candidate_pairs.end())
	{
		return it->second;
	}

	return nullptr;
}

std::shared_ptr<IceCandidatePair> IceSession::CreateAndAddCandidatePair(const ov::SocketAddressPair& address_pair, const std::shared_ptr<ov::Socket>& socket)
{
	std::lock_guard<std::shared_mutex> lock(_candidate_pairs_mutex);

	auto candidate_pair = std::make_shared<IceCandidatePair>(address_pair, socket);
	_candidate_pairs.insert(std::make_pair(address_pair, candidate_pair));

	return candidate_pair;
}

void IceSession::RemoveCandidatePair(const ov::SocketAddressPair& address_pair)
{
	std::lock_guard<std::shared_mutex> lock(_candidate_pairs_mutex);

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

bool IceSession::IsConnected(const ov::SocketAddressPair& address_pair)
{
	auto connected_candidate_pair = GetConnectedCandidatePair();
	if (connected_candidate_pair == nullptr)
	{
		return false;
	}

	return connected_candidate_pair->GetAddressPair() == address_pair;
}

// USE-CANDIDATE, used for controlling role
bool IceSession::UseCandidate(const ov::SocketAddressPair& address_pair)
{
	std::lock_guard<std::shared_mutex> lock(_connected_candidate_pair_mutex);

	// TODO(Getroot) : Consider the case where the ICE restart occurs
	if (GetState() != IceConnectionState::Checking)
	{
		logte("ICE session : %u | UseCandidate() | Invalid state: %s", GetSessionID(), IceConnectionStateToString(GetState()));
		return false;
	}

	auto candidate_pair = FindCandidatePair(address_pair);
	if (candidate_pair == nullptr)
	{
		logte("ICE session : %u | UseCandidate() | No candidate pair found for address pair: %s", GetSessionID(), address_pair.ToString().CStr());
		return false;
	}

	// candidate state
	candidate_pair->SetState(IceConnectionState::Connected);
	_connected_candidate_pair = candidate_pair;

	// Global state
	SetState(IceConnectionState::Connected);

	return true;
}