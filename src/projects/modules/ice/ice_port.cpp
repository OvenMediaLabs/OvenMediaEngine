//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "ice_port.h"

#include <base/info/application.h>
#include <base/info/stream.h>
#include <base/ovcrypto/message_digest.h>
#include <base/ovlibrary/ovlibrary.h>
#include <config/config.h>
#include <modules/rtc_signalling/rtc_ice_candidate.h>

#include <algorithm>

#include "ice_private.h"
#include "main/main.h"
#include "modules/ice/stun/attributes/stun_attributes.h"
#include "modules/ice/stun/channel_data_message.h"
#include "modules/ice/stun/stun_message.h"

IcePort::IcePort()
{
	_timer.Push(
		[this](void *paramter) -> ov::DelayQueueAction {
			CheckTimedOut();
			return ov::DelayQueueAction::Repeat;
		},
		1000);
	_timer.Start();
}

IcePort::~IcePort()
{
	_timer.Stop();

	Close();
}

bool IcePort::CreateIceCandidates(const char *server_name, const cfg::Server &server_config, const RtcIceCandidateList &ice_candidate_list, int ice_worker_count)
{
	std::lock_guard<std::recursive_mutex> lock_guard(_physical_port_list_mutex);

	bool result = true;
	std::map<ov::SocketAddress, bool> bounded;

	auto &ip_list = server_config.GetIPList();
	std::vector<ov::String> ice_address_string_list;

	for (auto &ice_candidates : ice_candidate_list)
	{
		for (auto &ice_candidate : ice_candidates)
		{
			// Find same candidate is already created
			auto transport = ice_candidate.GetTransport().UpperCaseString();
			const auto address = ice_candidate.GetAddress();
			ov::SocketType socket_type = (transport == "TCP") ? ov::SocketType::Tcp : ov::SocketType::Udp;

			// Create address list for the candidates from IP addresses
			std::vector<ov::SocketAddress> ice_address_list;

			try
			{
				ice_address_list = ov::SocketAddress::Create(ip_list, address.Port());
			}
			catch (const ov::Error &e)
			{
				logte("Could not create socket address: %s", e.What());
				return false;
			}

			for (auto &ice_address : ice_address_list)
			{
				{
					auto item = bounded.find(ice_address);
					if (item != bounded.end())
					{
						// Already opened
						logtt("ICE port is already bound to %s/%s", ice_address.ToString().CStr(), transport.CStr());
						continue;
					}

					bounded[ice_address] = true;
				}

				// Create an ICE port using candidate information
				auto physical_port = CreatePhysicalPort(ice_address, socket_type, ice_worker_count);
				if (physical_port == nullptr)
				{
					logte("Could not create physical port for %s/%s", ice_address.ToString().CStr(), transport.CStr());
					result = false;
					break;
				}

				ice_address_string_list.push_back(
					ov::String::FormatString(
						"%s/%s (%p)",
						ice_address.ToString().CStr(),
						transport.CStr(),
						physical_port.get()));

				_physical_port_list.push_back(physical_port);
			}
		}
	}

	if (result)
	{
		logti("ICE port%s listening on %s (for %s)",
			  (ice_address_string_list.size() == 1) ? " is" : "s are",
			  ov::String::Join(ice_address_string_list, ", ").CStr(),
			  server_name);
	}
	else
	{
		Close();
	}

	return result;
}

bool IcePort::CreateTurnServer(const ov::SocketAddress &address, ov::SocketType socket_type, int tcp_relay_worker_count)
{
	// {[Browser][WebRTC][TURN Client]} <----(TCP)-----> {[TURN Server][OvenMediaEngine]}

	// OME introduces a built-in TURN server to support WebRTC/TCP.
	// there are networks although the network speed is high but UDP packet loss occurs very seriously. There, WebRTC/udp does not play normally.

	// In order to playback in such an environment with good quality,
	// we have built in TURN server in OME to transmit WebRTC stream to tcp.
	// The built-in turn server does not use UDP when transmitting or receiving data from the relayed port to the peer,
	// and only needs to copy the memory within the same process, thus the udp transmission section between OME and Player is omitted.
	// In other words, Player and OME can communicate with only TCP.

	// If the peer is the same process as TurnServer (OME), it does not transmit through UDP and calls the function directly.
	// Player --[TURN/TCP]--> [TurnServer(OME) --[Fucntion Call not udp send]--> Peer(OME)]
	// Player <--[TURN/TCP]-- [TurnServer(OME) <--[Fucntion Call not udp send]-- Peer(OME)]

	// ov::SocketAddress address(listening_port);
	auto physical_port = CreatePhysicalPort(address, socket_type, tcp_relay_worker_count);
	if (physical_port == nullptr)
	{
		logte("Could not create physical port for %s/%s", address.ToString().CStr(), StringFromSocketType(socket_type));
		return false;
	}

	_physical_port_list.push_back(physical_port);

	// make HMAC key
	// https://tools.ietf.org/html/rfc8489#section-9.2.2
	//  key = MD5(username ":" OpaqueString(realm) ":" OpaqueString(password))
	_hmac_key = ov::MessageDigest::ComputeDigest(ov::CryptoAlgorithm::Md5,
												 ov::String::FormatString("%s:%s:%s", DEFAULT_RELAY_USERNAME, DEFAULT_RELAY_REALM, DEFAULT_RELAY_KEY).ToData(false));

	// Creating an attribute to be used in advance

	// Nonce
	auto nonce_attribute = std::make_shared<StunNonceAttribute>();
	nonce_attribute->SetText("1bcf94ca7494141e");
	_nonce_attribute = std::move(nonce_attribute);

	// Realm
	auto realm_attribute = std::make_shared<StunRealmAttribute>();
	realm_attribute->SetText(DEFAULT_RELAY_REALM);
	_realm_attribute = std::move(realm_attribute);

	// Software
	auto software_attribute = std::make_shared<StunSoftwareAttribute>();
	software_attribute->SetText(ov::String::FormatString("OvenMediaEngine v%s", OME_VERSION));
	_software_attribute = std::move(software_attribute);

	// Add XOR-RELAYED-ADDRESS attribute
	// This is the player's candidate and passed to OME.
	// However, OME does not use the player's candidate. So we pass anything by this value.
	auto xor_relayed_address_attribute = std::make_shared<StunXorRelayedAddressAttribute>();

	if (address.IsIPv4())
	{
		xor_relayed_address_attribute->SetParameters(ov::SocketAddress::CreateAndGetFirst(FAKE_RELAY_IP4, FAKE_RELAY_PORT));
		_xor_relayed_address_attribute_for_ipv4 = std::move(xor_relayed_address_attribute);
	}
	else
	{
		xor_relayed_address_attribute->SetParameters(ov::SocketAddress::CreateAndGetFirst(FAKE_RELAY_IP6, FAKE_RELAY_PORT));
		_xor_relayed_address_attribute_for_ipv6 = std::move(xor_relayed_address_attribute);
	}

	return true;
}

