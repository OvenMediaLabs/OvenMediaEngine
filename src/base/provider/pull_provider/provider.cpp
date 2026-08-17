//==============================================================================
//
//  Provider Base Class
//
//  Created by Getroot
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================

#include "provider.h"

#include <zconf.h>

#include "application.h"
#include "provider_private.h"
#include "stream.h"

namespace pvd
{
	PullProvider::PullProvider(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
		: Provider(server_config, router)
	{
	}
	PullProvider::~PullProvider()
	{
	}

	ov::String PullProvider::GeneratePullingKey(const info::VHostAppName &vhost_app_name, const ov::String &stream_name)
	{
		ov::String key;

		key.Format("%s#%s", vhost_app_name.CStr(), stream_name.CStr());

		return key;
	}

	std::shared_ptr<PullingItem> PullProvider::LockPullStreamIfNeeded(const ov::String &key)
	{
		// It handles duplicate requests while the stream is being created.

		// Table lock
		std::unique_lock<std::mutex> table_lock(_pulling_table_mutex, std::defer_lock);

		while (true)
		{
			table_lock.lock();

			auto it = _pulling_table.find(key);
			std::shared_ptr<PullingItem> item;
			if (it != _pulling_table.end())
			{
				item = it->second;
			}

			if (item == nullptr || item->State() != PullingItem::PullingItemState::PULLING)
			{
				// First item
				auto item = std::make_shared<PullingItem>(key);
				item->SetState(PullingItem::PullingItemState::PULLING);
				item->Lock();

				_pulling_table[key] = item;

				return item;
			}

			table_lock.unlock();

			if (item != nullptr)
			{
				// it will wait until the previous request is completed
				logti("Wait for the same stream that was previously requested to be created.: %s", key.CStr());
				item->Wait();

				if (item->State() == PullingItem::PullingItemState::PULLING)
				{
					// Unexpected error
					OV_ASSERT2(false);
					return nullptr;
				}
				else if (item->State() == PullingItem::PullingItemState::PULLED)
				{
					continue;
				}
				else if (item->State() == PullingItem::PullingItemState::ERROR)
				{
					continue;
				}
			}
		}

		return nullptr;
	}

	bool PullProvider::UnlockPullStreamIfNeeded(const ov::String &key, const std::shared_ptr<PullingItem> &item, PullingItem::PullingItemState state)
	{
		if (item != nullptr)
		{
			item->SetState(state);
			item->Unlock();
		}

		{
			std::unique_lock<std::mutex> table_lock(_pulling_table_mutex);
			auto it = _pulling_table.find(key);
			if (it != _pulling_table.end() && it->second == item)
			{
				_pulling_table.erase(it);
			}
		}

		return true;
	}

	std::shared_ptr<pvd::Stream> PullProvider::PullStream(
		const std::shared_ptr<const ov::Url> &request_from,
		const info::Application &app_info, const ov::String &stream_name,
		const std::vector<ov::String> &url_list, off_t offset, const std::shared_ptr<pvd::PullStreamProperties> &properties)
	{
		auto key  = GeneratePullingKey(app_info.GetVHostAppName(), stream_name);
		auto item = LockPullStreamIfNeeded(key);
		if (item == nullptr)
		{
			logte("Failed to lock pulling item for stream [%s] in app [%s]", stream_name.CStr(), app_info.GetVHostAppName().CStr());
			return nullptr;
		}

		// Find App
		auto app = std::dynamic_pointer_cast<PullApplication>(GetApplicationById(app_info.GetId()));
		if (app == nullptr)
		{
			UnlockPullStreamIfNeeded(key, item, PullingItem::PullingItemState::PULLED);
			logte("There is no such app (%s) in %s", app_info.GetVHostAppName().CStr(), GetProviderName());
			return nullptr;
		}

		// Find Stream (The stream must not exist)
		auto stream = app->GetStreamByName(stream_name);
		if (stream != nullptr)
		{
			// If stream is not running it can be deleted.
			if (stream->GetState() == Stream::State::STOPPED || stream->GetState() == Stream::State::ERROR)
			{
				// remove immediately
				app->DeleteStream(stream);
			}
			else
			{
				logti("The inbound stream already exists: %s/%s", app_info.GetVHostAppName().CStr(), stream_name.CStr());
				UnlockPullStreamIfNeeded(key, item, PullingItem::PullingItemState::PULLED);
				return stream;
			}
		}

		// Create Stream
		stream = app->CreateStream(stream_name, url_list, properties);
		if (stream == nullptr)
		{
			logte("%s could not create [%s] stream.", app->GetApplicationTypeName(), stream_name.CStr());
			UnlockPullStreamIfNeeded(key, item, PullingItem::PullingItemState::ERROR);
			return nullptr;
		}

		UnlockPullStreamIfNeeded(key, item, PullingItem::PullingItemState::PULLED);
		return stream;
	}

	bool PullProvider::StopStream(const info::Application &app_info, const std::shared_ptr<pvd::Stream> &stream)
	{
		// Find App
		auto app = stream->GetApplication();
		if (app == nullptr)
		{
			logte("There is no such app (%s)", app_info.GetVHostAppName().CStr());
			return false;
		}

		return app->DeleteStream(stream);
	}
}  // namespace pvd