//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <mutex>
#include <condition_variable>
#include <chrono>

namespace ov
{
	class Future
	{
	public:
		void Stop()
		{
			std::unique_lock<std::mutex> lock(_mutex);
			_stop_flag = true;
			_condition.notify_all();
		}

		bool Submit(bool result)
		{
			std::unique_lock<std::mutex> lock(_mutex);
			_result = result;
			_stop_flag = true;
			_condition.notify_all();
			return _result;
		}

		bool Get()
		{
			std::unique_lock<std::mutex> lock(_mutex);

			_condition.wait(lock, [this]() { return _stop_flag; });

			return _result;
		}

		bool GetFor(uint32_t timeout_delta_msec)
		{
			std::unique_lock<std::mutex> lock(_mutex);

			if (!_condition.wait_for(lock, std::chrono::milliseconds(timeout_delta_msec), [this]() { return _stop_flag; }))
			{
				return false;
			}

			return _result;
		}

	private:
		std::mutex _mutex;
		std::condition_variable _condition;
		bool _stop_flag = false;
		bool _result = false;
	};
}  // namespace ov