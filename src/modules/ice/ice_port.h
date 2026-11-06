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
#include <optional>
#include <unordered_set>
#include <limits>

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

// Dummy candidate used in relay-only (?transport=relay) mode.
// Must be unreachable so direct ICE pairs fail, forcing the client to use its TURN relay candidate.
// IPv4 uses the CGN range (RFC 6598, 100.64.0.0/10) which is reserved for ISP-internal NAT and
// virtually never appears on end-user LANs or containers, while still being a syntactically
// valid address browsers accept. The RFC 5737 documentation ranges (192.0.2.0/24 etc.) are
// avoided because some browsers drop those during candidate gathering.
// IPv6 uses a high ULA address; ULA is rarely deployed in practice, so collision risk is low.
#define RELAY_MODE_DUMMY_IP4_CANDIDATE  "100.127.255.254"
#define RELAY_MODE_DUMMY_IP6_CANDIDATE  "fd00::ffff"
#define RELAY_MODE_DUMMY_PORT 1518

#define OV_ICE_PORT_PUBLIC_IP "${PublicIP}"

class RtcIceCandidate;

class IcePort : protected PhysicalPortObserver
{
	// Unit-test access to the private session registry (see ice_port_test.cpp)
	friend class IcePortTest;

public:
	IcePort();
	~IcePort() override;

	bool CreateTurnServer(const ov::SocketAddress &address, ov::SocketType socket_type, int tcp_relay_worker_count);
	bool CreateIceCandidates(const char *server_name, const cfg::Server &server_config, const RtcIceCandidateList &ice_candidate_list, int ice_worker_count, int tcp_ice_worker_count = 0);
	bool Close();

	ov::String GenerateUfrag();

	// Issues the ufrag of an offer OME sends, so binding requests that arrive before the answer are answered
	// AddSession() takes it over when the answer arrives
	ov::String ReserveOfferUfrag(const ov::String &local_pwd);
	// No-op if the session already took the ufrag over or it was released
	void ReleaseOfferUfrag(const ov::String &local_ufrag);

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
			_requested_time = std::chrono::steady_clock::now();
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
		std::chrono::time_point<std::chrono::steady_clock>	_requested_time;

