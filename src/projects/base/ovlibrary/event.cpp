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
		std::lock_guard<std::mutex> lock(_mutex);

		_event = true;
		_condition.notify_all();

		return true;
	}

	bool Event::Reset()
	{
		std::lock_guard<std::mutex> lock(_mutex);

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
		std::unique_lock<std::mutex> lock(_mutex);

		// event가 활성화 될 때까지 대기
		_condition.wait_until(lock, time_point, [&]
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