std::shared_ptr<PhysicalPort> IcePort::CreatePhysicalPort(const ov::SocketAddress &address, ov::SocketType type, int worker_count)
{
	auto physical_port = PhysicalPortManager::GetInstance()->CreatePort("ICE", type, address, worker_count);
	if (physical_port != nullptr)
	{
		if (physical_port->AddObserver(this))
		{
			return physical_port;
		}

		logte("Cannot add a observer %p to %p", this, physical_port.get());

		PhysicalPortManager::GetInstance()->DeletePort(physical_port);
	}
	else
	{
		logte("Cannot create physical port for %s (type: %s), workers: %d", address.ToString().CStr(), ov::StringFromSocketType(type), worker_count);
	}

	return nullptr;
}

bool IcePort::Close()
{
	std::lock_guard<std::recursive_mutex> lock_guard(_physical_port_list_mutex);

	bool result = true;

	for (auto &physical_port : _physical_port_list)
	{
		result = result && physical_port->RemoveObserver(this);
		result = result && PhysicalPortManager::GetInstance()->DeletePort(physical_port);

		if (result == false)
		{
			logtt("Cannot close ICE port");
			break;
		}
	}

	_timer.Stop();

	return result;
}

ov::String IcePort::GenerateUfrag()
{
	std::shared_lock<std::shared_mutex> lock(_ice_sessions_with_ufrag_lock);

	while (true)
	{
		ov::String ufrag = ov::Random::GenerateString(6);

		if (_ice_sessions_with_ufrag.find(ufrag) == _ice_sessions_with_ufrag.end())
		{
			logtt("Generated ufrag: %s", ufrag.CStr());

			return ufrag;
		}
	}
}

bool IcePort::AddIceSession(session_id_t session_id, const std::shared_ptr<IceSession> &ice_session)
{
	std::lock_guard<std::shared_mutex> lock_guard(_ice_sessions_with_id_lock);
	auto item = _ice_seesions_with_id.find(session_id);
	if (item == _ice_seesions_with_id.end())
	{
		_ice_seesions_with_id.emplace(session_id, ice_session);
		return true;
	}

	return false;
}

bool IcePort::AddIceSession(const ov::String &local_ufrag, const std::shared_ptr<IceSession> &ice_session)
{
	std::lock_guard<std::shared_mutex> lock_guard(_ice_sessions_with_ufrag_lock);
	auto item = _ice_sessions_with_ufrag.find(local_ufrag);
	if (item == _ice_sessions_with_ufrag.end())
	{
		_ice_sessions_with_ufrag.emplace(local_ufrag, ice_session);
		return true;
	}

	return false;
}

bool IcePort::AddIceSession(const ov::SocketAddressPair &address_pair, const std::shared_ptr<IceSession> &ice_session)
{
	std::lock_guard<std::shared_mutex> lock_guard(_ice_sessions_with_address_pair_lock);
	auto item = _ice_sessions_with_address_pair.find(address_pair);
	if (item == _ice_sessions_with_address_pair.end())
	{
		_ice_sessions_with_address_pair.emplace(address_pair, ice_session);
		return true;
	}

	return false;
}

std::shared_ptr<IceSession> IcePort::FindIceSession(session_id_t session_id)
{
	std::shared_lock<std::shared_mutex> lock_guard(_ice_sessions_with_address_pair_lock);
	auto item = _ice_seesions_with_id.find(session_id);
	if (item != _ice_seesions_with_id.end())
	{
		return item->second;
	}

	return nullptr;
}

std::shared_ptr<IceSession> IcePort::FindIceSession(const ov::String &local_ufrag)
{
	std::shared_lock<std::shared_mutex> lock_guard(_ice_sessions_with_ufrag_lock);
	auto item = _ice_sessions_with_ufrag.find(local_ufrag);
	if (item != _ice_sessions_with_ufrag.end())
	{
		return item->second;
	}

	return nullptr;
}

std::shared_ptr<IceSession> IcePort::FindIceSession(const ov::SocketAddressPair &socket_address_pair)
{
	std::shared_lock<std::shared_mutex> lock_guard(_ice_sessions_with_address_pair_lock);
	auto item = _ice_sessions_with_address_pair.find(socket_address_pair);
	if (item != _ice_sessions_with_address_pair.end())
	{
		return item->second;
	}

	return nullptr;
}

session_id_t IcePort::IssueUniqueSessionId()
{
	return _session_id_counter++;
}

void IcePort::AddSession(const std::shared_ptr<IcePortObserver> &observer, session_id_t session_id, IceSession::Role role,
						 const std::shared_ptr<const SessionDescription> &local_sdp, const std::shared_ptr<const SessionDescription> &peer_sdp,
						 int expired_ms, uint64_t life_time_epoch_ms, std::any user_data)
{
	const ov::String &local_ufrag = local_sdp->GetIceUfrag();
	const ov::String &peer_ufrag = peer_sdp->GetIceUfrag();

	auto old_session = FindIceSession(local_ufrag);
	if (old_session != nullptr)
	{
		OV_ASSERT(false, "Duplicated ufrag: %s:%s, session_id: %d (existing session_id: %d)", local_ufrag.CStr(), peer_ufrag.CStr(), session_id, old_session->GetSessionID());
	}

	logtt("Trying to add session: %d (ufrag: %s:%s)...", session_id, local_ufrag.CStr(), peer_ufrag.CStr());

	std::shared_ptr<IceSession> new_session = std::make_shared<IceSession>(session_id, role, local_sdp, peer_sdp, expired_ms, life_time_epoch_ms, user_data, observer);

	if (AddIceSession(session_id, new_session) == false)
	{
		OV_ASSERT(false, "Duplicated session_id: %d", session_id);
		logte("Duplicated session_id: %d", session_id);
		return;
	}

	if (AddIceSession(local_ufrag, new_session) == false)
	{
		OV_ASSERT(false, "Duplicated ufrag: %s:%s, session_id: %d", local_ufrag.CStr(), peer_ufrag.CStr(), session_id);
		logte("Duplicated ufrag: %s:%s, session_id: %d", local_ufrag.CStr(), peer_ufrag.CStr(), session_id);
		return;
	}

	logti("Added session: %d (ufrag: %s:%s)", session_id, local_ufrag.CStr(), peer_ufrag.CStr());
}

