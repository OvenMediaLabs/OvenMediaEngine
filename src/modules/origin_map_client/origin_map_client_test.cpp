//==============================================================================
//
//  OvenMediaEngine
//
//  Covers OriginMapClient::BuildConnectionOptions(): host:port parsing,
//  TLS / SNI / CA bundle selection and timeouts. No Redis server is needed.
//
//==============================================================================
#include "origin_map_client.h"

#include <gtest/gtest.h>

namespace
{
	bool Build(const char *host, bool tls, const char *ca_cert, sw::redis::ConnectionOptions &options, const char *password = "")
	{
		return OriginMapClient::BuildConnectionOptions(host, password, tls, ca_cert, options);
	}
}  // namespace

TEST(OriginMapClientOptions, ParsesHostPortAndPassword)
{
	sw::redis::ConnectionOptions options;

	ASSERT_TRUE(Build("redis.example.com:6380", false, "", options, "secret"));

	EXPECT_EQ(options.host, "redis.example.com");
	EXPECT_EQ(options.port, 6380);
	EXPECT_EQ(options.password, "secret");
	EXPECT_TRUE(options.keep_alive);
}

TEST(OriginMapClientOptions, RejectsInvalidHostPort)
{
	sw::redis::ConnectionOptions options;

	EXPECT_FALSE(Build("", false, "", options));
	EXPECT_FALSE(Build("redis.example.com", false, "", options));
	EXPECT_FALSE(Build(":6379", false, "", options));
	EXPECT_FALSE(Build("redis.example.com:", false, "", options));
	EXPECT_FALSE(Build("redis.example.com:abc", false, "", options));
	EXPECT_FALSE(Build("redis.example.com:0", false, "", options));
	EXPECT_FALSE(Build("redis.example.com:65536", false, "", options));
	EXPECT_FALSE(Build("a:b:6379", false, "", options));
}

TEST(OriginMapClientOptions, AcceptsPortBoundaries)
{
	sw::redis::ConnectionOptions options;

	ASSERT_TRUE(Build("redis:1", false, "", options));
	EXPECT_EQ(options.port, 1);

	ASSERT_TRUE(Build("redis:65535", false, "", options));
	EXPECT_EQ(options.port, 65535);
}

TEST(OriginMapClientOptions, SetsBoundedTimeouts)
{
	sw::redis::ConnectionOptions options;

	ASSERT_TRUE(Build("redis:6379", false, "", options));

	// Zero means "no timeout", which would stall every caller holding the client mutex
	EXPECT_GT(options.connect_timeout.count(), 0);
	EXPECT_GT(options.socket_timeout.count(), 0);
}

TEST(OriginMapClientOptions, TlsDisabledLeavesTlsOptionsUntouched)
{
	sw::redis::ConnectionOptions options;

	ASSERT_TRUE(Build("redis.example.com:6379", false, "/some/ca.crt", options));

	EXPECT_FALSE(options.tls.enabled);
	EXPECT_TRUE(options.tls.sni.empty());
	EXPECT_TRUE(options.tls.cacert.empty());
}

TEST(OriginMapClientOptions, TlsHostnameIsSentAsSni)
{
	sw::redis::ConnectionOptions options;

	ASSERT_TRUE(Build("redis.example.com:6379", true, "/some/ca.crt", options));

	EXPECT_TRUE(options.tls.enabled);
	EXPECT_EQ(options.tls.sni, "redis.example.com");
	EXPECT_EQ(options.tls.cacert, "/some/ca.crt");
}

TEST(OriginMapClientOptions, TlsIpAddressIsNotSentAsSni)
{
	sw::redis::ConnectionOptions options;

	ASSERT_TRUE(Build("192.168.0.160:6379", true, "/some/ca.crt", options));

	EXPECT_TRUE(options.tls.enabled);
	EXPECT_TRUE(options.tls.sni.empty());
}

TEST(OriginMapClientOptions, ReusedOptionsDoNotKeepPreviousTlsSettings)
{
	sw::redis::ConnectionOptions options;

	ASSERT_TRUE(Build("redis.example.com:6379", true, "/some/ca.crt", options));
	ASSERT_TRUE(Build("redis.example.com:6379", false, "", options));

	EXPECT_FALSE(options.tls.enabled);
	EXPECT_TRUE(options.tls.sni.empty());
	EXPECT_TRUE(options.tls.cacert.empty());
}
