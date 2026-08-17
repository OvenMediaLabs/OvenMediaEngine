//==============================================================================
//
//  RtmpProvider
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include "base/common_types.h"
#include "base/provider/push_provider/application.h"
#include "base/provider/push_provider/stream.h"

namespace pvd
{
	class RtmpApplication : public PushApplication
	{
	public:
		static std::shared_ptr<RtmpApplication> Create(const std::shared_ptr<PushProvider> &provider, const info::Application &application_info);

		explicit RtmpApplication(const std::shared_ptr<PushProvider> &provider, const info::Application &info);
		~RtmpApplication() override = default;

		bool JoinStream(const std::shared_ptr<PushStream> &stream) override;
	};
}  // namespace pvd
