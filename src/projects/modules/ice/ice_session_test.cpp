//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  Covers: IceSession nomination / active-pair selection
//  (MarkNominated, SelectActiveCandidatePair, IsActive)
//
//==============================================================================
#include <gtest/gtest.h>

#include <base/ovsocket/ovsocket.h>
#include <modules/sdp/session_description.h>

#include "ice_session.h"

namespace
{
	ov::SocketAddressPair MakePair(uint16_t local_port, uint16_t remote_port)
	{
		auto local	= ov::SocketAddress::CreateAndGetFirst("127.0.0.1", local_port);
		auto remote = ov::SocketAddress::CreateAndGetFirst("127.0.0.1", remote_port);
		return ov::SocketAddressPair(local, remote);
	}

	std::shared_ptr<IceSession> MakeSession()
	{
		// A real (minimal) SDP so any code path that touches the SDP
		// (e.g. GetLocalUfrag()) stays safe, instead of a null that only
		// works as long as the tested paths never dereference it.
		auto sdp = std::make_shared<SessionDescription>(SessionDescription::SdpType::Offer);
		sdp->SetIceUfrag("ufragT");
		return std::make_shared<IceSession>(
			1, IceSession::Role::CONTROLLED,
			sdp, sdp,
			30000, 0,
			std::any{}, nullptr);
	}

	// Make the pair known to the session (creates the IceCandidatePair in
	// Checking state, the same way an inbound STUN binding request would).
	void Validate(const std::shared_ptr<IceSession> &session, const ov::SocketAddressPair &pair)
	{
		session->OnReceivedStunBindingRequest(pair, nullptr);
	}
}  // namespace

// MarkNominated only works on a STUN-validated pair, is idempotent, and never
// touches the active pair or the session state.
TEST(IceSessionNomination, MarkNominated)
{
	auto session = MakeSession();
	auto a		 = MakePair(10000, 20000);

	// Not validated yet
	EXPECT_FALSE(session->MarkNominated(a));

	Validate(session, a);

	EXPECT_TRUE(session->MarkNominated(a));	  // newly nominated
	EXPECT_FALSE(session->MarkNominated(a));	  // idempotent

	// Nomination must not pick the active pair nor flip session state
	EXPECT_EQ(session->GetActiveCandidatePair(), nullptr);
	EXPECT_NE(session->GetState(), IceConnectionState::Connected);

	// A pair whose STUN check failed must not be resurrected by nomination
	auto f = MakePair(10001, 20001);
	Validate(session, f);
	session->OnReceivedStunBindingErrorResponse(f, nullptr);
	EXPECT_FALSE(session->MarkNominated(f));
}

// SelectActiveCandidatePair accepts any STUN-validated pair (no explicit
// nomination needed) and sets the active pair + session state together.
TEST(IceSessionNomination, SelectRequiresValidated)
{
	auto session = MakeSession();
	auto a		 = MakePair(10000, 20000);
	auto b		 = MakePair(10000, 30000);

	// Unknown (never STUN-validated) pair cannot be selected
	EXPECT_FALSE(session->SelectActiveCandidatePair(a));
	EXPECT_EQ(session->GetActiveCandidatePair(), nullptr);
	EXPECT_NE(session->GetState(), IceConnectionState::Connected);

	// A STUN-validated pair is selectable without explicit nomination
	Validate(session, a);
	ASSERT_TRUE(session->SelectActiveCandidatePair(a));

	// Invariant: state == Connected  =>  active pair is set
	EXPECT_EQ(session->GetState(), IceConnectionState::Connected);
	ASSERT_NE(session->GetActiveCandidatePair(), nullptr);
	EXPECT_EQ(session->GetActiveCandidatePair()->GetAddressPair(), a);
	EXPECT_TRUE(session->IsActive(a));
	EXPECT_FALSE(session->IsActive(b));

	// Re-select the same pair is a no-op success
	EXPECT_TRUE(session->SelectActiveCandidatePair(a));
	EXPECT_EQ(session->GetActiveCandidatePair()->GetAddressPair(), a);
}

// The active pair follows the client: when application data arrives on a
// different validated pair, OME switches to it (both directions, repeatable).
TEST(IceSessionNomination, FollowClientSwitch)
{
	auto session = MakeSession();
	auto a		 = MakePair(10000, 20000);
	auto b		 = MakePair(10001, 30000);

	Validate(session, a);
	Validate(session, b);

	ASSERT_TRUE(session->SelectActiveCandidatePair(a));
	EXPECT_EQ(session->GetActiveCandidatePair()->GetAddressPair(), a);

	// Client moved to b
	ASSERT_TRUE(session->SelectActiveCandidatePair(b));
	EXPECT_EQ(session->GetActiveCandidatePair()->GetAddressPair(), b);
	EXPECT_TRUE(session->IsActive(b));
	EXPECT_FALSE(session->IsActive(a));

	// Client moved back to a
	ASSERT_TRUE(session->SelectActiveCandidatePair(a));
	EXPECT_EQ(session->GetActiveCandidatePair()->GetAddressPair(), a);
}

// A fresh session is not Connected and has no active pair until the first
// successful selection.
TEST(IceSessionNomination, InvariantBeforeSelection)
{
	auto session = MakeSession();
	auto a		 = MakePair(10000, 20000);

	EXPECT_NE(session->GetState(), IceConnectionState::Connected);
	EXPECT_EQ(session->GetActiveCandidatePair(), nullptr);

	Validate(session, a);
	ASSERT_TRUE(session->SelectActiveCandidatePair(a));

	EXPECT_EQ(session->GetState(), IceConnectionState::Connected);
	EXPECT_NE(session->GetActiveCandidatePair(), nullptr);
}

// While a session is active on one pair, an unknown (never STUN-validated)
// pair or a pair that has Failed its STUN check must NOT hijack the session:
// the active pair stays put.
TEST(IceSessionNomination, RejectFailedOrUnknownWhileActive)
{
	auto session = MakeSession();
	auto a		 = MakePair(10000, 20000);	 // becomes active
	auto b		 = MakePair(10001, 20001);	 // validated, then Failed
	auto u		 = MakePair(10002, 20002);	 // never validated

	Validate(session, a);
	Validate(session, b);
	ASSERT_TRUE(session->SelectActiveCandidatePair(a));
	ASSERT_TRUE(session->IsActive(a));

	// Unknown pair cannot hijack the active session
	EXPECT_FALSE(session->SelectActiveCandidatePair(u));
	EXPECT_TRUE(session->IsActive(a));

	// A pair whose STUN check Failed cannot become active
	session->OnReceivedStunBindingErrorResponse(b, nullptr);
	EXPECT_FALSE(session->SelectActiveCandidatePair(b));
	EXPECT_TRUE(session->IsActive(a));
	EXPECT_EQ(session->GetState(), IceConnectionState::Connected);
}
