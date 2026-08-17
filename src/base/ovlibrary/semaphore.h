//==============================================================================
//
//  OvenMediaEngine
//
//  Created by getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <mutex>
#include <condition_variable>

namespace ov
{
	class Semaphore
	{
	public:
		void Stop();
		void Notify();
		void Wait();

		// return false : timed out, return true : signalled
		bool WaitFor(uint32_t timeout_delta_msec);
		bool TryWait();

	private:
		std::mutex _mutex;
		std::condition_variable _condition;
		unsigned long _count = 0;
		bool _stop_flag = false;
	};
}