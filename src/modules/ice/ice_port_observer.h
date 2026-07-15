//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/session.h>
#include <base/ovlibrary/ovlibrary.h>
#include <modules/rtc_signalling/rtc_ice_candidate.h>
#include <modules/sdp/session_description.h>
#include <memory>

#include "ice_types.h"

class IcePort;

class IcePortObserver : public ov::EnableSharedFromThis<IcePortObserver>
{
public:
	void SetId(uint32_t id)
	{
		_id = id;
	}

	uint32_t GetId() const
	{
		return _id;
	}

	void SetTurnServerSocketType(ov::SocketType type)
	{
		_turn_server_socket_type = type;
	}

	void SetTurnServerPort(uint16_t port)
	{
		_turn_server_port = port;
	}

	// Notifies the observer that the ICE session changed state.
	// `is_expired` is reported separately because lifetime-expired sessions may be
	// delivered as the same transport state as ordinary disconnects, but Enterprise
	// ingress alerts must classify them as PolicyExpired instead of NetworkError.
	virtual void OnStateChanged(IcePort &port, uint32_t session_id, IceConnectionState state, bool is_expired, std::any user_data)
	{
		// dummy function
	}
	virtual void OnRouteChanged(IcePort &port)
	{
		// dummy function
	}
	virtual void OnDataReceived(IcePort &port, uint32_t session_id, std::shared_ptr<const ov::Data> data, std::any user_data) = 0;

	virtual RtcIceCandidateList &GetIceCandidateList()
	{
		return _ice_candidate_list;
	}

	void AppendIceCandidates(const RtcIceCandidateList &ice_candidate_list)
	{
		// IcePortManager::GenerateIceCandidates() emits transport-homogeneous groups
		// (one group per (port, socket_type)), so classifying by the first candidate is sufficient.
		for (const auto &group : ice_candidate_list)
		{
			if (group.empty())
			{
				continue;
			}

			if (group.front().GetTransport().UpperCaseString() == "TCP")
			{
				_tcp_candidate_groups.push_back(group);
			}
			else
			{
				_udp_candidate_groups.push_back(group);
			}
		}
		_ice_candidate_list.insert(_ice_candidate_list.end(), ice_candidate_list.begin(), ice_candidate_list.end());
	}

	// Pre-split by transport type for efficient round-robin selection.
	// Populated at AppendIceCandidates time so callers need not re-classify per request.
	const RtcIceCandidateList &GetUdpCandidateGroups() const { return _udp_candidate_groups; }
	const RtcIceCandidateList &GetTcpCandidateGroups() const { return _tcp_candidate_groups; }

protected:
	uint32_t _id = 0;
	// Full grouped list. Each group is transport-homogeneous (one (port, socket_type) per group).
	RtcIceCandidateList _ice_candidate_list;
	// Subsets pre-split by transport for fast round-robin access
	RtcIceCandidateList _udp_candidate_groups;
	RtcIceCandidateList _tcp_candidate_groups;
	ov::SocketType _turn_server_socket_type = ov::SocketType::Unknown;
	uint16_t _turn_server_port = 0;
};
