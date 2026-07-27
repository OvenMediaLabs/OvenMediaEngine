//==============================================================================
//
//  PushProvider Application Base Class 
//
//  Created by Getroot
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include "base/provider/application.h"
#include "stream.h"

namespace pvd
{
	class PushProvider;
	class PushApplication : public Application
	{
	public:
		virtual bool JoinStream(const std::shared_ptr<PushStream> &stream);

		// Returns the `PacketSilenceTimeoutMs` configured for the given provider type
		// in this application, or `0` when it is unset or disabled.
		// `is_configured` reports whether the option was present in the configuration, which tells a
		// value the operator asked for apart from a provider default filled in during config parsing.
		time_t GetConfiguredPacketSilenceTimeoutMs(ProviderType provider_type, bool *is_configured = nullptr);

	protected:
		PushApplication(const std::shared_ptr<PushProvider> &provider, const info::Application &application_info);
		virtual bool DeleteAllStreams() override;		

	private:

	};
}