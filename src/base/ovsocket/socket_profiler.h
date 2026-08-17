//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#define USE_SOCKET_PROFILER 0

namespace ov
{
#if USE_SOCKET_PROFILER
	// Calculate and callback the time before and after the mutex lock and the time until the method is completely processed

	class SocketProfiler
	{
	public:
		using PostHandler = std::function<void(int64_t lock_elapsed, int64_t total_elapsed)>;

		SocketProfiler()
		{
			sw.Start();
		}

		~SocketProfiler()
		{
			if (post_handler != nullptr)
			{
				post_handler(lock_elapsed, sw.Elapsed());
			}
		}

		void AfterLock()
		{
			lock_elapsed = sw.Elapsed();
		}

		void SetPostHandler(PostHandler post_handler)
		{
			this->post_handler = post_handler;
		}

		StopWatch sw;
		int64_t lock_elapsed;
		PostHandler post_handler;
	};

#	define SOCKET_PROFILER_INIT() SocketProfiler __socket_profiler
#	define SOCKET_PROFILER_AFTER_LOCK() __socket_profiler.AfterLock()
#	define SOCKET_PROFILER_POST_HANDLER(handler) __socket_profiler.SetPostHandler(handler)
#else  // USE_SOCKET_PROFILER
#	define SOCKET_PROFILER_NOOP() \
		do                         \
		{                          \
		} while (false)

#	define SOCKET_PROFILER_INIT() SOCKET_PROFILER_NOOP()
#	define SOCKET_PROFILER_AFTER_LOCK() SOCKET_PROFILER_NOOP()
#	define SOCKET_PROFILER_POST_HANDLER(handler) SOCKET_PROFILER_NOOP()
#endif	// USE_SOCKET_PROFILER
}  // namespace ov