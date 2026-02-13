//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================

#include "file_provider.h"

#include <base/ovlibrary/url.h>
#include <base/provider/pull_provider/stream_props.h>
#include <orchestrator/orchestrator.h>

#include "file_application.h"
#include "file_private.h"
#include "file_stream.h"

using namespace cmn;

namespace pvd
{
	std::shared_ptr<FileProvider> FileProvider::Create(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
	{
		auto provider = std::make_shared<FileProvider>(server_config, router);
		if (!provider->Start())
		{
			logte("An error occurred while creating File Provider");
			return nullptr;
		}
		return provider;
	}

	FileProvider::FileProvider(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
		: PullProvider(server_config, router)
	{
		logtt("Created File Provider module.");
	}

	FileProvider::~FileProvider()
	{
		Stop();

		logtt("Terminated File Provider module.");
	}

	bool FileProvider::OnCreateHost(const info::Host &host_info)
	{
		return true;
	}

	bool FileProvider::OnDeleteHost(const info::Host &host_info)
	{
		return true;
	}

	std::shared_ptr<pvd::Application> FileProvider::OnCreateProviderApplication(const info::Application &app_info)
	{
		if (IsModuleAvailable() == false)
		{
			return nullptr;
		}

		bool is_parsed = false;
		app_info.GetConfig().GetProviders().GetFileProvider(&is_parsed);
		if (!is_parsed)
		{
			return nullptr;
		}

		std::thread t([&](const info::Application &info) {
			ov::logger::ThreadHelper thread_helper;

			CreateStreamFromStreamMap(info);
		}, app_info);
		t.detach();

		return FileApplication::Create(GetSharedPtrAs<pvd::PullProvider>(), app_info);
	}

	bool FileProvider::OnDeleteProviderApplication(const std::shared_ptr<pvd::Application> &application)
	{
		return true;
	}

	void FileProvider::CreateStreamFromStreamMap(const info::Application &app_info)
	{
		sleep(1);

		auto stream_list = app_info.GetConfig().GetProviders().GetFileProvider().GetStreamMap().GetStreamList();
		for (auto &stream : stream_list)
		{
			std::vector<ov::String> url_list;
			url_list.push_back(ov::String::FormatString("file://localhost/%s", stream.GetPath().CStr()));

			// Persistent = true
			// Failback = false
			// Relay = true
			auto stream_props = std::make_shared<pvd::PullStreamProperties>();
			stream_props->EnablePersistent(true);
			stream_props->EnableFailback(false);
			stream_props->EnableRelay(true);

			PullStream(std::make_shared<ov::Url>(), app_info, stream.GetName(), url_list, 0, stream_props);
		}
	}
}  // namespace pvd