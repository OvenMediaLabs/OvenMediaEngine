//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  Covers: IcePort session registry (id / ufrag / address-pair maps) and
//  RemoveSession() cleanup, in particular that RemoveSession() erases EVERY
//  address pair registered for a session (not just the active one), and
//  ufrags reserved for offers, which answer binding requests arriving before the answer
//
//==============================================================================
#include "ice_port.h"

#include <arpa/inet.h>
#include <base/ovsocket/ovsocket.h>
#include <gtest/gtest.h>
#include <modules/ice/stun/attributes/stun_attributes.h>
#include <modules/ice/stun/stun_message.h>
#include <modules/sdp/session_description.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

#include "ice_candidate_pair.h"
#include "ice_session.h"

// Fixture is friended by IcePort, so its (protected) helpers may touch the
// private session registry. TEST_F bodies call the helpers.
class IcePortTest : public ::testing::Test
{
protected:
	static ov::SocketAddressPair Pair(uint16_t local_port, uint16_t remote_port)
	{
		return ov::SocketAddressPair(
			ov::SocketAddress::CreateAndGetFirst("127.0.0.1", local_port),
			ov::SocketAddress::CreateAndGetFirst("127.0.0.1", remote_port));
	}

	// RemoveSession() needs a non-null local SDP (GetLocalUfrag()).
	static std::shared_ptr<IceSession> MakeSession(session_id_t id, const ov::String &ufrag)
	{
		auto sdp = std::make_shared<SessionDescription>(SessionDescription::SdpType::Offer);
		sdp->SetIceUfrag(ufrag);
		return std::make_shared<IceSession>(
			id, IceSession::Role::CONTROLLED, sdp, sdp, 600000, 0, std::any{}, nullptr);
	}

	bool AddId(IcePort &p, session_id_t id, const std::shared_ptr<IceSession> &s)
	{
		return p.AddIceSession(id, s);
	}
	bool AddUfrag(IcePort &p, const ov::String &u, const std::shared_ptr<IceSession> &s)
	{
		std::map<ov::SocketAddressPair, IcePort::DeferredBindingRequest> deferred_binding_requests;
		return p.AddIceSession(u, s, &deferred_binding_requests);
	}

	// Registers the session by ufrag and returns how many deferred binding requests it took over, -1 on failure
	int AddUfragTakingOver(IcePort &p, const ov::String &u, const std::shared_ptr<IceSession> &s)
	{
		std::map<ov::SocketAddressPair, IcePort::DeferredBindingRequest> deferred_binding_requests;
		if (p.AddIceSession(u, s, &deferred_binding_requests) == false)
		{
			return -1;
		}
		return static_cast<int>(deferred_binding_requests.size());
	}

	std::shared_ptr<IceSession> FindOrReserved(IcePort &p, const ov::String &u, std::optional<ov::String> *reserved_pwd)
	{
		return p.FindIceSessionOrReservedUfrag(u, reserved_pwd);
	}

	// True if the request was deferred
	bool Defer(IcePort &p, const ov::String &u, const ov::SocketAddressPair &ap, const ov::String &peer_ufrag)
	{
		return p.DeferBindingRequest(u, ap, IcePort::DeferredBindingRequest{nullptr, IcePort::GateInfo{}, peer_ufrag}) == IcePort::DeferResult::Deferred;
	}

	size_t ReservedUfragCount(IcePort &p)
	{
		return p._reserved_ufrags.size();
	}
	bool AddPair(IcePort &p, const ov::SocketAddressPair &ap, const std::shared_ptr<IceSession> &s)
	{
		return p.AddIceSession(ap, s);
	}

	std::shared_ptr<IceSession> FindId(IcePort &p, session_id_t id)
	{
		return p.FindIceSession(id);
	}
	std::shared_ptr<IceSession> FindUfrag(IcePort &p, const ov::String &u)
	{
		return p.FindIceSession(u);
	}
	std::shared_ptr<IceSession> FindPair(IcePort &p, const ov::SocketAddressPair &ap)
	{
		return p.FindIceSession(ap);
	}

	bool MarkNom(IcePort &p, const std::shared_ptr<IceSession> &s, const ov::SocketAddressPair &ap)
	{
		return p.MarkNominated(nullptr, s, ap);
	}

	// Stop the background sweeper so these registry tests are deterministic and
	// free of a data race between CheckTimedOut() and the helpers below. The
	// idempotent ~IcePort() Stop() afterwards is a harmless no-op.
	void StopSweeper(IcePort &p)
	{
		p._timer.Stop();
	}

