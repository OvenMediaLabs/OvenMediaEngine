//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "ice_session.h"
#include "ice_port_observer.h"
#include "ice_tcp_demultiplexer.h"
#include "modules/ice/stun/stun_message.h"

#include <vector>
#include <memory>

#include <base/ovsocket/ovsocket.h>
#include <config/config.h>
#include <modules/physical_port/physical_port_manager.h>
#include <modules/rtp_rtcp/rtcp_packet.h>
#include <modules/rtp_rtcp/rtp_packet.h>

#define DEFAULT_RELAY_REALM		"airensoft"
#define DEFAULT_RELAY_USERNAME	"ome"
#define DEFAULT_RELAY_KEY		"airen"
#define DEFAULT_LIFETIME		3600
// This is the player's candidate and eventually passed to OME. 
// However, OME does not use the player's candidate. So we pass anything by this value.
#define FAKE_RELAY_IP4 "1.1.1.1"
#define FAKE_RELAY_IP6 "1::1"
#define FAKE_RELAY_PORT 14090

#define OV_ICE_PORT_PUBLIC_IP "${PublicIP}"

class RtcIceCandidate;

class IcePort : protected PhysicalPortObserver
{
public:
	IcePort();
	~IcePort() override;

	bool CreateTurnServer(const ov::SocketAddress &address, ov::SocketType socket_type, int tcp_relay_worker_count);
	bool CreateIceCandidates(const char *server_name, const cfg::Server &server_config, const RtcIceCandidateList &ice_candidate_list, int ice_worker_count);
	bool Close();

	ov::String GenerateUfrag();

	// Issue unique session id
	session_id_t IssueUniqueSessionId();
	void AddSession(const std::shared_ptr<IcePortObserver> &observer, session_id_t session_id, IceSession::Role role,
					const std::shared_ptr<const SessionDescription> &offer_sdp, const std::shared_ptr<const SessionDescription> &peer_sdp, 
					int stun_timeout_ms,  uint64_t life_time_epoch_ms, std::any user_data);
	bool RemoveSession(session_id_t session_id);
	bool DisconnectSession(session_id_t session_id);

	bool Send(session_id_t session_id, const std::shared_ptr<RtpPacket> &packet);
	bool Send(session_id_t session_id, const std::shared_ptr<RtcpPacket> &packet);
	bool Send(session_id_t session_id, const std::shared_ptr<const ov::Data> &data);

	ov::String ToString() const;

protected:
	std::shared_ptr<PhysicalPort> CreatePhysicalPort(const ov::SocketAddress &address, ov::SocketType type, int ice_worker_count);

	bool ParseIceCandidate(const ov::String &ice_candidate, std::vector<ov::String> *ip_list, ov::SocketType *socket_type, int *start_port, int *end_port);

	//--------------------------------------------------------------------
	// Implementation of PhysicalPortObserver
	//--------------------------------------------------------------------
	void OnConnected(const std::shared_ptr<ov::Socket> &remote) override;
	void OnDataReceived(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddress &address, const std::shared_ptr<const ov::Data> &data) override;
	void OnDatagramReceived(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, const std::shared_ptr<const ov::Data> &data) override;
	void OnDisconnected(const std::shared_ptr<ov::Socket> &remote, PhysicalPortDisconnectReason reason, const std::shared_ptr<const ov::Error> &error) override;
	//--------------------------------------------------------------------

	void NotifyIceSessionStateChanged(std::shared_ptr<IceSession> &info);

private:
	protected:
	struct GateInfo
	{
		enum class GateType
		{
			DIRECT,
			SEND_INDICATION,
			DATA_CHANNEL
		};

		IcePacketIdentifier::PacketType packet_type;
		GateType input_method = GateType::DIRECT;
		// If this packet cames from a send
		ov::SocketAddress peer_address;
		// If this packet is from a turn data channel, store the channel number.
		uint16_t channel_number = 0;

		ov::String GetGateTypeString()
		{
			switch (input_method)
			{
				case GateType::DIRECT:
					return "DIRECT";
				case GateType::SEND_INDICATION:
					return "SEND_INDICATION";
				case GateType::DATA_CHANNEL:
					return "DATA_CHANNEL";
				default:
					return "UNKNOWN";
			}
		}

		ov::String ToString()
		{
			return ov::String::FormatString("Packet type : %s GateType : %s", IcePacketIdentifier::GetPacketTypeString(packet_type).CStr(), GetGateTypeString().CStr());
		}
	};

	struct BindingRequestInfo
	{
		BindingRequestInfo(ov::String transaction_id, const std::shared_ptr<IceSession> &ice_session)
		{
			_transaction_id = transaction_id;
			_ice_session = ice_session;
			_requested_time = std::chrono::system_clock::now();
		}

		bool IsExpired() const
		{
			if (ov::Clock::GetElapsedMiliSecondsFromNow(_requested_time) > 3000)
			{
				return true;
			}

			return false;
		}

		ov::String _transaction_id;
		std::shared_ptr<IceSession> _ice_session;
		std::chrono::time_point<std::chrono::system_clock>	_requested_time;
	};

	std::atomic<session_id_t> _session_id_counter;

