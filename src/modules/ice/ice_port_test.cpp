//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  Covers: IcePort session registry (id / ufrag / address-pair maps) and
//  RemoveSession() cleanup, in particular that RemoveSession() erases EVERY
//  address pair registered for a session (not just the active one).
//
//==============================================================================
#include "ice_port.h"

#include <base/ovsocket/ovsocket.h>
#include <gtest/gtest.h>
#include <modules/sdp/session_description.h>

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
		return p.AddIceSession(u, s);
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
