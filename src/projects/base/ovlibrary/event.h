//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "./ovdata_structure.h"

#include <mutex>
#include <condition_variable>

namespace ov
{
	class Event
	{
	public:
		explicit Event(bool manual_reset = false);

		// 이벤트 설정
		bool SetEvent();

		bool Reset();

		// timeout in milliseconds
		bool Wait(int timeout = Infinite);
		bool Wait(std::chrono::time_point<std::chrono::steady_clock> time_point);

	protected:
		bool _manual_reset;

		std::mutex _mutex;
		std::condition_variable _condition;
		bool _event;
	};
}