bool IcePort::DisconnectSession(session_id_t session_id)
{
	auto ice_session = FindIceSession(session_id);
	if (ice_session == nullptr)
	{
		logtt("Could not find session: %d", session_id);
		return false;
	}

	// It will be deleted in the next timer (for thread safety)
	ice_session->SetState(IceConnectionState::Disconnecting);

	return true;
}

bool IcePort::RemoveSession(session_id_t session_id)
{
	auto ice_session = FindIceSession(session_id);
	if (ice_session == nullptr)
	{
		logtt("Could not find session: %d", session_id);
		return false;
	}

	size_t ice_sessions_with_id_size = 0;
	size_t ice_sessions_with_ufrag_size = 0;
	size_t ice_sessions_with_address_pair_size = 0;

	// Remove from _ice_sessions_with_id
	{
		std::lock_guard<std::shared_mutex> lock_guard(_ice_sessions_with_id_lock);
		_ice_seesions_with_id.erase(session_id);
		ice_sessions_with_id_size = _ice_seesions_with_id.size();
	}

	// Remove from _ice_sessions_with_ufrag
	{
		std::lock_guard<std::shared_mutex> lock_guard(_ice_sessions_with_ufrag_lock);
		_ice_sessions_with_ufrag.erase(ice_session->GetLocalUfrag());
		ice_sessions_with_ufrag_size = _ice_sessions_with_ufrag.size();
	}

	// Remove from _ice_sessions_with_address_pair if it exists
	{
		std::lock_guard<std::shared_mutex> lock_guard(_ice_sessions_with_address_pair_lock);
		auto connected_candidate_pair = ice_session->GetConnectedCandidatePair();
		if (connected_candidate_pair != nullptr)
		{
			_ice_sessions_with_address_pair.erase(connected_candidate_pair->GetAddressPair());
		}
		ice_sessions_with_address_pair_size = _ice_sessions_with_address_pair.size();
	}

	{
		// Close only TCP (TURN)
		auto remote = ice_session->GetConnectedSocket();
		if (remote != nullptr)
		{
			if (remote->GetSocket().GetType() == ov::SocketType::Tcp)
			{
				remote->CloseIfNeeded();
			}
		}
	}

	logti("Removed session(%u) from ICEPort | ice_seesions_with_id count(%zu) ice_sessions_with_ufrag(%zu) ice_sessions_with_address_pair(%zu) ", session_id, ice_sessions_with_id_size, ice_sessions_with_ufrag_size, ice_sessions_with_address_pair_size);

	return true;
}

bool IcePort::StoreIceSessionWithTransactionId(const std::shared_ptr<IceSession> &ice_session, const ov::String &transaction_id)
{
	std::lock_guard<std::shared_mutex> lock_guard(_binding_requests_with_transaction_id_lock);
	auto item = _binding_requests_with_transaction_id.find(transaction_id);
	if (item != _binding_requests_with_transaction_id.end())
	{
		logte("Duplicated transaction_id: %s", transaction_id.CStr());
		return false;
	}

	_binding_requests_with_transaction_id.emplace(transaction_id, BindingRequestInfo(transaction_id, ice_session));

	return true;
}

std::shared_ptr<IceSession> IcePort::FindIceSessionWithTransactionId(const ov::String &transaction_id)
{
	std::shared_lock<std::shared_mutex> lock_guard(_binding_requests_with_transaction_id_lock);
	auto item = _binding_requests_with_transaction_id.find(transaction_id);
	if (item == _binding_requests_with_transaction_id.end())
	{
		return nullptr;
	}

	return item->second._ice_session;
}

bool IcePort::RemoveTransaction(const ov::String &transaction_id)
{
	std::lock_guard<std::shared_mutex> lock_guard(_binding_requests_with_transaction_id_lock);
	auto item = _binding_requests_with_transaction_id.find(transaction_id);
	if (item == _binding_requests_with_transaction_id.end())
	{
		return false;
	}

	_binding_requests_with_transaction_id.erase(item);

	return true;
}

