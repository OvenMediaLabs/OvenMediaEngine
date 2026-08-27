//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <config/config.h>
#include <gtest/gtest.h>
#include <stdlib.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>

//  Covers the silence timeout options on the per-application provider item:
//  `PacketSilenceTimeoutMs` and `FirstMediaWaitTimeoutMs`.
//
//  These pin the boundary values, because once the item is loaded,
//  the runtime cannot tell an operator's value from a default or from a parse artifact.
namespace
{
	// Writes an `<RTMP>` element and parses it into the RTMP provider config item.
	// Returns false when the config layer rejects the document.
	bool ParseRtmpProvider(const std::string &body, cfg::vhost::app::pvd::RtmpProvider *provider)
	{
		char path[]	 = "/tmp/ome_provider_timeout_test_XXXXXX";
		const int fd = ::mkstemp(path);
		if (fd < 0)
		{
			return false;
		}
		::close(fd);

		{
			std::ofstream out(path);
			out << "<RTMP>" << body << "</RTMP>";
		}

		bool parsed = false;

		try
		{
			cfg::DataSource data_source(cfg::DataType::Xml, path, cfg::ItemName("RTMP"));
			provider->FromDataSource("RTMP", cfg::ItemName("RTMP"), data_source);
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

TEST(ProviderTimeout, FirstMediaWaitIsOffWhenAbsent)
{
	cfg::vhost::app::pvd::RtmpProvider provider;

	ASSERT_TRUE(ParseRtmpProvider("", &provider));

	// The option has no default, and nothing applies it while this is false,
	// so an absent option leaves the window on the `PacketSilenceTimeoutMs` policy.
	EXPECT_FALSE(provider.IsFirstMediaWaitTimeoutMsConfigured());
	EXPECT_EQ(provider.GetFirstMediaWaitTimeoutMs(), 0);
}

TEST(ProviderTimeout, FirstMediaWaitKeepsPositiveValue)
{
	cfg::vhost::app::pvd::RtmpProvider provider;

	ASSERT_TRUE(ParseRtmpProvider("<FirstMediaWaitTimeoutMs>8000</FirstMediaWaitTimeoutMs>", &provider));

	EXPECT_EQ(provider.GetFirstMediaWaitTimeoutMs(), 8000);
	EXPECT_TRUE(provider.IsFirstMediaWaitTimeoutMsConfigured());
}

TEST(ProviderTimeout, FirstMediaWaitRejectsZero)
{
	cfg::vhost::app::pvd::RtmpProvider provider;

	// `0` would leave the wait with no timeout at all,
	// so it is rejected rather than accepted and then ignored at runtime.
	EXPECT_FALSE(ParseRtmpProvider("<FirstMediaWaitTimeoutMs>0</FirstMediaWaitTimeoutMs>", &provider));
}

TEST(ProviderTimeout, FirstMediaWaitRejectsNegative)
{
	cfg::vhost::app::pvd::RtmpProvider provider;

	EXPECT_FALSE(ParseRtmpProvider("<FirstMediaWaitTimeoutMs>-1</FirstMediaWaitTimeoutMs>", &provider));
}

TEST(ProviderTimeout, FirstMediaWaitRejectsEmptyAndNonNumeric)
{
	// The config layer converts both to `0`,
	// which must be rejected for the same reason as a literal `0`: a typo cannot remove the guard.
	{
		cfg::vhost::app::pvd::RtmpProvider provider;
		EXPECT_FALSE(ParseRtmpProvider("<FirstMediaWaitTimeoutMs></FirstMediaWaitTimeoutMs>", &provider));
	}

	{
		cfg::vhost::app::pvd::RtmpProvider provider;
		EXPECT_FALSE(ParseRtmpProvider("<FirstMediaWaitTimeoutMs>abc</FirstMediaWaitTimeoutMs>", &provider));
	}
}

TEST(ProviderTimeout, PacketSilenceTimeoutTracksWhoSetIt)
{
	{
		cfg::vhost::app::pvd::RtmpProvider provider;
		ASSERT_TRUE(ParseRtmpProvider("", &provider));

		EXPECT_EQ(provider.GetPacketSilenceTimeoutMs(), 0);
		EXPECT_FALSE(provider.IsPacketSilenceTimeoutMsConfigured());
	}

	{
		cfg::vhost::app::pvd::RtmpProvider provider;
		ASSERT_TRUE(ParseRtmpProvider("<PacketSilenceTimeoutMs>5000</PacketSilenceTimeoutMs>", &provider));

		EXPECT_EQ(provider.GetPacketSilenceTimeoutMs(), 5000);
		EXPECT_TRUE(provider.IsPacketSilenceTimeoutMsConfigured());
	}

	{
		// An explicit `0` disables the timeout, and stays distinguishable from an absent option.
		cfg::vhost::app::pvd::RtmpProvider provider;
		ASSERT_TRUE(ParseRtmpProvider("<PacketSilenceTimeoutMs>0</PacketSilenceTimeoutMs>", &provider));

		EXPECT_EQ(provider.GetPacketSilenceTimeoutMs(), 0);
		EXPECT_TRUE(provider.IsPacketSilenceTimeoutMsConfigured());
	}

	{
		// Filling in a provider default drops the operator marker,
		// so nothing that must honor only an explicit setting picks the default up.
		cfg::vhost::app::pvd::RtmpProvider provider;
		ASSERT_TRUE(ParseRtmpProvider("<PacketSilenceTimeoutMs>0</PacketSilenceTimeoutMs>", &provider));
		provider.SetDefaultPacketSilenceTimeoutMs(1500);

		EXPECT_EQ(provider.GetPacketSilenceTimeoutMs(), 1500);
		EXPECT_FALSE(provider.IsPacketSilenceTimeoutMsConfigured());
	}
}
