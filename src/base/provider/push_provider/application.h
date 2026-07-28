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

		// Returns the `FirstMediaWaitTimeoutMs` configured for the given provider type in this
		// application. Unlike `PacketSilenceTimeoutMs` this always resolves to a value, falling back
		// to `cfg::vhost::app::pvd::DEFAULT_FIRST_MEDIA_WAIT_TIMEOUT_MS`. `0` disables the wait.
		time_t GetConfiguredFirstMediaWaitTimeoutMs(ProviderType provider_type);

	protected:
		PushApplication(const std::shared_ptr<PushProvider> &provider, const info::Application &application_info);
		virtual bool DeleteAllStreams() override;		

	private:

	};
}