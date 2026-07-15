//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  tests/base/ovsocket/test_socket_address.cpp
//  Covers: ov::SocketAddress (Create, port, IP, family, operators)
//
//==============================================================================
#include <gtest/gtest.h>

#include <base/ovsocket/ovsocket.h>

// ---------------------------------------------------------------------------
// Create from "host:port" string
// ---------------------------------------------------------------------------

TEST(SocketAddress, CreateIPv4WithPort)
{
	auto addrs = ov::SocketAddress::Create("127.0.0.1:8080");
	ASSERT_FALSE(addrs.empty());
	auto &addr = addrs.front();
	EXPECT_TRUE(addr.IsValid());
	EXPECT_TRUE(addr.IsIPv4());
	EXPECT_EQ(addr.Port(), 8080u);
}

TEST(SocketAddress, CreateIPv4WildcardPort)
{
	auto addrs = ov::SocketAddress::Create("*:1935");
	ASSERT_FALSE(addrs.empty());
	// wildcard resolves to 0.0.0.0 and/or :: 
	for (auto &addr : addrs)
	{
		EXPECT_TRUE(addr.IsValid());
		EXPECT_EQ(addr.Port(), 1935u);
	}
}

TEST(SocketAddress, CreateIPv6WithPort)
{
	auto addrs = ov::SocketAddress::Create("[::1]:9090");
	ASSERT_FALSE(addrs.empty());
	for (auto &addr : addrs)
	{
		if (addr.IsIPv6())
		{
			EXPECT_EQ(addr.Port(), 9090u);
		}
	}
}

TEST(SocketAddress, CreateLoopbackIPv4)
{
	auto addrs = ov::SocketAddress::Create("127.0.0.1:0");
	ASSERT_FALSE(addrs.empty());
	EXPECT_STREQ(addrs.front().GetIpAddress().CStr(), "127.0.0.1");
}

// ---------------------------------------------------------------------------
// ParsePort
// ---------------------------------------------------------------------------

TEST(SocketAddress, ParseSinglePort)
{
	auto ranges = ov::SocketAddress::ParsePort("1234");
	ASSERT_EQ(ranges.size(), 1u);
	EXPECT_EQ(ranges[0].start_port, 1234u);
	EXPECT_EQ(ranges[0].end_port, 1234u);
}

TEST(SocketAddress, ParsePortRange)
{
	auto ranges = ov::SocketAddress::ParsePort("1000-1002");
	ASSERT_EQ(ranges.size(), 1u);
	EXPECT_EQ(ranges[0].start_port, 1000u);
	EXPECT_EQ(ranges[0].end_port, 1002u);
}

TEST(SocketAddress, ParsePortMultipleRanges)
{
	auto ranges = ov::SocketAddress::ParsePort("1000-1001,1003");
	ASSERT_EQ(ranges.size(), 2u);
	EXPECT_EQ(ranges[0].start_port, 1000u);
	EXPECT_EQ(ranges[0].end_port, 1001u);
	EXPECT_EQ(ranges[1].start_port, 1003u);
	EXPECT_EQ(ranges[1].end_port, 1003u);
}

// ---------------------------------------------------------------------------
// Port setter
// ---------------------------------------------------------------------------

TEST(SocketAddress, SetPort)
{
	auto addrs = ov::SocketAddress::Create("127.0.0.1:1234");
	ASSERT_FALSE(addrs.empty());
	auto addr = addrs.front();
	EXPECT_TRUE(addr.SetPort(5678));
	EXPECT_EQ(addr.Port(), 5678u);
}

// ---------------------------------------------------------------------------
// Default (invalid) address
// ---------------------------------------------------------------------------

TEST(SocketAddress, DefaultConstructedIsInvalid)
{
	ov::SocketAddress addr;
	EXPECT_FALSE(addr.IsValid());
}

// ---------------------------------------------------------------------------
// Copy / move
// ---------------------------------------------------------------------------

TEST(SocketAddress, CopyConstructor)
{
	auto addrs = ov::SocketAddress::Create("127.0.0.1:8080");
	ASSERT_FALSE(addrs.empty());
	ov::SocketAddress copy(addrs.front());
	EXPECT_EQ(copy.Port(), addrs.front().Port());
	EXPECT_TRUE(copy.IsValid());
}

TEST(SocketAddress, MoveConstructor)
{
	auto addrs = ov::SocketAddress::Create("127.0.0.1:8080");
	ASSERT_FALSE(addrs.empty());
	ov::SocketAddress moved(std::move(addrs.front()));
	EXPECT_TRUE(moved.IsValid());
	EXPECT_EQ(moved.Port(), 8080u);
}

// ---------------------------------------------------------------------------
// Equality operators
// ---------------------------------------------------------------------------

TEST(SocketAddress, EqualityOperator)
{
	auto a1 = ov::SocketAddress::Create("127.0.0.1:8080");
	auto a2 = ov::SocketAddress::Create("127.0.0.1:8080");
	ASSERT_FALSE(a1.empty());
	ASSERT_FALSE(a2.empty());
	EXPECT_TRUE(a1.front() == a2.front());
}

TEST(SocketAddress, InequalityOperator)
{
	auto a1 = ov::SocketAddress::Create("127.0.0.1:8080");
	auto a2 = ov::SocketAddress::Create("127.0.0.1:9090");
	ASSERT_FALSE(a1.empty());
	ASSERT_FALSE(a2.empty());
	EXPECT_TRUE(a1.front() != a2.front());
}

// ---------------------------------------------------------------------------
// ToString
// ---------------------------------------------------------------------------

TEST(SocketAddress, ToStringContainsIP)
{
	auto addrs = ov::SocketAddress::Create("192.168.1.1:1234");
	ASSERT_FALSE(addrs.empty());
	auto str = addrs.front().ToString();
	EXPECT_NE(ov::String(str).IndexOf("192.168.1.1"), -1);
}
