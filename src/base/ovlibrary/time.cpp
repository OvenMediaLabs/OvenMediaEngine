//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./time.h"

#include <sys/time.h>

#include <chrono>

namespace ov
{
	int64_t Time::GetTimestampInMs()
	{
		auto current = std::chrono::system_clock::now().time_since_epoch();

		return std::chrono::duration_cast<std::chrono::milliseconds>(current).count();
	}

	int64_t Time::GetTimestamp()
	{
		auto current = std::chrono::system_clock::now().time_since_epoch();

		return std::chrono::duration_cast<std::chrono::seconds>(current).count();
	}

	int64_t Time::GetMonotonicTimestamp()
	{
		struct timespec now;

		::clock_gettime(CLOCK_MONOTONIC, &now);

		return now.tv_sec * 1000LL + now.tv_nsec / 1000000LL;
	}

	ov::String Time::MakeUtcSecond(int64_t value)
	{
		time_t target = (value < 0) ? ::time(nullptr) : static_cast<time_t>(value);
		std::tm *now_tm = ::gmtime(&target);

		char buffer[42];

		::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%TZ", now_tm);

		return buffer;
	}

	ov::String Time::MakeUtcMillisecond(int64_t value)
	{
		if (value == -1)
		{
			value = GetTimestampInMs();
		}

		ov::String result;

		// YYYY-MM-DDTHH:II:SS.sssZ
		// 012345678901234567890123
		result.SetCapacity(24);

		time_t time_value = static_cast<time_t>(value) / 1000;
		std::tm *now_tm = ::gmtime(&time_value);
		char *buffer = result.GetBuffer();

		// YYYY-MM-DDTHH:II:SS.sssZ
		// ~~~~~~~~~~~~~~~~~~~
		auto length = ::strftime(buffer, result.GetCapacity(), "%Y-%m-%dT%T", now_tm);

		if (result.SetLength(length))
		{
			// Change to "+00:00", because dash.js does not recognize timezone format such as "Z"
			//
			// in dash.js/src/dash/parser/matchers/DateTimeMatcher.js:
			// const datetimeRegex = /^([0-9]{4})-([0-9]{2})-([0-9]{2})T([0-9]{2}):([0-9]{2})(?::([0-9]*)(\.[0-9]*)?)?(?:([+-])([0-9]{2})(?::?)([0-9]{2}))?/;

			// YYYY-MM-DDTHH:II:SS.sss+00:00
			//                    ~~~~~~~~~~
			result.AppendFormat(".%03u+00:00", static_cast<uint32_t>(value % 1000LL));
			return result;
		}

		return "";
	}
}  // namespace ov
