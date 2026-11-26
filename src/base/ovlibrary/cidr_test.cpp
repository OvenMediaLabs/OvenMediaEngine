//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <base/ovlibrary/cidr.h>
#include <base/ovlibrary/string.h>
#include <gtest/gtest.h>

namespace
{
	uint32_t ToHostOrder(const char *ip)
	{
		in_addr addr{};
		::inet_pton(AF_INET, ip, &addr);
		return ::ntohl(addr.s_addr);
	}

	ov::String ToString(uint32_t host_order)
	{
		in_addr addr{};
		addr.s_addr = ::htonl(host_order);
		char buf[INET_ADDRSTRLEN];
		::inet_ntop(AF_INET, &addr, buf, sizeof(buf));
		return buf;
	}
}  // namespace

TEST(OvCIDR, Slash24AllowsOnlyItsRange)
{
	auto cidr = ov::CIDR::Parse("192.168.1.0/24");
	ASSERT_NE(cidr, nullptr);

	EXPECT_TRUE(cidr->CheckIP("192.168.1.0"));
	EXPECT_TRUE(cidr->CheckIP("192.168.1.5"));
	EXPECT_TRUE(cidr->CheckIP("192.168.1.255"));

	EXPECT_FALSE(cidr->CheckIP("10.1.2.3"));
	EXPECT_FALSE(cidr->CheckIP("192.168.0.255"));
	EXPECT_FALSE(cidr->CheckIP("192.168.2.5"));
	EXPECT_FALSE(cidr->CheckIP("192.169.1.5"));
}

TEST(OvCIDR, Slash32AllowsOnlyTheAddress)
{
	auto cidr = ov::CIDR::Parse("192.168.100.5/32");
	ASSERT_NE(cidr, nullptr);

	EXPECT_TRUE(cidr->CheckIP("192.168.100.5"));
	EXPECT_FALSE(cidr->CheckIP("192.168.100.4"));
	EXPECT_FALSE(cidr->CheckIP("192.168.100.6"));
}

TEST(OvCIDR, EveryPrefixBoundary)
{
	const auto base = ToHostOrder("172.16.5.9");

	for (int bits = 1; bits <= 32; ++bits)
	{
		const uint32_t mask		 = 0xFFFFFFFFu << (32 - bits);
		const uint32_t network	 = base & mask;
		const uint32_t broadcast = network | ~mask;

		SCOPED_TRACE("172.16.5.9/" + std::to_string(bits));

		auto cidr = ov::CIDR::Parse(ov::String::FormatString("%s/%d", ToString(base).CStr(), bits));
		ASSERT_NE(cidr, nullptr);

		EXPECT_TRUE(cidr->CheckIP(ToString(network).CStr()));
		EXPECT_TRUE(cidr->CheckIP(ToString(broadcast).CStr()));
		EXPECT_STREQ(cidr->Begin().CStr(), ToString(network).CStr());
		EXPECT_STREQ(cidr->End().CStr(), ToString(broadcast).CStr());

		if (network != 0)
		{
			EXPECT_FALSE(cidr->CheckIP(ToString(network - 1).CStr()));
		}

		if (broadcast != 0xFFFFFFFFu)
		{
			EXPECT_FALSE(cidr->CheckIP(ToString(broadcast + 1).CStr()));
		}
	}
}

// Addresses outside the range must be rejected for every non-/32 prefix, not only /24.
TEST(OvCIDR, RejectsAddressesOutsideRangeForCommonPrefixes)
{
	struct Case
	{
		const char *cidr;
		const char *inside;
		const char *outside;
	};

	// Each row is a CIDR with one address inside it and one outside it.
	const Case cases[] = {
		// /8: the outside address differs in the first octet.
		{"10.0.0.0/8", "10.200.3.4", "11.0.0.1"},
		// /8: the inside address is the first host, the outside one is right below the network address.
		{"10.0.0.0/8", "10.0.0.1", "9.255.255.255"},
		// /12 is not octet-aligned: the inside address is the last host, the outside one is right past the range end.
		{"172.16.0.0/12", "172.31.255.254", "172.32.0.1"},
		// /16: the outside address differs only by one in the second octet.
		{"192.168.0.0/16", "192.168.77.88", "192.169.0.1"},
		// /16: the outside address is in an unrelated network.
		{"192.168.0.0/16", "192.168.0.1", "10.1.2.3"},
		// /24: the outside address differs in the third octet.
		{"192.168.1.0/24", "192.168.1.200", "192.168.2.5"},
		// /25 is not octet-aligned: the outside address is in the upper half of the same /24.
		{"192.168.1.0/25", "192.168.1.100", "192.168.1.200"},
		// /30: the inside address is the last one of the block, the outside one is the first of the next block.
		{"192.168.1.0/30", "192.168.1.3", "192.168.1.4"},
		// /31: the block is .4 and .5, and the outside address is the first of the next pair.
		{"192.168.1.4/31", "192.168.1.5", "192.168.1.6"},
	};

	for (const auto &c : cases)
	{
		SCOPED_TRACE(c.cidr);

		auto cidr = ov::CIDR::Parse(c.cidr);
		ASSERT_NE(cidr, nullptr);

		EXPECT_TRUE(cidr->CheckIP(c.inside)) << c.inside;
		EXPECT_FALSE(cidr->CheckIP(c.outside)) << c.outside;
	}
}

// Compares `CheckIP()` with a mask-based reference over a spread of addresses for every prefix.
TEST(OvCIDR, MatchesMaskReferenceForAllPrefixes)
{
	const char *networks[] = {"192.168.1.77", "10.20.30.40", "172.16.5.9"};

	for (const auto *network : networks)
	{
		const auto base = ToHostOrder(network);

		for (int bits = 1; bits <= 32; ++bits)
		{
			const uint32_t mask = 0xFFFFFFFFu << (32 - bits);

			SCOPED_TRACE(ov::String::FormatString("%s/%d", network, bits));

			auto cidr = ov::CIDR::Parse(ov::String::FormatString("%s/%d", network, bits));
			ASSERT_NE(cidr, nullptr);

			uint32_t mismatches = 0;
			for (uint32_t i = 0; i < 4096; ++i)
			{
				const uint32_t address = i * 2654435761u;
				const bool expected	   = (address & mask) == (base & mask);

				if (cidr->CheckIP(ToString(address).CStr()) != expected)
				{
					++mismatches;
				}
			}

			EXPECT_EQ(mismatches, 0u);
		}
	}
}

TEST(OvCIDR, RejectsMalformedInput)
{
	EXPECT_EQ(ov::CIDR::Parse("192.168.1.0"), nullptr);
	EXPECT_EQ(ov::CIDR::Parse("192.168.1.0/0"), nullptr);
	EXPECT_EQ(ov::CIDR::Parse("192.168.1.0/33"), nullptr);

	auto cidr = ov::CIDR::Parse("192.168.1.0/24");
	ASSERT_NE(cidr, nullptr);
	EXPECT_FALSE(cidr->CheckIP("not-an-ip"));
	EXPECT_FALSE(cidr->CheckIP("::1"));
}
