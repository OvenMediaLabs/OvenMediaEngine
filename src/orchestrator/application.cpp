//==============================================================================
//
//  OvenMediaEngine
//
//  Created by getroot
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================

#include "application.h"

namespace ocst
{
	Application::Application(CallbackInterface *callback, const info::Application &app_info)
		: _callback(callback),
		  _app_info(app_info)
	{
		_idle_timer.Start();
	}

	info::VHostAppName Application::GetVHostAppName() const
	{
		return _app_info.GetVHostAppName();
	}

	const info::Application &Application::GetAppInfo() const
	{
		return _app_info;
	}

	bool Application::IsUnusedFor(int seconds) const
	{
		return _idle_timer.IsElapsed(seconds * 1000);
	}

	//--------------------------------------------------------------------
	// Implementation of MediaRouterApplicationObserver
	//--------------------------------------------------------------------
	// Temporarily used until Orchestrator takes stream management
	bool Application::OnStreamCreated(const std::shared_ptr<info::Stream> &info)
	{
		logt("DEBUG", "%s stream has been created: %s/%s", info->IsInputStream()?"Inbound":"Outbound", _app_info.GetVHostAppName().CStr(), info->GetName().CStr());

		if (info->IsInputStream())
		{
			std::lock_guard<std::shared_mutex> lock(_provider_stream_map_mutex);
			_provider_stream_map[info->GetName()] = std::static_pointer_cast<pvd::Stream>(info);
		}
		else
		{
			std::lock_guard<std::shared_mutex> lock(_publisher_stream_map_mutex);
			_publisher_stream_map[info->GetName()] = std::static_pointer_cast<pub::Stream>(info);
		}

		_idle_timer.Stop();

		return _callback->OnStreamCreated(_app_info, info);
	}

	bool Application::OnStreamDeleted(const std::shared_ptr<info::Stream> &info)
	{
		logt("DEBUG", "%s stream has been deleted: %s/%s", info->IsInputStream()?"Inbound":"Outbound", _app_info.GetVHostAppName().CStr(), info->GetName().CStr());

		if (info->IsInputStream())
		{
			std::lock_guard<std::shared_mutex> lock(_provider_stream_map_mutex);
			_provider_stream_map.erase(info->GetName());
		}
		else
		{
			std::lock_guard<std::shared_mutex> lock(_publisher_stream_map_mutex);
			_publisher_stream_map.erase(info->GetName());
		}

		if (_provider_stream_map.empty() && _publisher_stream_map.empty())
		{
			_idle_timer.Start();
		}
	
		return _callback->OnStreamDeleted(_app_info, info);
	}

	std::shared_ptr<pvd::Stream> Application::GetProviderStream(const ov::String &stream_name)
	{
		std::shared_lock<std::shared_mutex> lock(_provider_stream_map_mutex);
		auto item = _provider_stream_map.find(stream_name);
		if (item == _provider_stream_map.end())
		{
			return nullptr;
		}

		return item->second;
	}
	
	std::shared_ptr<pub::Stream> Application::GetPublisherStream(const ov::String &stream_name)
	{
		std::shared_lock<std::shared_mutex> lock(_publisher_stream_map_mutex);
		auto item = _publisher_stream_map.find(stream_name);

		if (item == _publisher_stream_map.end())
		{
			return nullptr;
		}

		return item->second;
	}

	size_t Application::GetProviderStreamCount() const
	{
		std::shared_lock<std::shared_mutex> lock(_provider_stream_map_mutex);
		return _provider_stream_map.size();
	}

	size_t Application::GetPublisherStreamCount() const
	{
		std::shared_lock<std::shared_mutex> lock(_publisher_stream_map_mutex);
		return _publisher_stream_map.size();
	}

	bool Application::OnStreamPrepared(const std::shared_ptr<info::Stream> &info)
	{
		return _callback->OnStreamPrepared(_app_info, info);
	}

	bool Application::OnStreamUpdated(const std::shared_ptr<info::Stream> &info)
	{
		return _callback->OnStreamUpdated(_app_info, info);
	}

	bool Application::OnSendFrame(const std::shared_ptr<info::Stream> &info, const std::shared_ptr<MediaPacket> &packet)
	{
		// Ignore packets
		return true;
	}

	MediaRouterApplicationObserver::ObserverType Application::GetObserverType()
	{
		return ObserverType::Orchestrator;
	}
} // namespace ocst