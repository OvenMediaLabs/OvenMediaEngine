//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "event.h"

namespace ov
{
	Event::Event(bool manual_reset)
		: _manual_reset(manual_reset),

		  _event(false)
	{
	}

	bool Event::SetEvent()
	{
		LockGuard lock(_mutex);

		_event = true;
		_condition.NotifyAll();

		return true;
	}

	bool Event::Reset()
	{
		LockGuard lock(_mutex);

		_event = false;

		return true;
	}

	bool Event::Wait(int timeout)
	{
		if(timeout != Infinite)
		{
			return Wait(std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout));
		}

		return Wait(std::chrono::time_point<std::chrono::steady_clock>::max());
	}

	bool Event::Wait(std::chrono::time_point<std::chrono::steady_clock> time_point)
	{
		LockGuard lock(_mutex);

		// event가 활성화 될 때까지 대기
		_condition.WaitUntil(lock, time_point, [&]() OV_REQUIRES(_mutex)
		{
			return _event;
		});

		bool result = _event;

		if(_manual_reset == false)
		{
			_event = false;
		}

		return result;
	}
}
