//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include "base/info/push.h"
#include "base/publisher/publisher.h"
#include "push_application.h"

namespace pub
{
	class PushPublisher : public pub::Publisher
	{
	public:
		static std::shared_ptr<PushPublisher> Create(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);

		PushPublisher(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);
		~PushPublisher() override;
		bool Stop() override;

	private:
		bool Start() override;

		//--------------------------------------------------------------------
		// Implementation of Publisher
		//--------------------------------------------------------------------
		PublisherType GetPublisherType() const override
		{
			return PublisherType::Push;
		}
		const char *GetPublisherName() const override
		{
			return "PushPublisher";
		}

		bool OnCreateHost(const info::Host &host_info) override;
		bool OnDeleteHost(const info::Host &host_info) override;
		std::shared_ptr<pub::Application> OnCreatePublisherApplication(const info::Application &application_info) override;
		bool OnDeletePublisherApplication(const std::shared_ptr<pub::Application> &application) override;
	};
}  // namespace pub