#pragma once

#include <orchestrator/orchestrator.h>

#include "base/common_types.h"
#include "base/info/record.h"
#include "base/mediarouter/mediarouter_application_interface.h"
#include "base/ovlibrary/url.h"
#include "base/publisher/publisher.h"
#include "file_application.h"
#include "file_userdata.h"

namespace pub
{
	class FilePublisher : public pub::Publisher
	{
	public:
		static std::shared_ptr<FilePublisher> Create(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);

		FilePublisher(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);
		~FilePublisher() override;
		bool Stop() override;

	private:
		bool Start() override;

		//--------------------------------------------------------------------
		// Implementation of Publisher
		//--------------------------------------------------------------------
		PublisherType GetPublisherType() const override
		{
			return PublisherType::File;
		}
		const char *GetPublisherName() const override
		{
			return "FilePublisher";
		}

		bool OnCreateHost(const info::Host &host_info) override;
		bool OnDeleteHost(const info::Host &host_info) override;
		std::shared_ptr<pub::Application> OnCreatePublisherApplication(const info::Application &application_info) override;
		bool OnDeletePublisherApplication(const std::shared_ptr<pub::Application> &application) override;

	public:
		enum FilePublisherStatusCode
		{
			Success,
			FailureInvalidParameter,
			FailureDuplicateKey,
			FailureNotExist,
			FailureUnknown
		};
	};
}  // namespace pub 