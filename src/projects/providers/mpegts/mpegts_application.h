//==============================================================================
//
//  RtmpProvider
//
//  Created by Getroot
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include "base/common_types.h"
#include "base/provider/push_provider/application.h"
#include "base/provider/push_provider/stream.h"

namespace pvd
{
	class MpegTsApplication : public PushApplication
	{
	public:
		static std::shared_ptr<MpegTsApplication> Create(const std::shared_ptr<PushProvider> &provider, const info::Application &application_info);

		explicit MpegTsApplication(const std::shared_ptr<PushProvider> &provider, const info::Application &info);
		~MpegTsApplication() override = default;
	};
}  // namespace pvd
