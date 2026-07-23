//==============================================================================
//
//  OvenMediaEngine
//
//  Created by getroot
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/mediarouter/mediarouter_application_observer.h>
#include <base/provider/stream.h>
#include <base/publisher/stream.h>

namespace ocst
{
	class Application : public MediaRouterApplicationObserver
	{
	public:
		class CallbackInterface
		{
		public:
			virtual bool OnStreamCreated(const info::Application &app_info, const std::shared_ptr<info::Stream> &info) = 0;
			virtual bool OnStreamDeleted(const info::Application &app_info, const std::shared_ptr<info::Stream> &info) = 0;
			virtual bool OnStreamPrepared(const info::Application &app_info, const std::shared_ptr<info::Stream> &info) = 0;
		};

		Application(CallbackInterface *callback, const info::Application &app_info);

		info::VHostAppName GetVHostAppName() const;
		const info::Application & GetAppInfo() const;

		//--------------------------------------------------------------------
		// Implementation of MediaRouterApplicationObserver
		//--------------------------------------------------------------------
		// Temporarily used until Orchestrator takes stream management
		bool OnStreamCreated(const std::shared_ptr<info::Stream> &info) override;
		bool OnStreamDeleted(const std::shared_ptr<info::Stream> &info) override;
		bool OnStreamPrepared(const std::shared_ptr<info::Stream> &info) override;
		bool OnSendFrame(const std::shared_ptr<info::Stream> &info, const std::shared_ptr<MediaPacket> &packet) override;

		ObserverType GetObserverType() override;

		// Stream info as observed through the media router. To call into the real
		// provider/publisher stream, resolve it through the owning module
		// (e.g. Orchestrator::GetProviderStream(stream_info)).
		std::shared_ptr<info::Stream> GetProviderStream(const ov::String &stream_name);
		std::shared_ptr<info::Stream> GetPublisherStream(const ov::String &stream_name);
		size_t GetProviderStreamCount() const;
		size_t GetPublisherStreamCount() const;
		bool IsUnusedFor(int seconds) const;

	private:
		CallbackInterface *_callback = nullptr;
		info::Application _app_info;

		// Stream Name : Stream
		std::map<ov::String, std::shared_ptr<info::Stream>> _provider_stream_map;
		mutable std::shared_mutex _provider_stream_map_mutex;
		std::map<ov::String, std::shared_ptr<info::Stream>> _publisher_stream_map;
		mutable std::shared_mutex _publisher_stream_map_mutex;

		// unused timer 
		ov::StopWatch _idle_timer;
	};
}