	static std::shared_ptr<IceSession> MakeObservedSession(session_id_t id, const ov::String &ufrag, const std::shared_ptr<IcePortObserver> &observer)
	{
		auto sdp = std::make_shared<SessionDescription>(SessionDescription::SdpType::Offer);
		sdp->SetIceUfrag(ufrag);
		return std::make_shared<IceSession>(
			id, IceSession::Role::CONTROLLED, sdp, sdp, 600000, 0, std::any{}, observer);
	}

	// Feed an application packet into the (private) demux entry point
	void InjectAppPacket(IcePort &p, const ov::SocketAddressPair &ap)
	{
		IcePort::GateInfo gate_info;
		gate_info.packet_type = IcePacketIdentifier::PacketType::RTP_RTCP;
		p.OnApplicationPacketReceived(nullptr, ap, gate_info, std::make_shared<ov::Data>());
	}

	// Feed a STUN packet into the (private) entry point as if it arrived on remote
	void InjectStunPacket(IcePort &p, const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &ap, const std::shared_ptr<const ov::Data> &data)
	{
		IcePort::GateInfo gate_info;
		gate_info.packet_type = IcePacketIdentifier::PacketType::STUN;
		p.OnStunPacketReceived(remote, ap, gate_info, data);
	}

	// A binding request from the peer, signed with OME's password
	static std::shared_ptr<const ov::Data> MakeBindingRequest(const ov::String &user_name, const ov::String &pwd, const uint8_t *transaction_id)
	{
		StunMessage message;
		message.SetHeader(StunClass::Request, StunMethod::Binding, transaction_id);

		auto user_name_attr = std::make_shared<StunUserNameAttribute>();
		user_name_attr->SetText(user_name);
		message.AddAttribute(user_name_attr);

		return message.Serialize(pwd.ToData(false));
	}
};

// A loopback ov::Socket that plays OME's UDP ICE port
class IceUdpSocket
{
public:
	~IceUdpSocket()
	{
		if (_socket != nullptr)
		{
			_socket->CloseIfNeeded();
		}

		if (_pool != nullptr)
		{
			_pool->Uninitialize();
		}
	}

	bool Open()
	{
		_pool = ov::SocketPool::Create("ice-port-test", ov::SocketType::Udp, false);
		if (_pool == nullptr)
		{
			return false;
		}

		if (_pool->Initialize(1) == false)
		{
			return false;
		}

		_socket = _pool->AllocSocket(ov::SocketFamily::Inet);
		if (_socket == nullptr)
		{
			return false;
		}

		if (_socket->MakeBlocking() == false)
		{
			return false;
		}

		if (_socket->Bind(ov::SocketAddress::CreateAndGetFirst("127.0.0.1", 0)) == false)
		{
			return false;
		}

		sockaddr_in address{};
		socklen_t length = sizeof(address);
		if (::getsockname(_socket->GetSocket().GetNativeHandle(), reinterpret_cast<sockaddr *>(&address), &length) != 0)
		{
			return false;
		}

		_port = ntohs(address.sin_port);

		return true;
	}

	const std::shared_ptr<ov::Socket> &GetSocket() const
	{
		return _socket;
	}

	uint16_t Port() const
	{
		return _port;
	}

private:
	std::shared_ptr<ov::SocketPool> _pool;
	std::shared_ptr<ov::Socket> _socket;
	uint16_t _port = 0;
};

// A loopback UDP socket that plays the peer
class UdpPeer
{
public:
	~UdpPeer()
	{
		if (_fd >= 0)
		{
			::close(_fd);
		}
	}

