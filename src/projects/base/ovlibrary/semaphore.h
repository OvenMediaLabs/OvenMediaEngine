//==============================================================================
//
//  OvenMediaEngine
//
//  Created by getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "./tsa/mutex.h"

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
		Mutex _mutex;
		ConditionVariable _condition;
		unsigned long _count OV_GUARDED_BY(_mutex) = 0;
		bool _stop_flag OV_GUARDED_BY(_mutex) = false;
	};
}