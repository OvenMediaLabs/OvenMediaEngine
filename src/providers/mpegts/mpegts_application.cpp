//==============================================================================
//
//  MpegTs Application
//
//  Created by Getroot
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================

#include "mpegts_application.h"

#include "base/info/stream.h"
#include "base/provider/push_provider/application.h"
#include "mpegts_provider_private.h"
#include "mpegts_stream.h"

namespace pvd
{
	std::shared_ptr<MpegTsApplication> MpegTsApplication::Create(const std::shared_ptr<PushProvider> &provider, const info::Application &application_info)
	{
		auto application = std::make_shared<MpegTsApplication>(provider, application_info);
		application->Start();
		return application;
	}

	MpegTsApplication::MpegTsApplication(const std::shared_ptr<PushProvider> &provider, const info::Application &application_info)
		: PushApplication(provider, application_info)
	{
	}
}  // namespace pvd
