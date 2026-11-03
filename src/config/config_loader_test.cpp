//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  tests/config/test_config_loader.cpp
//  Covers: cfg::ConfigManager, cfg::Server loading fundamentals
//
//  NOTE: These are skeleton tests. Expand with actual config XML snippets
//        once the test fixture infrastructure is established.
//
//==============================================================================
#include <config/config.h>
#include <gtest/gtest.h>
#include <stdlib.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <string>

// Placeholder - tests will be added here
// Example structure:
//
// TEST(ConfigLoader, LoadMinimalServerXml) { ... }
// TEST(ConfigLoader, InvalidXmlReturnsError) { ... }
// TEST(ConfigLoader, VirtualHostParsed) { ... }

TEST(ConfigPlaceholder, TodoImplementTests)
{
	GTEST_SKIP() << "Config tests not yet implemented - see tests/config/test_config_loader.cpp";
}

namespace
{
	// Parses a `<RedisServer>` element. Returns false when the config layer rejects it.
	bool ParseRedisServer(const std::string &body, cfg::vhost::orgn::RedisServer *redis_server)
	{
		char path[]	 = "/tmp/ome_redis_server_test_XXXXXX";
		const int fd = ::mkstemp(path);
		if (fd < 0)
		{
			return false;
		}
		::close(fd);

		{
			std::ofstream out(path);
			out << "<RedisServer>" << body << "</RedisServer>";
		}

		bool parsed = false;

		try
		{
			cfg::DataSource data_source(cfg::DataType::Xml, path, cfg::ItemName("RedisServer"));
			redis_server->FromDataSource("RedisServer", cfg::ItemName("RedisServer"), data_source);
			parsed = true;
		}
		catch (const cfg::ConfigError &)
		{
			parsed = false;
		}

		::remove(path);

		return parsed;
	}
}  // namespace

TEST(RedisServerConfig, ClusterTlsAndCaCertDefaultToOffWhenOmitted)
{
	cfg::vhost::orgn::RedisServer redis_server;

	ASSERT_TRUE(ParseRedisServer("<Host>127.0.0.1:6379</Host>", &redis_server));

	EXPECT_STREQ(redis_server.GetHost().CStr(), "127.0.0.1:6379");
	EXPECT_FALSE(redis_server.IsClusterEnabled());
	EXPECT_FALSE(redis_server.IsTlsEnabled());
	EXPECT_TRUE(redis_server.GetCaCert().IsEmpty());
}

TEST(RedisServerConfig, ParsesClusterTlsAndCaCert)
{
	cfg::vhost::orgn::RedisServer redis_server;

	ASSERT_TRUE(ParseRedisServer(
		"<Host>my-redis.clustercfg.cache.amazonaws.com:6379</Host>"
		"<Auth>secret</Auth>"
		"<Cluster>true</Cluster>"
		"<Tls>true</Tls>"
		"<CaCert>/etc/pki/tls/certs/ca-bundle.crt</CaCert>",
		&redis_server));

	EXPECT_STREQ(redis_server.GetAuth().CStr(), "secret");
	EXPECT_TRUE(redis_server.IsClusterEnabled());
	EXPECT_TRUE(redis_server.IsTlsEnabled());
	EXPECT_STREQ(redis_server.GetCaCert().CStr(), "/etc/pki/tls/certs/ca-bundle.crt");
}

TEST(RedisServerConfig, ClusterAndTlsAreIndependent)
{
	cfg::vhost::orgn::RedisServer redis_server;

	ASSERT_TRUE(ParseRedisServer("<Host>127.0.0.1:6379</Host><Cluster>false</Cluster><Tls>true</Tls>", &redis_server));

	EXPECT_FALSE(redis_server.IsClusterEnabled());
	EXPECT_TRUE(redis_server.IsTlsEnabled());
}

TEST(RedisServerConfig, RejectsHostWithoutPort)
{
	cfg::vhost::orgn::RedisServer redis_server;

	EXPECT_FALSE(ParseRedisServer("<Host>127.0.0.1</Host>", &redis_server));
}
