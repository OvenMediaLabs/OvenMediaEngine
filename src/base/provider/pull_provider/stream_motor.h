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

#include <base/ovlibrary/tsa/mutex.h>

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
		// A stream in this motor, with the socket it registered for events
		struct Entry
		{
			std::shared_ptr<PullStream> stream;
			// The socket registered in `_epoll_fd`, which may no longer be the one the stream holds.
			// `nullptr` for a stream that is processed by interval.
			std::shared_ptr<ov::Socket> event_socket;
		};

		// Removes the stream's entry and its epoll registration, and returns false if there is no entry
		bool RemoveEntry(uint32_t stream_id) OV_REQUIRES(_streams_map_guard);

		void WorkerThread();

		uint32_t _id;

		// Watches the sockets of the streams processed by epoll. -1 before `Start()` and after `Stop()`.
		int _epoll_fd = -1;

		std::atomic<bool> _stop_thread_flag;
		std::thread _thread;
		ov::SharedMutex _streams_map_guard;
		// key: stream ID
		std::map<uint32_t, Entry> _streams OV_GUARDED_BY(_streams_map_guard);
	};
}