	// Add IceSession
	bool AddIceSession(session_id_t session_id, const std::shared_ptr<IceSession> &ice_session);
	bool AddIceSession(const ov::String &local_ufrag, const std::shared_ptr<IceSession> &ice_session);
	bool AddIceSession(const ov::SocketAddressPair &pair, const std::shared_ptr<IceSession> &ice_session);

	// Get IceSession
	std::shared_ptr<IceSession> FindIceSession(session_id_t session_id);
	std::shared_ptr<IceSession> FindIceSession(const ov::String &local_ufrag);
	std::shared_ptr<IceSession> FindIceSession(const ov::SocketAddressPair &pair);

	bool StoreIceSessionWithTransactionId(const std::shared_ptr<IceSession> &ice_session, const ov::String &transaction_id);
	std::shared_ptr<IceSession> FindIceSessionWithTransactionId(const ov::String &transaction_id);
	bool RemoveTransaction(const ov::String &transaction_id);

	void CheckTimedOut();

	void OnPacketReceived(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair,
						  GateInfo &packet_info, const std::shared_ptr<const ov::Data> &data);
	void OnStunPacketReceived(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair,
							  GateInfo &packet_info, const std::shared_ptr<const ov::Data> &data);
	void OnChannelDataPacketReceived(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair,
									 GateInfo &packet_info, const std::shared_ptr<const ov::Data> &data);
	void OnApplicationPacketReceived(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair,
									 GateInfo &packet_info, const std::shared_ptr<const ov::Data> &data);

	bool SendStunMessage(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, GateInfo &packet_info, StunMessage &message, const std::shared_ptr<const ov::Data> &integrity_key = nullptr);
	bool SendStunBindingRequest(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, GateInfo &packet_info, const std::shared_ptr<IceSession> &info);

	const std::shared_ptr<const ov::Data> CreateDataIndication(const ov::SocketAddress &peer_address, const std::shared_ptr<const ov::Data> &data);
	const std::shared_ptr<const ov::Data> CreateChannelDataMessage(uint16_t channel_number, const std::shared_ptr<const ov::Data> &data);

	// STUN negotiation order:
	// (State: New)
	// [Server] <-- 1. Binding Request          --- [Player]
	// (State: Checking)
	// [Server] --- 2. Binding Success Response --> [Player]
	// [Server] --- 3. Binding Request          --> [Player]
	// [Server] <-- 4. Binding Success Response --- [Player]
	// (State: Connected)
	bool OnReceivedStunBindingRequest(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, GateInfo &packet_info, const StunMessage &message);
	bool OnReceivedStunBindingResponse(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, GateInfo &packet_info, const StunMessage &message);
	bool OnReceivedTurnAllocateRequest(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, GateInfo &packet_info, const StunMessage &message);
	bool OnReceivedTurnRefreshRequest(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, GateInfo &packet_info, const StunMessage &message);
	bool OnReceivedTurnSendIndication(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, GateInfo &packet_info, const StunMessage &message);
	bool OnReceivedTurnCreatePermissionRequest(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, GateInfo &packet_info, const StunMessage &message);
	bool OnReceivedTurnChannelBindRequest(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, GateInfo &packet_info, const StunMessage &message);

	bool UseCandidate(const std::shared_ptr<IceSession> &ice_session, const ov::SocketAddressPair &address_pair);

	// Related TURN
	std::shared_ptr<ov::Data> _hmac_key;
	// Creating an attribute to be used in advance
	std::shared_ptr<StunAttribute> _realm_attribute;
	std::shared_ptr<StunAttribute> _software_attribute;
	std::shared_ptr<StunAttribute> _nonce_attribute;

	std::shared_ptr<StunAttribute> _xor_relayed_address_attribute_for_ipv4;
	std::shared_ptr<StunAttribute> _xor_relayed_address_attribute_for_ipv6;

	std::vector<std::shared_ptr<PhysicalPort>> _physical_port_list;
	std::recursive_mutex _physical_port_list_mutex;

	// Mapping table containing related information until STUN binding.
	// Once binding is complete, there is no need because it can be found by destination ip & port.
	// key: offer ufrag
	std::shared_mutex _ice_sessions_with_ufrag_lock;
	std::map<const ov::String, std::shared_ptr<IceSession>> _ice_sessions_with_ufrag;
	
	// Find IceSession with connected CandidatePair, used when receiving TURN channel data and application data
	// key: SocketAddressPair
	std::shared_mutex _ice_sessions_with_address_pair_lock;
	std::map<ov::SocketAddressPair, std::shared_ptr<IceSession>> _ice_sessions_with_address_pair;
	
	// Find IceSession with peer's session id, used for sending application data 
	std::shared_mutex _ice_sessions_with_id_lock;
	std::map<session_id_t, std::shared_ptr<IceSession>> _ice_seesions_with_id;

	// Insert item when send stun binding request
	// Remove item when receive stun binding response or timed out
	// Request Transaction ID : Session
	std::shared_mutex _binding_requests_with_transaction_id_lock;
	std::map<ov::String, BindingRequestInfo> _binding_requests_with_transaction_id;
	
	// Demultiplexer for data input through TCP
	// remote's ID : Demultiplexer
	std::shared_mutex _demultiplexers_lock;
	std::map<int, std::shared_ptr<IceTcpDemultiplexer>> _demultiplexers;

	ov::DelayQueue _timer{"ICETmout"};
};
