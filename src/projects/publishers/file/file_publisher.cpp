#include "file_publisher.h"

#include <base/ovlibrary/url.h>

#include "file_private.h"

namespace pub
{
	std::shared_ptr<FilePublisher> FilePublisher::Create(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
	{
		auto file = std::make_shared<FilePublisher>(server_config, router);

		if (!file->Start())
		{
			logte("An error occurred while creating FilePublisher");
			return nullptr;
		}

		return file;
	}

	FilePublisher::FilePublisher(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
		: Publisher(server_config, router)
	{
		logtt("FilePublisher has been create");
	}

	FilePublisher::~FilePublisher()
	{
		logtt("FilePublisher has been terminated finally");
	}

	bool FilePublisher::Start()
	{
		return Publisher::Start();
	}

	bool FilePublisher::Stop()
	{
		return Publisher::Stop();
	}

	bool FilePublisher::OnCreateHost(const info::Host &host_info)
	{
		return true;
	}

	bool FilePublisher::OnDeleteHost(const info::Host &host_info)
	{
		return true;
	}

	std::shared_ptr<pub::Application> FilePublisher::OnCreatePublisherApplication(const info::Application &application_info)
	{
		if (IsModuleAvailable() == false)
		{
			return nullptr;
		}

		return FileApplication::Create(FilePublisher::GetSharedPtrAs<pub::Publisher>(), application_info);
	}

	bool FilePublisher::OnDeletePublisherApplication(const std::shared_ptr<pub::Application> &application)
	{
		return true;
	}
}  // namespace pub