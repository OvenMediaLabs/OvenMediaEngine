//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================


#pragma once

#include <base/ovlibrary/url.h>
#include <base/common_types.h>
#include <base/provider/pull_provider/application.h>
#include <base/provider/pull_provider/stream.h>

namespace pvd
{
	class FileApplication : public pvd::PullApplication
	{
	public:
		static std::shared_ptr<FileApplication> Create(const std::shared_ptr<PullProvider> &provider, const info::Application &application_info);

		explicit FileApplication(const std::shared_ptr<PullProvider> &provider, const info::Application &info);
		~FileApplication() override;

		std::shared_ptr<pvd::PullStream> CreateStream(const uint32_t stream_id, const ov::String &stream_name, const std::vector<ov::String> &url_list, const std::shared_ptr<pvd::PullStreamProperties> &properties) override;

		MediaRouterApplicationConnector::ConnectorType GetConnectorType() override
		{
			return MediaRouterApplicationConnector::ConnectorType::Provider;
		}

	private:
		bool Start() override;
		bool Stop() override;
	};
}