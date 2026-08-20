//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "stream_stats.h"

#include <gtest/gtest.h>

static ov::SocketAddressPair Pair(const char *local_ip, uint16_t local_port, const char *remote_ip, uint16_t remote_port)
{
	return ov::SocketAddressPair(
		ov::SocketAddress::CreateAndGetFirst(local_ip, local_port),
		ov::SocketAddress::CreateAndGetFirst(remote_ip, remote_port));
}

// From() maps socket-level facts to the reported transport/protocol
TEST(ConnectionInfo, FromClassification)
{
	auto pair		= Pair("10.0.0.5", 10200, "1.2.3.4", 51234);

	auto direct_udp = info::ConnectionInfo::From(pair, ov::SocketType::Udp, nullptr, 1);
	EXPECT_STREQ(direct_udp->transport.CStr(), "UDP");
	EXPECT_STREQ(direct_udp->protocol.CStr(), "UDP");
	EXPECT_STREQ(direct_udp->local_address.CStr(), "10.0.0.5");
	EXPECT_EQ(direct_udp->local_port, 10200u);
	EXPECT_STREQ(direct_udp->remote_address.CStr(), "1.2.3.4");
	EXPECT_EQ(direct_udp->remote_port, 51234u);
	EXPECT_EQ(direct_udp->version, 1u);

	auto direct_tcp = info::ConnectionInfo::From(pair, ov::SocketType::Tcp);
	EXPECT_STREQ(direct_tcp->transport.CStr(), "TCP");
	EXPECT_STREQ(direct_tcp->protocol.CStr(), "TCP");

	auto turn_over_tcp = info::ConnectionInfo::From(pair, ov::SocketType::Tcp, "TURN");
	EXPECT_STREQ(turn_over_tcp->transport.CStr(), "TURN");
	EXPECT_STREQ(turn_over_tcp->protocol.CStr(), "TCP");

	EXPECT_STREQ(direct_udp->ToString().CStr(), "UDP (UDP, local: 10.0.0.5:10200, remote: 1.2.3.4:51234)");
}

// SetConnectionInfo() must keep only the newest versioned snapshot;
// unversioned snapshots always win
TEST(StreamStats, ConnectionInfoVersionGate)
{
	info::StreamStats stats;
	auto pair_a = Pair("10.0.0.5", 10200, "1.2.3.4", 50001);
	auto pair_b = Pair("10.0.0.5", 13478, "1.2.3.4", 50002);

	EXPECT_EQ(stats.GetConnectionInfo(), nullptr);

	auto v1 = info::ConnectionInfo::From(pair_a, ov::SocketType::Udp, nullptr, 1);
	auto v2 = info::ConnectionInfo::From(pair_b, ov::SocketType::Tcp, "TURN", 2);

	EXPECT_TRUE(stats.SetConnectionInfo(v1));
	EXPECT_TRUE(stats.SetConnectionInfo(v2));
	EXPECT_EQ(stats.GetConnectionInfo(), v2);

	// The straggler publication of the older selection is dropped
	EXPECT_FALSE(stats.SetConnectionInfo(v1));
	EXPECT_EQ(stats.GetConnectionInfo(), v2);

	// A duplicate publication of the same selection is dropped too
	auto v2_dup = info::ConnectionInfo::From(pair_b, ov::SocketType::Tcp, "TURN", 2);
	EXPECT_FALSE(stats.SetConnectionInfo(v2_dup));
	EXPECT_EQ(stats.GetConnectionInfo(), v2);

	// An unversioned snapshot is always accepted (a provider without racing publishers)
	auto unversioned = info::ConnectionInfo::From(pair_a, ov::SocketType::Tcp);
	EXPECT_TRUE(stats.SetConnectionInfo(unversioned));
	EXPECT_EQ(stats.GetConnectionInfo(), unversioned);
}