	bool Open()
	{
		_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
		if (_fd < 0)
		{
			return false;
		}

		sockaddr_in address{};
		address.sin_family		= AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port		= 0;

		if (::bind(_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0)
		{
			return false;
		}

		timeval timeout = {3, 0};
		if (::setsockopt(_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0)
		{
			return false;
		}

		socklen_t length = sizeof(address);
		if (::getsockname(_fd, reinterpret_cast<sockaddr *>(&address), &length) != 0)
		{
			return false;
		}

		_port = ntohs(address.sin_port);

		return true;
	}

	uint16_t Port() const
	{
		return _port;
	}

	// Receives one datagram as a STUN message, nullptr on timeout
	std::shared_ptr<StunMessage> ReceiveStun()
	{
		uint8_t buffer[2048];
		auto length = ::recv(_fd, buffer, sizeof(buffer), 0);
		if (length <= 0)
		{
			return nullptr;
		}

		auto data = std::make_shared<ov::Data>(buffer, length);
		ov::ByteStream stream(data.get());

		auto message = std::make_shared<StunMessage>();
		if (message->Parse(stream) == false)
		{
			return nullptr;
		}

		return message;
	}

private:
	int _fd = -1;
	uint16_t _port = 0;
};

// Records every OnIceCandidatePairSelected() delivery for assertions
class PairRecordingObserver : public IcePortObserver
{
public:
	void OnDataReceived(IcePort &port, uint32_t session_id, std::shared_ptr<const ov::Data> data, std::any user_data) override
	{
	}

	void OnIceCandidatePairSelected(IcePort &port, uint32_t session_id, const std::shared_ptr<const IceCandidatePair> &candidate_pair, uint64_t selected_version, std::any user_data) override
	{
		last_session_id = session_id;
		selected_pairs.push_back(candidate_pair);
		selected_versions.push_back(selected_version);
	}

	uint32_t last_session_id = 0;
	std::vector<std::shared_ptr<const IceCandidatePair>> selected_pairs;
	std::vector<uint64_t> selected_versions;
};

// Add / Find across the three indices, including idempotent inserts.
TEST_F(IcePortTest, RegistryAddFind)
{
	IcePort port;
	StopSweeper(port);
	auto s = MakeSession(1, "ufragA");

	EXPECT_TRUE(AddId(port, 1, s));
	EXPECT_TRUE(AddUfrag(port, "ufragA", s));
	EXPECT_TRUE(AddPair(port, Pair(10000, 20000), s));

	EXPECT_EQ(FindId(port, 1), s);
	EXPECT_EQ(FindUfrag(port, "ufragA"), s);
	EXPECT_EQ(FindPair(port, Pair(10000, 20000)), s);

	// Unknown lookups
	EXPECT_EQ(FindId(port, 999), nullptr);
	EXPECT_EQ(FindUfrag(port, "nope"), nullptr);
	EXPECT_EQ(FindPair(port, Pair(10000, 59999)), nullptr);

	// Duplicate inserts are rejected / idempotent
	EXPECT_FALSE(AddId(port, 1, s));
	EXPECT_FALSE(AddPair(port, Pair(10000, 20000), s));
}

// The core of the fix: RemoveSession() must erase every address pair
// registered for the session, not just the active/connected one, and must
// not touch other sessions.
TEST_F(IcePortTest, RemoveSessionErasesAllAddressPairs)
{
	IcePort port;
	StopSweeper(port);

	auto s1 = MakeSession(1, "ufrag1");
	auto a	= Pair(10000, 20000);  // e.g. direct UDP
	auto b	= Pair(10001, 20001);  // e.g. direct TCP
	auto c	= Pair(13478, 20002);  // e.g. TURN-relayed

	ASSERT_TRUE(AddId(port, 1, s1));
	ASSERT_TRUE(AddUfrag(port, "ufrag1", s1));
	ASSERT_TRUE(AddPair(port, a, s1));
	ASSERT_TRUE(AddPair(port, b, s1));
	ASSERT_TRUE(AddPair(port, c, s1));

	// A second, unrelated session that must survive intact.
	auto s2 = MakeSession(2, "ufrag2");
	auto d	= Pair(10000, 30000);
	ASSERT_TRUE(AddId(port, 2, s2));
	ASSERT_TRUE(AddUfrag(port, "ufrag2", s2));
	ASSERT_TRUE(AddPair(port, d, s2));

	EXPECT_TRUE(port.RemoveSession(1));

	// Every index entry for session 1 is gone (no leaked pair)
	EXPECT_EQ(FindId(port, 1), nullptr);
	EXPECT_EQ(FindUfrag(port, "ufrag1"), nullptr);
	EXPECT_EQ(FindPair(port, a), nullptr);
	EXPECT_EQ(FindPair(port, b), nullptr);
	EXPECT_EQ(FindPair(port, c), nullptr);

	// Session 2 untouched
	EXPECT_EQ(FindId(port, 2), s2);
	EXPECT_EQ(FindUfrag(port, "ufrag2"), s2);
	EXPECT_EQ(FindPair(port, d), s2);

	// Removing again / unknown is a no-op false
	EXPECT_FALSE(port.RemoveSession(1));
	EXPECT_FALSE(port.RemoveSession(999));
}

// DisconnectSession marks the session Disconnecting (deferred removal).
TEST_F(IcePortTest, DisconnectSessionMarksDisconnecting)
{
	IcePort port;
	StopSweeper(port);
	auto s = MakeSession(1, "ufragA");
	ASSERT_TRUE(AddId(port, 1, s));
	ASSERT_TRUE(AddUfrag(port, "ufragA", s));

	EXPECT_TRUE(port.DisconnectSession(1));
	EXPECT_EQ(s->GetState(), IceConnectionState::Disconnecting);

	EXPECT_FALSE(port.DisconnectSession(999));
}

// Nominating a pair via IcePort must register it in the address-pair index so
// an application packet on it resolves the session (the link that makes a
// TURN-relayed DTLS path reachable). Idempotent; a pair the session never
// validated is not registered.
TEST_F(IcePortTest, MarkNominatedRegistersPair)
{
	IcePort port;
	StopSweeper(port);
	auto s	   = MakeSession(1, "ufragA");
	auto known = Pair(13478, 20000);  // validated on the session
	auto other = Pair(13478, 20001);  // never validated on the session

	// The session must already know the pair (created by an inbound binding)
	s->OnReceivedStunBindingRequest(known, nullptr);

	EXPECT_EQ(FindPair(port, known), nullptr);

	EXPECT_TRUE(MarkNom(port, s, known));
	EXPECT_EQ(FindPair(port, known), s);

	// Idempotent: already nominated -> false, still registered
	EXPECT_FALSE(MarkNom(port, s, known));
	EXPECT_EQ(FindPair(port, known), s);

	// A pair the session never validated cannot be nominated/registered
	EXPECT_FALSE(MarkNom(port, s, other));
	EXPECT_EQ(FindPair(port, other), nullptr);
}

// The observer is notified once per selection/switch,
// never for a packet arriving on the already-active pair (the hot path).
TEST_F(IcePortTest, ApplicationPacketReportsSelectedPair)
{
	IcePort port;
	StopSweeper(port);

	auto observer = std::make_shared<PairRecordingObserver>();
	auto s		  = MakeObservedSession(7, "ufragA", observer);
	auto pair_a	  = Pair(13478, 20000);
	auto pair_b	  = Pair(13478, 20001);

	// Validate both pairs on the session and register them in the port's index
	s->OnReceivedStunBindingRequest(pair_a, nullptr);
	s->OnReceivedStunBindingRequest(pair_b, nullptr);
	ASSERT_TRUE(MarkNom(port, s, pair_a));
	ASSERT_TRUE(MarkNom(port, s, pair_b));

	// First application packet selects pair_a and reports it
	InjectAppPacket(port, pair_a);
	ASSERT_EQ(observer->selected_pairs.size(), 1u);
	EXPECT_EQ(observer->last_session_id, 7u);
	EXPECT_TRUE(observer->selected_pairs[0]->GetAddressPair() == pair_a);
	EXPECT_EQ(observer->selected_versions[0], 1u);

	// A packet on the already-active pair must not report again
	InjectAppPacket(port, pair_a);
	EXPECT_EQ(observer->selected_pairs.size(), 1u);

	// The peer moves to pair_b: the switch is reported with the new pair
	// and a higher selection version
	InjectAppPacket(port, pair_b);
	ASSERT_EQ(observer->selected_pairs.size(), 2u);
	EXPECT_TRUE(observer->selected_pairs[1]->GetAddressPair() == pair_b);
	EXPECT_EQ(observer->selected_versions[1], 2u);

	// A packet on an unknown pair is dropped and reports nothing
	InjectAppPacket(port, Pair(13478, 20002));
	EXPECT_EQ(observer->selected_pairs.size(), 2u);

	// A pair the port knows but the session never STUN-validated is rejected:
	// no callback, and the active pair stays pair_b
	auto unvalidated = Pair(13478, 20003);
	ASSERT_TRUE(AddPair(port, unvalidated, s));
	InjectAppPacket(port, unvalidated);
	EXPECT_EQ(observer->selected_pairs.size(), 2u);
	EXPECT_TRUE(s->GetActiveCandidatePair()->GetAddressPair() == pair_b);
}

// A ufrag is found reserved until its session takes it over with its deferred
// binding requests, and is never found as neither
TEST_F(IcePortTest, ReservedUfragIsTakenOverBySession)
{
	IcePort port;
	StopSweeper(port);

	auto ufrag = port.ReserveOfferUfrag("local-pwd");
	ASSERT_FALSE(ufrag.IsEmpty());

	std::optional<ov::String> reserved_pwd;
	EXPECT_EQ(FindOrReserved(port, ufrag, &reserved_pwd), nullptr);
	ASSERT_TRUE(reserved_pwd.has_value());
	EXPECT_STREQ(reserved_pwd->CStr(), "local-pwd");

	// The same path twice counts once
	EXPECT_TRUE(Defer(port, ufrag, Pair(10000, 20000), "peer"));
	EXPECT_TRUE(Defer(port, ufrag, Pair(10000, 20000), "peer"));
	EXPECT_TRUE(Defer(port, ufrag, Pair(10001, 20001), "peer"));

	auto s = MakeSession(1, ufrag);
	EXPECT_EQ(AddUfragTakingOver(port, ufrag, s), 2);
	EXPECT_EQ(ReservedUfragCount(port), 0u);

	// Now found as the session, no longer reserved
	reserved_pwd.reset();
	EXPECT_EQ(FindOrReserved(port, ufrag, &reserved_pwd), s);
	EXPECT_FALSE(reserved_pwd.has_value());
	EXPECT_FALSE(Defer(port, ufrag, Pair(10002, 20002), "peer"));

	// Releasing a ufrag the session took over leaves the session alone
	port.ReleaseOfferUfrag(ufrag);
	EXPECT_EQ(FindUfrag(port, ufrag), s);
}

// A ufrag whose answer never arrives is released, and releasing again is a no-op
TEST_F(IcePortTest, ReleaseOfferUfrag)
{
	IcePort port;
	StopSweeper(port);

	auto ufrag_a = port.ReserveOfferUfrag("pwd-a");
	auto ufrag_b = port.ReserveOfferUfrag("pwd-b");
	EXPECT_TRUE(ufrag_a != ufrag_b);
	EXPECT_EQ(ReservedUfragCount(port), 2u);

	port.ReleaseOfferUfrag(ufrag_a);
	port.ReleaseOfferUfrag(ufrag_a);
	EXPECT_EQ(ReservedUfragCount(port), 1u);

	std::optional<ov::String> reserved_pwd;
	EXPECT_EQ(FindOrReserved(port, ufrag_a, &reserved_pwd), nullptr);
	EXPECT_FALSE(reserved_pwd.has_value());
	EXPECT_FALSE(Defer(port, ufrag_a, Pair(10000, 20000), "peer"));

	EXPECT_EQ(FindOrReserved(port, ufrag_b, &reserved_pwd), nullptr);
	ASSERT_TRUE(reserved_pwd.has_value());
	EXPECT_STREQ(reserved_pwd->CStr(), "pwd-b");
}

// A reserved ufrag keeps at most as many paths as a session holds, and the paths it has still update
TEST_F(IcePortTest, ReservedUfragHoldsAtMostMaxPaths)
{
	IcePort port;
	StopSweeper(port);

	auto ufrag = port.ReserveOfferUfrag("local-pwd");
	for (size_t index = 0; index < IceSession::kMaxCandidatePairs; index++)
	{
		ASSERT_TRUE(Defer(port, ufrag, Pair(10000, static_cast<uint16_t>(20000 + index)), "peer"));
	}

	EXPECT_FALSE(Defer(port, ufrag, Pair(10000, 30000), "peer"));
	EXPECT_TRUE(Defer(port, ufrag, Pair(10000, 20000), "peer"));

	auto s = MakeSession(1, ufrag);
	EXPECT_EQ(AddUfragTakingOver(port, ufrag, s), static_cast<int>(IceSession::kMaxCandidatePairs));
}

// When the answer arrives, a deferred binding request whose peer ufrag differs is not sent
TEST_F(IcePortTest, AddSessionSkipsDeferredRequestOfAnotherPeer)
{
	IcePort port;
	StopSweeper(port);

	auto ufrag = port.ReserveOfferUfrag("local-pwd");
	auto path  = Pair(10000, 20000);
	ASSERT_TRUE(Defer(port, ufrag, path, "stranger"));

	auto offer_sdp = std::make_shared<SessionDescription>(SessionDescription::SdpType::Offer);
	offer_sdp->SetIceUfrag(ufrag);
	offer_sdp->SetIcePwd("local-pwd");
	auto answer_sdp = std::make_shared<SessionDescription>(SessionDescription::SdpType::Answer);
	answer_sdp->SetIceUfrag("peer");
	answer_sdp->SetIcePwd("peer-pwd");

	port.AddSession(nullptr, 1, IceSession::Role::CONTROLLING, offer_sdp, answer_sdp, 600000, 0, std::any{});

	auto s = FindUfrag(port, ufrag);
	ASSERT_NE(s, nullptr);
	EXPECT_EQ(ReservedUfragCount(port), 0u);
	EXPECT_EQ(s->FindCandidatePair(path), nullptr);
	EXPECT_EQ(FindPair(port, path), nullptr);
}

// A binding request that arrives before the answer is answered at once, and when the answer
// arrives OME sends its own binding request on that path without the peer asking again
TEST_F(IcePortTest, BindingRequestBeforeAnswer)
{
	IceUdpSocket ice_socket;
	ASSERT_TRUE(ice_socket.Open());
	UdpPeer peer;
	ASSERT_TRUE(peer.Open());

	IcePort port;
	StopSweeper(port);

	auto path  = Pair(ice_socket.Port(), peer.Port());
	auto ufrag = port.ReserveOfferUfrag("local-pwd");

	const uint8_t transaction_id[OV_STUN_TRANSACTION_ID_LENGTH] = {'b', 'e', 'f', 'o', 'r', 'e', 'a', 'n', 's', 'w', 'e', 'r'};
	InjectStunPacket(port, ice_socket.GetSocket(), path, MakeBindingRequest(ov::String::FormatString("%s:peer", ufrag.CStr()), "local-pwd", transaction_id));

	auto response = peer.ReceiveStun();
	ASSERT_NE(response, nullptr);
	EXPECT_EQ(response->GetClass(), StunClass::SuccessResponse);
	EXPECT_EQ(::memcmp(response->GetTransactionId(), transaction_id, OV_STUN_TRANSACTION_ID_LENGTH), 0);
	EXPECT_TRUE(response->CheckIntegrity("local-pwd"));

	auto mapped_address = response->GetAttribute<StunXorMappedAddressAttribute>(StunAttributeType::XorMappedAddress);
	ASSERT_NE(mapped_address, nullptr);
	EXPECT_TRUE(mapped_address->GetAddress() == path.GetRemoteAddress());

	// The answer arrives
	auto offer_sdp = std::make_shared<SessionDescription>(SessionDescription::SdpType::Offer);
	offer_sdp->SetIceUfrag(ufrag);
	offer_sdp->SetIcePwd("local-pwd");
	auto answer_sdp = std::make_shared<SessionDescription>(SessionDescription::SdpType::Answer);
	answer_sdp->SetIceUfrag("peer");
	answer_sdp->SetIcePwd("peer-pwd");

	port.AddSession(nullptr, 1, IceSession::Role::CONTROLLING, offer_sdp, answer_sdp, 600000, 0, std::any{});

	auto request = peer.ReceiveStun();
	ASSERT_NE(request, nullptr);
	EXPECT_EQ(request->GetClass(), StunClass::Request);
	EXPECT_TRUE(request->CheckIntegrity("peer-pwd"));
	EXPECT_NE(request->GetAttribute<StunIceControllingAttribute>(StunAttributeType::IceControlling), nullptr);

	// USERNAME is "<peer ufrag>:<OME ufrag>"
	ov::String to_ufrag;
	ov::String from_ufrag;
	ASSERT_TRUE(request->GetUfrags(&to_ufrag, &from_ufrag));
	EXPECT_STREQ(to_ufrag.CStr(), "peer");
	EXPECT_STREQ(from_ufrag.CStr(), ufrag.CStr());

	auto s = FindUfrag(port, ufrag);
	ASSERT_NE(s, nullptr);
	EXPECT_NE(s->FindCandidatePair(path), nullptr);
	EXPECT_EQ(FindPair(port, path), s);
}

// The reported transport label follows the pair's TURN framing:
// Direct -> nullptr (socket protocol), relayed -> "TURN"
TEST_F(IcePortTest, ReportedTransportFollowsTurnFraming)
{
	IceCandidatePair direct_pair(Pair(10200, 20000), nullptr);
	EXPECT_EQ(direct_pair.GetReportedTransport(), nullptr);

	IceCandidatePair channel_pair(Pair(13478, 20001), nullptr);
	channel_pair.SetTurnDataChannel(0x4000);
	EXPECT_STREQ(channel_pair.GetReportedTransport(), "TURN");

	IceCandidatePair indication_pair(Pair(13478, 20002), nullptr);
	indication_pair.SetTurnSendIndication(ov::SocketAddress::CreateAndGetFirst("127.0.0.1", 30000));
	EXPECT_STREQ(indication_pair.GetReportedTransport(), "TURN");
}
