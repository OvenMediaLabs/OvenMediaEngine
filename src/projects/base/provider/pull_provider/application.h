//==============================================================================
//
//  PullProvider Base Class 
//
//  Created by Getroot
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/provider/application.h>
#include "stream_motor.h"
#include "orchestrator/orchestrator.h"

//TODO(Dimiden): It has to be moved to configuration
#define MAX_APPLICATION_STREAM_MOTOR_COUNT		20
#define MAX_UNUSED_STREAM_AVAILABLE_TIME_SEC	60

namespace pvd
{
	class PullProvider;
	class PullApplication : public Application
	{
	public:
		virtual bool Start() override;
		virtual bool Stop() override;

		// For pulling
		std::shared_ptr<pvd::Stream> CreateStream(const ov::String &stream_name, const std::vector<ov::String> &url_list, const std::shared_ptr<pvd::PullStreamProperties> &properties);

		// Delete stream
		virtual bool DeleteStream(const std::shared_ptr<Stream> &stream) override;
		virtual bool DeleteAllStreams() override;

	protected:
		explicit PullApplication(const std::shared_ptr<PullProvider> &provider, const info::Application &application_info);
		virtual ~PullApplication() override;

		virtual std::shared_ptr<pvd::PullStream> CreateStream(const uint32_t stream_id, const ov::String &stream_name, const std::vector<ov::String> &url_list, const std::shared_ptr<pvd::PullStreamProperties> &properties) = 0;

	private:
		uint32_t GetStreamMotorId(const std::shared_ptr<PullStream> &stream);

		bool AddStreamToMotorInternal(const std::shared_ptr<PullStream> &stream);
		bool DeleteStreamFromMotorInternal(const std::shared_ptr<PullStream> &stream);
	
		// Remove unused streams
		void WhiteElephantStreamCollector();

		// Try restarting the stream for failover
		bool ResumeStream(const std::shared_ptr<Stream> &stream);
		
		bool _stop_collector_thread_flag;
		std::thread _collector_thread;

		std::shared_mutex _stream_motors_guard;
		std::map<uint32_t, std::shared_ptr<StreamMotor>> _stream_motors;
	};
}