		// What it takes to send the request again until it is answered, no data if it is not sent again
		std::shared_ptr<ov::Socket> _remote;
		ov::SocketAddressPair _address_pair;
		GateInfo _gate_info;
		std::shared_ptr<const ov::Data> _data;
		// Whether CheckTimedOut() already found it unanswered
		bool _seen_unanswered = false;
	};

	// A binding request OME sends once the answer arrives, on the path the peer's
	// request came in before the answer
	struct DeferredBindingRequest
	{
		std::shared_ptr<ov::Socket> remote;
		GateInfo gate_info;
		// Verified against the answer once it arrives
		ov::String peer_ufrag;
	};

	// A peer checks at most this many candidate pairs by default (RFC 8445 6.1.2.5),
	// so an offer keeps no more paths than this until its answer arrives
	static constexpr size_t kMaxDeferredBindingRequests = 100;

	// The ufrag of an offer OME sent, reserved until the answer arrives
	struct ReservedUfrag
	{
		ov::String local_pwd;
		// Holds at most kMaxDeferredBindingRequests
		std::map<ov::SocketAddressPair, DeferredBindingRequest> deferred_binding_requests;
		// Whether reaching the limit was logged
		bool limit_reported = false;
	};

	enum class DeferResult : uint8_t
	{
		Deferred,
		// The answer arrived in the meantime
		NotReserved,
		// The offer already holds kMaxDeferredBindingRequests paths
		Full
	};

	std::atomic<session_id_t> _session_id_counter;

	// Add IceSession
	bool AddIceSession(session_id_t session_id, const std::shared_ptr<IceSession> &ice_session);
	// Also ends the reservation of local_ufrag, adds the paths of the session's peer to the session,
	// and hands all deferred binding requests to the caller
	bool AddIceSession(const ov::String &local_ufrag, const std::shared_ptr<IceSession> &ice_session, std::map<ov::SocketAddressPair, DeferredBindingRequest> *deferred_binding_requests);
	bool AddIceSession(const ov::SocketAddressPair &pair, const std::shared_ptr<IceSession> &ice_session);

	// Get IceSession
	std::shared_ptr<IceSession> FindIceSession(session_id_t session_id);
	std::shared_ptr<IceSession> FindIceSession(const ov::String &local_ufrag);
	std::shared_ptr<IceSession> FindIceSession(const ov::SocketAddressPair &pair);
	// If there is no session for local_ufrag yet, fills the password reserved with it
	std::shared_ptr<IceSession> FindIceSessionOrReservedUfrag(const ov::String &local_ufrag, std::optional<ov::String> *reserved_pwd);

	// Caller holds _ice_sessions_with_ufrag_lock
	bool IsUfragInUse(const ov::String &ufrag) const;
	DeferResult DeferBindingRequest(const ov::String &local_ufrag, const ov::SocketAddressPair &address_pair, const DeferredBindingRequest &request);
	// Closes the TCP connection of a path dropped before it reached a session, unless an offer or a session still uses it
	void CloseUnusedTcpSocket(const std::shared_ptr<ov::Socket> &remote);

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
	// Frames a serialized STUN message for the gate it goes through and sends it
	bool SendStunData(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, const GateInfo &packet_info, const std::shared_ptr<const ov::Data> &source_data);
	bool SendStunBindingRequest(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, GateInfo &packet_info, const std::shared_ptr<IceSession> &info);
	bool SendStunBindingResponse(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, GateInfo &packet_info, const StunMessage &request, const ov::String &local_pwd);

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
	// False if the answer arrived in the meantime
	bool OnReceivedStunBindingRequestBeforeAnswer(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, GateInfo &packet_info, const StunMessage &message,
												  const ov::String &local_ufrag, const ov::String &peer_ufrag, const ov::String &local_pwd);
	bool OnReceivedStunBindingResponse(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, GateInfo &packet_info, const StunMessage &message);
	bool OnReceivedTurnAllocateRequest(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, GateInfo &packet_info, const StunMessage &message);
	bool OnReceivedTurnRefreshRequest(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, GateInfo &packet_info, const StunMessage &message);
	bool OnReceivedTurnSendIndication(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, GateInfo &packet_info, const StunMessage &message);
	bool OnReceivedTurnCreatePermissionRequest(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, GateInfo &packet_info, const StunMessage &message);
	bool OnReceivedTurnChannelBindRequest(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair, GateInfo &packet_info, const StunMessage &message);

	bool MarkNominated(const std::shared_ptr<const ov::Socket> &remote, const std::shared_ptr<IceSession> &ice_session, const ov::SocketAddressPair &address_pair);

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
	// Ufrags reserved for offers until the answer arrives. Shares the lock above so a
	// ufrag is always found, either reserved or as its session
	// key: offer ufrag
	std::map<ov::String, ReservedUfrag> _reserved_ufrags;

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

	// TCP ports that serve as direct ICE candidates (RFC 6544).
	// Connections arriving on these ports use RFC 4571 framing instead of TURN framing.
	std::unordered_set<uint16_t> _ice_tcp_candidate_ports;

	bool IsIceTcpCandidatePort(uint16_t port) const
	{
		return _ice_tcp_candidate_ports.count(port) > 0;
	}

	// Wrap |data| in a 2-byte big-endian RFC 4571 length frame.
	// Returns nullptr if |data| is null or larger than what the 2-byte length
	// prefix can represent; callers must drop the packet in that case to avoid
	// truncating the length and desynchronizing the TCP framing.
	static std::shared_ptr<ov::Data> CreateRfc4571Frame(const std::shared_ptr<const ov::Data> &data)
	{
		if (data == nullptr || data->GetLength() > std::numeric_limits<uint16_t>::max())
		{
			return nullptr;
		}

		const uint16_t length = static_cast<uint16_t>(data->GetLength());
		auto framed = std::make_shared<ov::Data>(2 + length);
		const uint8_t header[2] = {static_cast<uint8_t>(length >> 8), static_cast<uint8_t>(length & 0xFF)};
		framed->Append(header, 2);
		framed->Append(data);
		return framed;
	}

	ov::DelayQueue _timer{"ICETmout"};
};
