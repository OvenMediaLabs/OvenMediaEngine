//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#include "srt_application.h"

#include "srt_private.h"
#include "srt_session.h"
#include "srt_stream.h"

#define OV_LOG_PREFIX_FORMAT "[%s(%u)] "
#define OV_LOG_PREFIX_VALUE GetVHostAppName().CStr(), GetId()

namespace pub
{
	std::shared_ptr<SrtApplication> SrtApplication::Create(const std::shared_ptr<Publisher> &publisher, const info::Application &application_info)
	{
		auto application = std::make_shared<SrtApplication>(publisher, application_info);

		application->Start();

		return application;
	}

	SrtApplication::SrtApplication(const std::shared_ptr<Publisher> &publisher, const info::Application &application_info)
		: Application(publisher, application_info)
	{
	}

	SrtApplication::~SrtApplication()
	{
		Stop();

		logat("SrtApplication has finally been terminated");
	}

	std::shared_ptr<Stream> SrtApplication::CreateStream(const std::shared_ptr<info::Stream> &info, uint32_t worker_count)
	{
		logat("Creating a new SRT stream: %s(%u)", info->GetName().CStr(), info->GetId());

		return SrtStream::Create(GetSharedPtrAs<Application>(), *info, worker_count);
	}

	bool SrtApplication::DeleteStream(const std::shared_ptr<info::Stream> &info)
	{
		logat("Deleting the SRT stream: %s(%u)", info->GetName().CStr(), info->GetId());

		auto stream = std::static_pointer_cast<SrtStream>(GetStream(info->GetId()));

		if (stream == nullptr)
		{
			logae("Failed to delete stream - Cannot find stream: %s(%u)", info->GetName().CStr(), info->GetId());
			return false;
		}

		logat("The SRT stream has been deleted: %s(%u)", stream->GetName().CStr(), info->GetId());
		return true;
	}
}  // namespace pub
