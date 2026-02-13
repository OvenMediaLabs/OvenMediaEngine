//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include <base/common_types.h>
#include <base/info/session.h>
#include <base/publisher/application.h>

#include "push_stream.h"

namespace pub
{
	class PushApplication final : public pub::Application
	{
	public:
		enum ErrorCode
		{
			Success,
			FailureInvalidParameter,
			FailureDuplicateKey,
			FailureNotExist,
			FailureUnknown
		};

	public:
		static std::shared_ptr<PushApplication> Create(const std::shared_ptr<pub::Publisher> &publisher, const info::Application &application_info);
		PushApplication(const std::shared_ptr<pub::Publisher> &publisher, const info::Application &application_info);
		~PushApplication() final;

	private:
		bool Start() override;
		bool Stop() override;

		// Application Implementation
		std::shared_ptr<pub::Stream> CreateStream(const std::shared_ptr<info::Stream> &info, uint32_t worker_count) override;
		bool DeleteStream(const std::shared_ptr<info::Stream> &info) override;

	public:
		std::shared_ptr<ov::Error> StartPush(const std::shared_ptr<info::Push> &push, bool is_config = false);
		std::shared_ptr<ov::Error> StopPush(const std::shared_ptr<info::Push> &push);
		std::shared_ptr<ov::Error> GetPushes(const std::shared_ptr<info::Push> push, std::vector<std::shared_ptr<info::Push>> &results);
		
		void StartPushInternal(const std::shared_ptr<info::Push> &push, std::shared_ptr<pub::Session> session);
		void StopPushInternal(const std::shared_ptr<info::Push> &push, std::shared_ptr<pub::Session> session);
		void SessionControlThread();

		std::shared_ptr<info::Push> GetPushById(ov::String id);
		std::vector<std::shared_ptr<info::Push>> GetPushesByStreamName(const ov::String streamName);	
		std::vector<std::shared_ptr<info::Push>> GetPushesByStreamMap(const ov::String &xml_path, const std::shared_ptr<info::Stream> &stream_info);

	private:
		bool Validate(const std::shared_ptr<info::Push> &push, ov::String &error_message);

		std::atomic<bool> _session_control_stop_thread_flag = false;
		std::thread _session_contol_thread;

		// <uniqueId, pushInfo>
		std::map<ov::String, std::shared_ptr<info::Push>> _pushes;
		std::shared_mutex _push_map_mutex;
	};
}  // namespace pub