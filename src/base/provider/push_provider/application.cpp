//==============================================================================
//
//  PushProviderApplication 
//
//  Created by Getroot
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================


#include "application.h"
#include "provider.h"
#include "provider_private.h"

namespace pvd
{
	PushApplication::PushApplication(const std::shared_ptr<PushProvider> &provider, const info::Application &application_info)
		: Application(provider, application_info)
	{
	}

	bool PushApplication::JoinStream(const std::shared_ptr<PushStream> &stream)
	{
		if(stream->IsReadyToReceiveStreamData() == false)
		{
			logte("The stream(%s/%s) is not yet ready to be published.", GetVHostAppName().CStr(), stream->GetName().CStr());
			return false;
		}

		int packet_silence_timeout_ms = 0;
		auto cfg_provider_list = GetConfig().GetProviders().GetProviderList();
		for (const auto &cfg_provider : cfg_provider_list)
		{
			if (cfg_provider->GetType() == GetParentProvider()->GetProviderType())
			{
				packet_silence_timeout_ms = cfg_provider->GetPacketSilenceTimeoutMs();
				break;
			}
		}
		
		stream->SetPacketSilenceTimeoutMs(packet_silence_timeout_ms);

		return AddStream(stream);
	}

	bool PushApplication::DeleteAllStreams()
	{
		return Application::DeleteAllStreams();
	}
}