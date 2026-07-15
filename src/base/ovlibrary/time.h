//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "./string.h"

namespace ov
{
	class Time
	{
	public:
		// 1621251881528
		static int64_t GetTimestampInMs();
		// 1621251881
		static int64_t GetTimestamp();

		// 2360755441
		static int64_t GetMonotonicTimestamp();

		// 2021-05-17T06:23:11.260Z
		static String MakeUtcSecond(int64_t value_sec = -1LL);

		// 2021-05-17T06:24:15Z
		static String MakeUtcMillisecond(int64_t value_msec = -1LL);
	};
}  // namespace ov