void IcePort::CheckTimedOut()
{
	// Remove expired transction items
	{
		std::lock_guard<std::shared_mutex> brt_lock(_binding_requests_with_transaction_id_lock);

		for (auto it = _binding_requests_with_transaction_id.begin(); it != _binding_requests_with_transaction_id.end();)
		{
			if (it->second.IsExpired())
			{
				it = _binding_requests_with_transaction_id.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	// Collect terminated sessions for thread safety
	std::vector<std::shared_ptr<IceSession>> terminated_session_list;
	{
		std::shared_lock<std::shared_mutex> lock_guard(_ice_sessions_with_id_lock);

		for (const auto &item : _ice_seesions_with_id)
		{
			auto session = item.second;
			if (session->IsExpired() || session->GetState() == IceConnectionState::Disconnecting)
			{
				terminated_session_list.push_back(session);
			}
		}
	}

	// Remove terminated sessions and notify
	for (auto &terminated_session : terminated_session_list)
	{
		RemoveSession(terminated_session->GetSessionID());

		auto connected_candidate_pair = terminated_session->GetConnectedCandidatePair();

		if (terminated_session->IsExpired())
		{
			terminated_session->SetState(IceConnectionState::Disconnected);

			logtw("Agent [%s, %u] has expired", connected_candidate_pair != nullptr ? connected_candidate_pair->ToString().CStr() : "Unknow", terminated_session->GetSessionID());
		}
		else
		{
			terminated_session->SetState(IceConnectionState::Closed);
			logti("Agent [%s, %u] has closed", connected_candidate_pair != nullptr ? connected_candidate_pair->ToString().CStr() : "Unknow", terminated_session->GetSessionID());
		}

		NotifyIceSessionStateChanged(terminated_session);
	}
}

bool IcePort::Send(session_id_t session_id, const std::shared_ptr<RtpPacket> &packet)
{
	return Send(session_id, packet->GetData());
}

bool IcePort::Send(session_id_t session_id, const std::shared_ptr<RtcpPacket> &packet)
{
	return Send(session_id, packet->GetData());
}

bool IcePort::Send(session_id_t session_id, const std::shared_ptr<const ov::Data> &data)
{
	std::shared_ptr<IceSession> ice_session = FindIceSession(session_id);
	if (ice_session == nullptr || ice_session->GetState() != IceConnectionState::Connected)
	{
		logtt("IcePort::Send - Could not find session: %d", session_id);
		return false;
	}

	std::shared_ptr<const ov::Data> send_data = nullptr;

	// Send throutgh TURN server Data Channel proxy
	if (ice_session->IsTurnClient() == true && ice_session->IsDataChannelEnabled() == true)
	{
		send_data = CreateChannelDataMessage(ice_session->GetDataChannelNumber(), data);
	}
	// Send thourgh TURN server Data Indication proxy
	else if (ice_session->IsTurnClient() == true && ice_session->IsDataChannelEnabled() == false)
	{
		send_data = CreateDataIndication(ice_session->GetTurnPeerAddress(), data);
	}
	// Send direct
	else
	{
		send_data = data;
	}

	if (send_data == nullptr)
	{
		return false;
	}

	auto remote = ice_session->GetConnectedSocket();
	if (remote == nullptr)
	{
		logte("IcePort::Send - Could not find connected remote socket: %d", session_id);
		return false;
	}

	// TODO(Getroot) : Change to use Local / Remote address of candidate pair
	auto connected_candidate_pair = ice_session->GetConnectedCandidatePair();
	if (connected_candidate_pair == nullptr)
	{
		return false;
	}

	return remote->SendFromTo(connected_candidate_pair->GetAddressPair(), send_data);
}

void IcePort::OnConnected(const std::shared_ptr<ov::Socket> &remote)
{
	// called when TURN client connected to the turn server with TCP
	auto demultiplexer = std::make_shared<IceTcpDemultiplexer>();

	std::lock_guard<std::shared_mutex> lock_guard(_demultiplexers_lock);
	_demultiplexers[remote->GetNativeHandle()] = demultiplexer;

	logti("Turn client has connected : %s", remote->ToString().CStr());
}

void IcePort::OnDisconnected(const std::shared_ptr<ov::Socket> &remote, PhysicalPortDisconnectReason reason, const std::shared_ptr<const ov::Error> &error)
{
	// called when TURN client disconnected from the turn server with TCP
	std::lock_guard<std::shared_mutex> lock_guard(_demultiplexers_lock);

	auto it = _demultiplexers.find(remote->GetNativeHandle());
	if (it != _demultiplexers.end())
	{
		_demultiplexers.erase(remote->GetNativeHandle());
	}

	logti("Turn client has disconnected : %s", remote->ToString().CStr());
}

void IcePort::OnDataReceived(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddress &address, const std::shared_ptr<const ov::Data> &data)
{
	// The only packet input to IcePort/TCP is STUN and TURN DATA CHANNEL.
	std::shared_lock<std::shared_mutex> lock(_demultiplexers_lock);
	// If remote protocol is tcp, it must be TURN
	if (_demultiplexers.find(remote->GetNativeHandle()) == _demultiplexers.end())
	{
		// If the client disconnects at this time, it cannot be found.
		logtt("TCP packet input but cannot find the demultiplexer of %s.", remote->ToString().CStr());
		return;
	}

	auto demultiplexer = _demultiplexers[remote->GetNativeHandle()];
	lock.unlock();

	ov::SocketAddressPair address_pair(*remote->GetLocalAddress(), address);

	// TCP demultiplexer
	demultiplexer->AppendData(data);

	while (demultiplexer->IsAvailablePacket())
	{
		auto packet = demultiplexer->PopPacket();

		GateInfo gate_info;
		gate_info.packet_type = packet->GetPacketType();
		OnPacketReceived(remote, address_pair, gate_info, packet->GetData());
	}
}

void IcePort::OnDatagramReceived(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, const std::shared_ptr<const ov::Data> &data)
{
	GateInfo gate_info;
	gate_info.packet_type = IcePacketIdentifier::FindPacketType(data);

	OnPacketReceived(remote, address_pair, gate_info, data);
}

void IcePort::OnPacketReceived(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, GateInfo &gate_info, const std::shared_ptr<const ov::Data> &data)
{
	logtt("OnPacketReceived %s (%s)", gate_info.ToString().CStr(), address_pair.ToString().CStr());

	switch (gate_info.packet_type)
	{
		case IcePacketIdentifier::PacketType::TURN_CHANNEL_DATA:
			OnChannelDataPacketReceived(remote, address_pair, gate_info, data);
			break;
		case IcePacketIdentifier::PacketType::STUN:
			OnStunPacketReceived(remote, address_pair, gate_info, data);
			break;
		case IcePacketIdentifier::PacketType::RTP_RTCP:
		case IcePacketIdentifier::PacketType::DTLS:
			OnApplicationPacketReceived(remote, address_pair, gate_info, data);
			break;
		case IcePacketIdentifier::PacketType::ZRTP:
		case IcePacketIdentifier::PacketType::UNKNOWN:
			break;
	}
}

void IcePort::OnApplicationPacketReceived(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair,
										  GateInfo &gate_info, const std::shared_ptr<const ov::Data> &data)
{
	// TODO(Getroot) : After adding the local address parameter to this function, I need to modify the line below
	auto ice_session = FindIceSession(address_pair);
	if (ice_session == nullptr)
	{
		logtw("Could not find agent(%s) information. Dropping... [%s]", address_pair.ToString().CStr(), gate_info.ToString().CStr());
		return;
	}

	// When the candidate pair is determined, the peer starts sending DTLS messages. This can be seen as a true connected.
	if (ice_session->GetState() != IceConnectionState::Connected)
	{
		// If the peer is not connected, it is not a valid packet.
		logtw("Received application packet from invalid peer(%s). Dropping...", address_pair.ToString().CStr());
		return;
	}

	if (ice_session->GetObserver() != nullptr)
	{
		// Some webrtc peer does not send STUN Binding Request repeatedly. So, I determine the peer is alive by receiving application data.
		ice_session->Refresh();
		ice_session->GetObserver()->OnDataReceived(*this, ice_session->GetSessionID(), data, ice_session->GetUserData());
	}
}

void IcePort::OnChannelDataPacketReceived(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, GateInfo &gate_info, const std::shared_ptr<const ov::Data> &data)
{
	ChannelDataMessage message;

	if (message.Load(data) == false)
	{
		return;
	}

	GateInfo application_gate_info;

	application_gate_info.input_method = IcePort::GateInfo::GateType::DATA_CHANNEL;
	application_gate_info.channel_number = message.GetChannelNumber();
	application_gate_info.packet_type = IcePacketIdentifier::FindPacketType(message.GetData());

	// Update GateInfo
	// If a request comes from a send indication or channel, this is through a turn. When transmitting a packet to the player, it must be sent through a data indication or channel, so it stores related information.
	auto ice_session = FindIceSession(address_pair);
	if (ice_session != nullptr)
	{
		ice_session->SetTurnClient(true);
		ice_session->SetDataChannelEnabled(true);
		ice_session->SetDataChannelNumber(application_gate_info.channel_number);
	}

	// Decapsulate and process the packet again.
	OnPacketReceived(remote, address_pair, application_gate_info, message.GetData());
}

void IcePort::OnStunPacketReceived(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, GateInfo &gate_info, const std::shared_ptr<const ov::Data> &data)
{
	ov::ByteStream stream(data.get());
	StunMessage message;

	logtt("Trying to parse a STUN message from data...\n%s", data->Dump().CStr());

	if (message.Parse(stream) == false)
	{
		logte("Could not parse STUN packet from %s", remote->ToString().CStr());
		return;
	}

	logtt("Received message:\n%s", message.ToString().CStr());

	if (message.GetClass() == StunClass::ErrorResponse)
	{
		// Print
		auto error_code = message.GetAttribute<StunErrorCodeAttribute>(StunAttributeType::ErrorCode);
		if (error_code == nullptr)
		{
			logtw("Received stun error response, but there is no ErrorCode attribute");
		}
		else
		{
			logtw("Received stun error response (Error code : %d Reason : %s)", error_code->GetErrorCodeNumber(), error_code->GetErrorReason().CStr());
		}
	}

	switch (message.GetMethod())
	{
		// STUN
		case StunMethod::Binding: {
			switch (message.GetClass())
			{
				case StunClass::Request:
				case StunClass::Indication:
					OnReceivedStunBindingRequest(remote, address_pair, gate_info, message);
					break;
				case StunClass::SuccessResponse:
					OnReceivedStunBindingResponse(remote, address_pair, gate_info, message);
					break;
				case StunClass::ErrorResponse:
					//TODO(Getroot): Delete the transaction immediately.
					break;
			}
			break;
		}
		// TURN Server
		// Because it is a turn server, no response class comes.
		case StunMethod::Allocate:
			if (message.GetClass() == StunClass::Request)
			{
				OnReceivedTurnAllocateRequest(remote, address_pair, gate_info, message);
			}
			break;
		case StunMethod::Refresh:
			if (message.GetClass() == StunClass::Request)
			{
				OnReceivedTurnRefreshRequest(remote, address_pair, gate_info, message);
			}
			break;
		case StunMethod::Send:
			if (message.GetClass() == StunClass::Indication)
			{
				OnReceivedTurnSendIndication(remote, address_pair, gate_info, message);
			}
			break;
		case StunMethod::CreatePermission:
			if (message.GetClass() == StunClass::Request)
			{
				OnReceivedTurnCreatePermissionRequest(remote, address_pair, gate_info, message);
			}
			break;
		case StunMethod::ChannelBind:
			if (message.GetClass() == StunClass::Request)
			{
				OnReceivedTurnChannelBindRequest(remote, address_pair, gate_info, message);
			}
			break;
		case StunMethod::Data:
			// Since this is a turn server, it does not receive a data method.
			logtt("Bad Packet - TURN Server cannot receive the Stun Data method(%s)", remote->ToString().CStr());
			break;
		default:
			OV_ASSERT(false, "Not implemented method: %d", ov::ToUnderlyingType(message.GetMethod()));
			logtw("Unknown method: %d", ov::ToUnderlyingType(message.GetMethod()));
			break;
	}
}

bool IcePort::UseCandidate(const std::shared_ptr<IceSession> &ice_session, const ov::SocketAddressPair &address_pair)
{
	if (ice_session->GetState() == IceConnectionState::Connected && ice_session->GetConnectedCandidatePair()->GetAddressPair() == address_pair)
	{
		// Already connected
		return true;
	}

	if (ice_session->UseCandidate(address_pair) == false)
	{
		return false;
	}

	logti("Session %u uses candidate: %s", ice_session->GetSessionID(), address_pair.ToString().CStr());
	AddIceSession(address_pair, ice_session);

	return true;
}

bool IcePort::OnReceivedStunBindingRequest(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, GateInfo &gate_info, const StunMessage &message)
{
	// Binding Request
	ov::String local_ufrag;
	ov::String peer_ufrag;

	if (message.GetUfrags(&local_ufrag, &peer_ufrag) == false)
	{
		logtw("Could not process user name attribute");
		return false;
	}

	logtt("[%s] Received STUN binding request: %s:%s", address_pair.ToString().CStr(), local_ufrag.CStr(), peer_ufrag.CStr());

	auto ice_session = FindIceSession(local_ufrag);
	if (ice_session == nullptr || ice_session->GetPeerSdp() == nullptr || ice_session->GetLocalSdp() == nullptr)
	{
		// Stun may arrive first before AddSession, it is not an error
		logtt("User not found: %s", local_ufrag.CStr());
		return false;
	}

	ice_session->Refresh();

	if (ice_session->GetPeerSdp()->GetIceUfrag() != peer_ufrag)
	{
		logtw("Mismatched ufrag: %s (ufrag in peer SDP: %s)", peer_ufrag.CStr(), ice_session->GetPeerSdp()->GetIceUfrag().CStr());
	}

	if (message.CheckIntegrity(ice_session->GetLocalSdp()->GetIcePwd()) == false)
	{
		logtw("Failed to check integrity");

		ice_session->SetState(IceConnectionState::Failed);
		NotifyIceSessionStateChanged(ice_session);
		RemoveSession(ice_session->GetSessionID());

		return false;
	}

	// Add the candidate to the session
	auto old_state = ice_session->GetState();
	ice_session->OnReceivedStunBindingRequest(address_pair, remote);

	// Check the USE-CANDIDATE attribute if session role is controlled
	if (ice_session->GetRole() == IceSession::Role::CONTROLLED)
	{
		auto use_candidate_attr = message.GetAttribute<StunAttribute>(StunAttributeType::UseCandidate);
		if (use_candidate_attr != nullptr)
		{
			UseCandidate(ice_session, address_pair);
		}
	}

	if (old_state != ice_session->GetState())
	{
		NotifyIceSessionStateChanged(ice_session);
	}

	// If the class is Indication it doesn't need to send response
	if (message.GetClass() == StunClass::Request)
	{
		StunMessage response_message;
		response_message.SetHeader(StunClass::SuccessResponse, StunMethod::Binding, message.GetTransactionId());

		// Add XOR-MAPPED-ADDRESS attribute
		// If client is relay, then use relay IP/port
		auto xor_mapped_attribute = std::make_shared<StunXorMappedAddressAttribute>();

		if (gate_info.input_method == GateInfo::GateType::DIRECT)
		{
			xor_mapped_attribute->SetParameters(address_pair.GetRemoteAddress());
		}
		else
		{
			xor_mapped_attribute->SetParameters(ov::SocketAddress::CreateAndGetFirst(address_pair.GetRemoteAddress().IsIPv6() ? FAKE_RELAY_IP6 : FAKE_RELAY_IP4, FAKE_RELAY_PORT));
		}

		response_message.AddAttribute(std::move(xor_mapped_attribute));

		// Send Stun Binding Response
		// TODO: apply SASLprep(password)
		SendStunMessage(remote, address_pair, gate_info, response_message, ice_session->GetLocalSdp()->GetIcePwd().ToData(false));
	}

	// Already connected, we don't send stun binding request to another peer address
	if (ice_session->GetState() == IceConnectionState::Connected)
	{
		auto connected_candidate_pair = ice_session->GetConnectedCandidatePair();
		if (connected_candidate_pair == nullptr)
		{
			// This should not happen
			logte("IceSession(%u) is in connected state, but connected candidate pair is null", ice_session->GetSessionID());
			return false;
		}

		if (connected_candidate_pair->GetAddressPair() != address_pair)
		{
			logtt("Already connected with another address : Connected(%s) Bind Request(%s)", connected_candidate_pair->GetAddressPair().ToString().CStr(), address_pair.ToString().CStr());
			// Didn't respond
			return true;
		}
	}

	// OvenMediaEngine sends Stun Binding Request to peer at this point
	SendStunBindingRequest(remote, address_pair, gate_info, ice_session);

	return true;
}

bool IcePort::SendStunBindingRequest(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, GateInfo &gate_info, const std::shared_ptr<IceSession> &ice_session)
{
	logtt("IceSession : %s", ice_session->ToString().CStr());

	StunMessage message;

	message.SetClass(StunClass::Request);
	message.SetMethod(StunMethod::Binding);
	// TODO: make transaction_id unique
	uint8_t transaction_id[OV_STUN_TRANSACTION_ID_LENGTH];
	uint8_t charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

	// generate transaction id ramdomly
	for (int index = 0; index < OV_STUN_TRANSACTION_ID_LENGTH; index++)
	{
		transaction_id[index] = charset[rand() % (OV_COUNTOF(charset) - 1)];
	}

	message.SetTransactionId(&(transaction_id[0]));

	// USERNAME attribute
	auto user_name_attr = std::make_shared<StunUserNameAttribute>();
	user_name_attr->SetText(ov::String::FormatString("%s:%s", ice_session->GetPeerSdp()->GetIceUfrag().CStr(), ice_session->GetLocalSdp()->GetIceUfrag().CStr()));
	message.AddAttribute(user_name_attr);

	if (ice_session->GetRole() == IceSession::Role::CONTROLLING)
	{
		// ICE-CONTROLLING
		auto ice_controlling_attr = std::make_shared<StunIceControllingAttribute>();
		// There is no chance of a client and ICE role conflict occurring in OME.
		ice_controlling_attr->SetValue(0x0000000000000001);
		message.AddAttribute(ice_controlling_attr);

		// TODO(Getroot): For now, it connect to the first connectable candidate pair,
		// but we have to select the best pair among all nominated pairs and connect.
		if (ice_session->GetState() == IceConnectionState::Checking && ice_session->IsConnectable(address_pair))
		{
			UseCandidate(ice_session, address_pair);
		}

		// Ice Session State can be changed upper code, so check it again.
		if (ice_session->GetState() == IceConnectionState::Connected && ice_session->IsConnected(address_pair))
		{
			// USE-CANDIDATE Attribute
			auto use_candidate_attr = std::make_shared<StunUseCandidateAttribute>();
			message.AddAttribute(use_candidate_attr);

			logtt("Use candidate [%s] Gate [%s] State [%s]", address_pair.ToString().CStr(), gate_info.ToString().CStr(), IceConnectionStateToString(ice_session->GetState()));
		}
		else
		{
			logtt("Not yet use candidate [%s] Gate [%s] State [%s]", address_pair.ToString().CStr(), gate_info.ToString().CStr(), IceConnectionStateToString(ice_session->GetState()));
		}
	}
	else if (ice_session->GetRole() == IceSession::Role::CONTROLLED)
	{
		// ICE-CONTROLLED
		auto ice_controlled_attr = std::make_shared<StunIceControlledAttribute>();
		// There is no chance of a client and ICE role conflict occurring in OME.
		ice_controlled_attr->SetValue(0x0000000000000001);
		message.AddAttribute(ice_controlled_attr);
	}

	// PRIORITY (Common Attribute)
	auto priority_attr = std::make_shared<StunPriorityAttribute>();
	priority_attr->SetValue(0x627F1EFF);
	message.AddAttribute(priority_attr);

	logtt("Send Stun Binding Request : %s", address_pair.ToString().CStr());

	// Store binding request transction
	{
		std::lock_guard<std::shared_mutex> brt_lock(_binding_requests_with_transaction_id_lock);

		ov::String transaction_id_key((char *)(&transaction_id[0]), OV_STUN_TRANSACTION_ID_LENGTH);
		_binding_requests_with_transaction_id.emplace(transaction_id_key, BindingRequestInfo(transaction_id_key, ice_session));

		logtt("Send Binding Request to(%s) id(%s)", address_pair.ToString().CStr(), transaction_id_key.CStr());
	}

	// TODO: apply SASLprep(password)
	SendStunMessage(remote, address_pair, gate_info, message, ice_session->GetPeerSdp()->GetIcePwd().ToData(false));

	return true;
}

bool IcePort::OnReceivedStunBindingResponse(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, GateInfo &gate_info, const StunMessage &message)
{
	ov::String transaction_id_key((char *)(&message.GetTransactionId()[0]), OV_STUN_TRANSACTION_ID_LENGTH);

	auto ice_session = FindIceSessionWithTransactionId(transaction_id_key);
	if (ice_session == nullptr)
	{
		logtw("Could not find binding request info : address(%s) transaction id(%s)", address_pair.ToString().CStr(), transaction_id_key.CStr());
		return false;
	}

	ice_session->Refresh();

	// Erase ended transction item
	RemoveTransaction(transaction_id_key);

	logtt("Receive stun binding response from %s, table size(%zu)", address_pair.ToString().CStr(), _binding_requests_with_transaction_id.size());

	if (message.CheckIntegrity(ice_session->GetLocalSdp()->GetIcePwd()) == false)
	{
		logtw("Failed to check integrity");
		return false;
	}

	logtt("Client %s sent STUN binding response", address_pair.ToString().CStr());

	auto old_state = ice_session->GetState();
	ice_session->OnReceivedStunBindingResponse(address_pair, remote);

	if (ice_session->GetState() == IceConnectionState::Checking && ice_session->IsConnectable(address_pair))
	{
		UseCandidate(ice_session, address_pair);

		// Send STUN Binding Request for quickly connect
		SendStunBindingRequest(remote, address_pair, gate_info, ice_session);
	}

	if (old_state != ice_session->GetState())
	{
		NotifyIceSessionStateChanged(ice_session);
	}

	return true;
}

bool IcePort::SendStunMessage(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, GateInfo &gate_info, StunMessage &message, const std::shared_ptr<const ov::Data> &integrity_key)
{
	std::shared_ptr<const ov::Data> source_data, send_data;

	if (integrity_key == nullptr)
	{
		source_data = message.Serialize();
	}
	else
	{
		source_data = message.Serialize(integrity_key);
	}

	logtt("Send message: [%s/%s]\n%s", gate_info.ToString().CStr(), address_pair.ToString().CStr(), message.ToString().CStr());

	if (gate_info.input_method == IcePort::GateInfo::GateType::DIRECT)
	{
		send_data = source_data;
	}
	else if (gate_info.input_method == IcePort::GateInfo::GateType::SEND_INDICATION)
	{
		send_data = CreateDataIndication(gate_info.peer_address, source_data);
	}
	else if (gate_info.input_method == IcePort::GateInfo::GateType::DATA_CHANNEL)
	{
		send_data = CreateChannelDataMessage(gate_info.channel_number, source_data);
	}

	if (send_data == nullptr)
	{
		return false;
	}

	auto sent_bytes = remote->SendFromTo(address_pair, send_data);

	return sent_bytes > 0;
}

const std::shared_ptr<const ov::Data> IcePort::CreateDataIndication(const ov::SocketAddress &peer_address, const std::shared_ptr<const ov::Data> &data)
{
	StunMessage send_indication_message;
	send_indication_message.SetHeader(StunClass::Indication, StunMethod::Data, reinterpret_cast<uint8_t *>(ov::Random::GenerateString(20).GetBuffer()));

	auto data_attribute = std::make_shared<StunDataAttribute>();
	data_attribute->SetData(data);
	send_indication_message.AddAttribute(std::move(data_attribute));

	auto xor_peer_attribute = std::make_shared<StunXorPeerAddressAttribute>();
	xor_peer_attribute->SetParameters(peer_address);
	send_indication_message.AddAttribute(std::move(xor_peer_attribute));

	send_indication_message.AddAttribute(_software_attribute);

	auto send_data = send_indication_message.Serialize();

	logtt("Send Data Indication:\n%s", send_indication_message.ToString().CStr());

	return send_data;
}

const std::shared_ptr<const ov::Data> IcePort::CreateChannelDataMessage(uint16_t channel_number, const std::shared_ptr<const ov::Data> &data)
{
	ChannelDataMessage channel_data_message(channel_number, data);
	return channel_data_message.GetPacket();
}

bool IcePort::OnReceivedTurnAllocateRequest(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, GateInfo &gate_info, const StunMessage &message)
{
	StunMessage response_message;

	auto requested_transport_attr = message.GetAttribute<StunRequestedTransportAttribute>(StunAttributeType::RequestedTransport);
	if (requested_transport_attr == nullptr)
	{
		response_message.SetHeader(StunClass::ErrorResponse, StunMethod::Allocate, message.GetTransactionId());
		response_message.SetErrorCodeAttribute(StunErrorCode::BadRequest, "REQUESTED-TRANSPORT attribute is not included");
		SendStunMessage(remote, address_pair, gate_info, response_message);
		return false;
	}

	// only protocol number 17(UDP) is allowed (https://www.iana.org/assignments/protocol-numbers/protocol-numbers.xhtml)
	if (requested_transport_attr->GetProtocolNumber() != 17)
	{
		response_message.SetHeader(StunClass::ErrorResponse, StunMethod::Allocate, message.GetTransactionId());
		response_message.SetErrorCodeAttribute(StunErrorCode::UnsupportedTransportProtocol);
		SendStunMessage(remote, address_pair, gate_info, response_message);
		return false;
	}

	auto integrity_attribute = message.GetAttribute<StunMessageIntegrityAttribute>(StunAttributeType::MessageIntegrity);
	if (integrity_attribute == nullptr)
	{
		// First request
		response_message.SetHeader(StunClass::ErrorResponse, StunMethod::Allocate, message.GetTransactionId());
		response_message.SetErrorCodeAttribute(StunErrorCode::Unauthonticated);

		response_message.AddAttribute(_nonce_attribute);
		response_message.AddAttribute(_realm_attribute);
		response_message.AddAttribute(_software_attribute);

		SendStunMessage(remote, address_pair, gate_info, response_message);

		// This is the original protocol specification.
		return true;
	}

	// TODO: Check authentication information, USERNAME, REALM, NONCE, MESSAGE-INTEGRITY

	response_message.SetHeader(StunClass::SuccessResponse, StunMethod::Allocate, message.GetTransactionId());

	// Add XOR-MAPPED-ADDRESS attribute
	auto xor_mapped_address_attribute = std::make_shared<StunXorMappedAddressAttribute>();
	xor_mapped_address_attribute->SetParameters(address_pair.GetRemoteAddress());
	response_message.AddAttribute(std::move(xor_mapped_address_attribute));

	// Add lifetime
	uint32_t lifetime = DEFAULT_LIFETIME;
	auto requested_lifetime_attribute = message.GetAttribute<StunLifetimeAttribute>(StunAttributeType::Lifetime);
	if (requested_lifetime_attribute != nullptr)
	{
		lifetime = std::min(static_cast<uint32_t>(DEFAULT_LIFETIME), requested_lifetime_attribute->GetValue());
	}

	auto lifetime_attribute = std::make_shared<StunLifetimeAttribute>();
	lifetime_attribute->SetValue(lifetime);
	response_message.AddAttribute(lifetime_attribute);

	response_message.AddAttribute(address_pair.GetRemoteAddress().IsIPv6() ? _xor_relayed_address_attribute_for_ipv6 : _xor_relayed_address_attribute_for_ipv4);
	response_message.AddAttribute(_software_attribute);

	SendStunMessage(remote, address_pair, gate_info, response_message, _hmac_key);

	return true;
}

bool IcePort::OnReceivedTurnSendIndication(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, GateInfo &gate_info, const StunMessage &message)
{
	auto xor_peer_attribute = message.GetAttribute<StunXorPeerAddressAttribute>(StunAttributeType::XorPeerAddress);
	if (xor_peer_attribute == nullptr)
	{
		return false;
	}

	auto data_attribute = message.GetAttribute<StunDataAttribute>(StunAttributeType::Data);
	if (data_attribute == nullptr)
	{
		return false;
	}

	auto data = data_attribute->GetData();

	gate_info.packet_type = IcePacketIdentifier::FindPacketType(data);
	gate_info.input_method = IcePort::GateInfo::GateType::SEND_INDICATION;
	gate_info.peer_address = xor_peer_attribute->GetAddress();

	std::shared_ptr<IceSession> ice_session = FindIceSession(address_pair);
	// Connected Session,
	// if agent sends data to TURN server (Send Indication) then ome will respond to agent through TURN server (Send Indication)
	if (ice_session != nullptr)
	{
		ice_session->SetTurnClient(true);
		ice_session->SetDataChannelEnabled(false);
		ice_session->SetTurnPeerAddress(gate_info.peer_address);
	}

	OnPacketReceived(remote, address_pair, gate_info, data);

	return true;
}

bool IcePort::OnReceivedTurnCreatePermissionRequest(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, GateInfo &gate_info, const StunMessage &message)
{
	//TODO(Getroot): Check validation
	StunMessage response_message;
	response_message.SetHeader(StunClass::SuccessResponse, StunMethod::CreatePermission, message.GetTransactionId());
	SendStunMessage(remote, address_pair, gate_info, response_message, _hmac_key);

	return true;
}

bool IcePort::OnReceivedTurnChannelBindRequest(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, GateInfo &gate_info, const StunMessage &message)
{
	StunMessage response_message;

	auto channel_number_attribute = message.GetAttribute<StunChannelNumberAttribute>(StunAttributeType::ChannelNumber);
	if (channel_number_attribute == nullptr)
	{
		response_message.SetHeader(StunClass::ErrorResponse, StunMethod::ChannelBind, message.GetTransactionId());
		response_message.SetErrorCodeAttribute(StunErrorCode::BadRequest);
		SendStunMessage(remote, address_pair, gate_info, response_message);
		return false;
	}

	response_message.SetHeader(StunClass::SuccessResponse, StunMethod::ChannelBind, message.GetTransactionId());
	SendStunMessage(remote, address_pair, gate_info, response_message, _hmac_key);

	std::shared_ptr<IceSession> ice_session = FindIceSession(address_pair);
	// Connected Session,
	// if agent sends data to TURN server (Data Channel) then ome will respond to agent through TURN server (Data Channel)
	if (ice_session != nullptr)
	{
		ice_session->SetTurnClient(true);
		ice_session->SetDataChannelEnabled(true);
		ice_session->SetDataChannelNumber(channel_number_attribute->GetChannelNumber());
	}

	return true;
}

bool IcePort::OnReceivedTurnRefreshRequest(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, GateInfo &gate_info, const StunMessage &message)
{
	StunMessage response_message;

	// Add lifetime
	uint32_t lifetime = DEFAULT_LIFETIME;

	auto requested_lifetime_attr = message.GetAttribute<StunLifetimeAttribute>(StunAttributeType::Lifetime);
	if (requested_lifetime_attr != nullptr)
	{
		lifetime = std::min(static_cast<uint32_t>(DEFAULT_LIFETIME), requested_lifetime_attr->GetValue());
	}

	auto lifetime_attribute = std::make_shared<StunLifetimeAttribute>();
	lifetime_attribute->SetValue(lifetime);
	response_message.AddAttribute(lifetime_attribute);

	response_message.SetHeader(StunClass::SuccessResponse, StunMethod::Refresh, message.GetTransactionId());
	SendStunMessage(remote, address_pair, gate_info, response_message, _hmac_key);

	logtt("Turn Refresh Request : %s", lifetime_attribute->ToString().CStr());

	return true;
}

void IcePort::NotifyIceSessionStateChanged(std::shared_ptr<IceSession> &session)
{
	if (session->GetObserver() != nullptr)
	{
		session->GetObserver()->OnStateChanged(*this, session->GetSessionID(), session->GetState(), session->GetUserData());
	}
}

ov::String IcePort::ToString() const
{
	return ov::String::FormatString("<IcePort: %p, %zu ports>", this, _physical_port_list.size());
}
