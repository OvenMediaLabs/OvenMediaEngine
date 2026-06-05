//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <chrono>

#include "./tsa/mutex.h"

namespace ov
{
	class Future
	{
	public:
		void Stop()
		{
			LockGuard lock(_mutex);
			_stop_flag = true;
			_condition.NotifyAll();
		}

		bool Submit(bool result)
		{
			LockGuard lock(_mutex);
			_result = result;
			_stop_flag = true;
			_condition.NotifyAll();
			return _result;
		}

		bool Get()
		{
			LockGuard lock(_mutex);

			_condition.Wait(lock, [this]() OV_REQUIRES(_mutex) -> bool { return _stop_flag; });

			return _result;
		}

		bool GetFor(uint32_t timeout_delta_msec)
		{
			LockGuard lock(_mutex);

			if (!_condition.WaitFor(lock, std::chrono::milliseconds(timeout_delta_msec), [this]() OV_REQUIRES(_mutex) -> bool { return _stop_flag; }))
			{
				return false;
			}

			return _result;
		}

	private:
		Mutex _mutex;
		ConditionVariable _condition;
		bool _stop_flag OV_GUARDED_BY(_mutex) = false;
		bool _result OV_GUARDED_BY(_mutex) = false;
	};
}  // namespace ov