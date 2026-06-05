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
#include "./tsa/mutex.h"

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

		Mutex _mutex;
		ConditionVariable _condition;
		bool _event OV_GUARDED_BY(_mutex);
	};
}

