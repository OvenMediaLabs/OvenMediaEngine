//==============================================================================
//
//  StreamMotor 
//  StreamMotor is a thread for pull provider stream that calls 
//  the stream's ProcessMedia function periodically
//  Created by Getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "config/config.h"

#include "base/common_types.h"
#include "base/ovlibrary/ovlibrary.h"

#include "base/mediarouter/mediarouter_application_connector.h"
#include "stream.h"

#include <shared_mutex>

#define MAX_EPOLL_EVENTS						1024
#define EPOLL_TIMEOUT_MSEC						100
namespace pvd
{
	class StreamMotor
	{
	public:
		StreamMotor(uint32_t id);

		uint32_t GetId();
		uint32_t GetStreamCount();

		bool Start();
		bool Stop();

		bool AddStream(const std::shared_ptr<PullStream> &stream);
		bool DelStream(const std::shared_ptr<PullStream> &stream);

	private:
		bool AddStreamToEpoll(const std::shared_ptr<PullStream> &stream);
		bool DelStreamFromEpoll(const std::shared_ptr<PullStream> &stream);

		void WorkerThread();

		uint32_t _id;

		int _epoll_fd;

		std::atomic<bool> _stop_thread_flag;
		std::thread _thread;
		std::shared_mutex _streams_map_guard;
		std::map<uint32_t, std::shared_ptr<PullStream>> _streams;
	};
}