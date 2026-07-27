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
		time_t GetConfiguredPacketSilenceTimeoutMs(ProviderType provider_type);

	protected:
		PushApplication(const std::shared_ptr<PushProvider> &provider, const info::Application &application_info);
		virtual bool DeleteAllStreams() override;		

	private:

	};
}