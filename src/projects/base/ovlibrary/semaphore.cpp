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
		LockGuard lock(_mutex);

		++_count;

		_condition.NotifyAll();
	}

	void Semaphore::Stop()
	{
		LockGuard lock(_mutex);

		_stop_flag = true;

		_condition.NotifyAll();
	}

	void Semaphore::Wait()
	{
		LockGuard lock(_mutex);

		while(_count <= 0 && !_stop_flag)
		{
			_condition.Wait(lock);
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
		LockGuard lock(_mutex);

		while(_count <= 0 && !_stop_flag)
		{
			auto result = _condition.WaitFor(lock, std::chrono::milliseconds(timeout_delta_msec));
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
		LockGuard lock(_mutex);

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