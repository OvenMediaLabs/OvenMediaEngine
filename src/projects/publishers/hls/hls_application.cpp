//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================

#include "hls_application.h"
#include "hls_stream.h"
#include "hls_private.h"

std::shared_ptr<HlsApplication> HlsApplication::Create(const std::shared_ptr<pub::Publisher> &publisher, const info::Application &application_info)
{
	auto application = std::make_shared<HlsApplication>(publisher, application_info);
	application->Start();
	return application;
}

HlsApplication::HlsApplication(const std::shared_ptr<pub::Publisher> &publisher, const info::Application &application_info)
	: Application(publisher, application_info)
{
	auto ts_config = application_info.GetConfig().GetPublishers().GetHlsPublisher();
	bool is_parsed;
	const auto &cross_domains = ts_config.GetCrossDomains(&is_parsed);

	if (is_parsed)
	{
		_cors_manager.SetCrossDomains(application_info.GetVHostAppName(), cross_domains);
	}

	_origin_mode = true;
}

HlsApplication::~HlsApplication()
{
	Stop();
	logtt("HlsApplication(%d) has been terminated finally", GetId());
}

bool HlsApplication::Start()
{
	return Application::Start();
}

bool HlsApplication::Stop()
{
	return Application::Stop();
}

std::shared_ptr<pub::Stream> HlsApplication::CreateStream(const std::shared_ptr<info::Stream> &info, uint32_t worker_count)
{
	logtt("Created stream : %s/%u", info->GetName().CStr(), info->GetId());

	return HlsStream::Create(GetSharedPtrAs<pub::Application>(), *info, worker_count);
}

bool HlsApplication::DeleteStream(const std::shared_ptr<info::Stream> &info)
{
	logtt("HlsApplication::DeleteStream : %s/%u", info->GetName().CStr(), info->GetId());

	auto stream = std::static_pointer_cast<HlsStream>(GetStream(info->GetId()));
	if (stream == nullptr)
	{
		logte("HlsApplication::Delete stream failed. Cannot find stream (%s)", info->GetName().CStr());
		return false;
	}

	logtt("HlsApplication %s/%s stream has been deleted", GetVHostAppName().CStr(), stream->GetName().CStr());

	return true;
}
