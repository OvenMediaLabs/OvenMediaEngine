//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/common_types.h>
#include <base/info/session.h>
#include <base/publisher/application.h>

#include "srt_stream.h"

namespace pub
{
	class SrtApplication final : public Application
	{
	public:
		static std::shared_ptr<SrtApplication> Create(const std::shared_ptr<Publisher> &publisher, const info::Application &application_info);

		SrtApplication(const std::shared_ptr<Publisher> &publisher, const info::Application &application_info);
		~SrtApplication() override final;

	private:
		//--------------------------------------------------------------------
		// Implementation of Application
		//--------------------------------------------------------------------
		std::shared_ptr<Stream> CreateStream(const std::shared_ptr<info::Stream> &info, uint32_t worker_count) override;
		bool DeleteStream(const std::shared_ptr<info::Stream> &info) override;
		//--------------------------------------------------------------------
	};
}  // namespace pub
