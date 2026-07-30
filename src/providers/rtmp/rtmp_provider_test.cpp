//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "rtmp_provider.h"

#include <gtest/gtest.h>

//  Covers what `RtmpProvider` asks of the push provider base,
//  for the parts that decide whether an RTMP connection can sit there forever.
namespace
{
	// The property is protected, because only the push provider base has any business asking.
	class TestableRtmpProvider : public pvd::RtmpProvider
	{
	public:
		using RtmpProvider::DoesSilenceStartAtChannelCreation;
		using RtmpProvider::RtmpProvider;
	};
}  // namespace

TEST(RtmpProviderPolicy, SilenceStartsWhenTheChannelIsCreated)
{
	cfg::Server server_config;
	auto provider = std::make_shared<TestableRtmpProvider>(server_config, nullptr);

	// An RTMP client opens the connection and immediately sends its handshake,
	// so a channel with no data at all is a peer holding a connection it never uses.
	// The base class only starts the budget at creation for a provider that asks for it.
	EXPECT_TRUE(provider->DoesSilenceStartAtChannelCreation());
}
