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

		// Returns the effective `PacketSilenceTimeoutMs` for the given provider type in this
		// application: the operator's value, or a default filled in during config parsing (MPEG-TS
		// gets `1500` ms), or `0` when neither applies and the timeout is therefore disabled.
		// `is_configured` reports whether that value is the operator's rather than a default.
		time_t GetConfiguredPacketSilenceTimeoutMs(ProviderType provider_type, bool *is_configured = nullptr);

		// Returns the `FirstMediaWaitTimeoutMs` configured for this provider type in this application,
		// or `0` when the option is absent. The option has no default: `is_configured` reports whether
		// the operator set it, and nothing applies it otherwise.
		time_t GetConfiguredFirstMediaWaitTimeoutMs(ProviderType provider_type, bool *is_configured = nullptr);

	protected:
		PushApplication(const std::shared_ptr<PushProvider> &provider, const info::Application &application_info);
		virtual bool DeleteAllStreams() override;		

	private:

	};
}