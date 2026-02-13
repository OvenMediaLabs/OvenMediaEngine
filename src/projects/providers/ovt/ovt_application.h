//
// Created by getroot on 19. 12. 9.
//

#pragma once

#include <base/ovlibrary/url.h>
#include <base/common_types.h>
#include <base/provider/pull_provider/application.h>
#include <base/provider/pull_provider/stream.h>

namespace pvd
{
	class OvtApplication : public pvd::PullApplication
	{
	public:
		static std::shared_ptr<OvtApplication> Create(const std::shared_ptr<PullProvider> &provider, const info::Application &application_info);

		explicit OvtApplication(const std::shared_ptr<PullProvider> &provider, const info::Application &info);

		~OvtApplication() override;

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