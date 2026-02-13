//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include <base/mediarouter/media_buffer.h>
#include <base/mediarouter/media_type.h>
#include <base/ovlibrary/ovlibrary.h>
#include <base/provider/pull_provider/application.h>
#include <base/provider/pull_provider/provider.h>
#include <orchestrator/orchestrator.h>

namespace pvd
{
	class FileProvider : public pvd::PullProvider
	{

	public:
		static std::shared_ptr<FileProvider> Create(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);

		explicit FileProvider(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);

		~FileProvider() override;

		ProviderStreamDirection GetProviderStreamDirection() const override
		{
			return ProviderStreamDirection::Pull;
		}

		ProviderType GetProviderType() const override
		{
			return ProviderType::File;
		}
		
		const char* GetProviderName() const override
	    {
		    return "FileProvider";
	    }

		void CreateStreamFromStreamMap(const info::Application &app_info);

	protected:
		bool OnCreateHost(const info::Host &host_info) override;
		bool OnDeleteHost(const info::Host &host_info) override;
		std::shared_ptr<pvd::Application> OnCreateProviderApplication(const info::Application &app_info) override;
		bool OnDeleteProviderApplication(const std::shared_ptr<pvd::Application> &application) override;

		// int _worker_count = 1;
	};
}  // namespace pvd