//==============================================================================
//
//  OvenMediaEngine
//
//  Created by getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./semaphore.h"
#include "./assert.h"

#include <chrono>

namespace ov
{
	void Semaphore::Notify()
	{
		std::unique_lock<decltype(_mutex)> lock(_mutex);

		++_count;

		_condition.notify_all();
	}

	void Semaphore::Stop()
	{
		std::unique_lock<decltype(_mutex)> lock(_mutex);

		_stop_flag = true;

		_condition.notify_all();
	}

	void Semaphore::Wait()
	{
		std::unique_lock<decltype(_mutex)> lock(_mutex);

		while(_count <= 0 && !_stop_flag)
		{
			_condition.wait(lock);
		}

		if (_stop_flag)
		{
			return;
		}

		OV_ASSERT2(_count > 0);
		
		--_count;
	}

	bool Semaphore::WaitFor(uint32_t timeout_delta_msec)
	{
		std::unique_lock<decltype(_mutex)> lock(_mutex);

		while(_count <= 0 && !_stop_flag)
		{
			auto result = _condition.wait_for(lock, std::chrono::milliseconds(timeout_delta_msec));
			if(result == std::cv_status::timeout)
			{
				return false;
			}
		}

		if (_stop_flag)
		{
			return false;
		}

		OV_ASSERT2(_count > 0);
		
		--_count;
		return true;
	}

	bool Semaphore::TryWait()
	{
		std::unique_lock<decltype(_mutex)> lock(_mutex);

		if (_stop_flag)
		{
			return false;
		}

		if (_count > 0)
		{
			--_count;
			return true;
		}
		
		return false;
	}
}