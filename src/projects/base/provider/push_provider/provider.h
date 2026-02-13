//==============================================================================
//
//  PushProvider Base Class 
//
//  Created by Getroot
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
// 	This is a base class for interleaved push provider such as RTMP, MPEG-TS...
// 
//==============================================================================

#pragma once

#include <base/provider/provider.h>

#include "application.h"
#include "stream.h"

namespace pvd
{
	static constexpr time_t DEFAULT_PUSH_CHANNEL_PACKET_SILENCE_TIMEOUT_MS = 3000;

    class PushProvider : public Provider
    {
    public:
		// Implementation Provider -> ModuleInterface
		ocst::ModuleType GetModuleType() const override
		{
			return ocst::ModuleType::PushProvider;
		}

		// Call CreateServer and store the server 
		virtual bool Start() override;
		virtual bool Stop() override;

		// Get Application by name
		std::shared_ptr<PushApplication> GetApplicationByName(const info::VHostAppName &vhost_app_name);

		// To be interleaved mode, a channel must have application/stream and track informaiton
		virtual bool PublishChannel(uint32_t channel_id, const info::VHostAppName &vhost_app_name, const std::shared_ptr<PushStream> &channel);

    protected:
		PushProvider(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);
		virtual ~PushProvider();

		virtual bool OnDeleteProviderApplication(const std::shared_ptr<pvd::Application> &application) override;

		// [Interleaved protocols such as RTSP/TCP, RTMP, MPEG-TS]
		// - OnChannelCreated() -> [Collect app/stream name and track informaiton] -> PublishChannel() -> OnChannelDeleted(Signalling)
		bool OnChannelCreated(uint32_t channel_id, const std::shared_ptr<pvd::PushStream> &channel);
		bool OnDataReceived(uint32_t channel_id, const std::shared_ptr<const ov::Data> &data);
		bool OnChannelDeleted(uint32_t channel_id);
		bool OnChannelDeleted(const std::shared_ptr<pvd::PushStream> &channel);
		std::shared_ptr<PushStream> GetChannel(uint32_t channel_id);

		virtual void OnTimedOut(const std::shared_ptr<PushStream> &channel) {}

    private:
		void ChannelTaskRunner();

		bool _run_task_runner;
		std::thread _task_runner_thread;

		// All streams (signalling streams + data streams)
		std::shared_mutex _channels_lock;
		// channel_id : stream
		std::map<uint32_t, std::shared_ptr<PushStream>>	_channels;
    };
}