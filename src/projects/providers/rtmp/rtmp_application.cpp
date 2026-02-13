//==============================================================================
//
//  RtmpApplication
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#include "rtmp_application.h"

#include "base/info/stream.h"
#include "base/provider/push_provider/application.h"
#include "rtmp_provider_private.h"
#include "rtmp_stream.h"

namespace pvd
{
	std::shared_ptr<RtmpApplication> RtmpApplication::Create(const std::shared_ptr<PushProvider> &provider, const info::Application &application_info)
	{
		auto application = std::make_shared<RtmpApplication>(provider, application_info);
		application->Start();
		return application;
	}

	RtmpApplication::RtmpApplication(const std::shared_ptr<PushProvider> &provider, const info::Application &application_info)
		: PushApplication(provider, application_info)
	{
	}

	bool RtmpApplication::JoinStream(const std::shared_ptr<PushStream> &stream)
	{
		// Check duplicated stream name
		// If there is a same stream name
		auto exist_stream = GetStreamByName(stream->GetName());
		if (exist_stream != nullptr)
		{
			// Block
			if (GetConfig().GetProviders().GetRtmpProvider().IsBlockDuplicateStreamName())
			{
				logti("Reject %s/%s stream it is a stream with a duplicate name.", GetVHostAppName().CStr(), stream->GetName().CStr());
				stream->SetApplication(GetSharedPtrAs<Application>());
				stream->SetApplicationInfo(GetSharedPtrAs<Application>());
				MonitorInstance->OnStreamCreationFailed(*stream);
				return false;
			}
			else
			{
				// Disconnect exist stream
				logti("Remove exist %s/%s stream because the stream with the same name is connected.", GetVHostAppName().CStr(), stream->GetName().CStr());
				DeleteStream(exist_stream);
			}
		}

		return PushApplication::JoinStream(stream);
	}
}  // namespace pvd
