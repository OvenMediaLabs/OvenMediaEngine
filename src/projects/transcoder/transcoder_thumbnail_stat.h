//==============================================================================
//
//  OvenMediaEngine
//
//  Created by AirenSoft
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/mediarouter/media_type.h>
#include <base/ovlibrary/ovlibrary.h>

#include <chrono>
#include <ctime>

// Per-frame timing for image (thumbnail) tracks, gated on the "ThumbStat" logger
// tag. Construct around the work to time; Emit() logs one line with the
// thread-CPU time consumed since construction and the wall time (measured since
// construction, or supplied by the caller when it already measured the span).
// When disabled, construction and Emit() do no work.
class ThumbStatTimer
{
public:
	explicit ThumbStatTimer(bool enabled)
		: _enabled(enabled)
	{
		if (_enabled)
		{
			::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &_cpu0);
			_wall0 = std::chrono::steady_clock::now();
		}
	}

	bool IsEnabled() const
	{
		return _enabled;
	}

	void Emit(const char *stage, cmn::MediaCodecId codec, const ov::String &variant, int64_t wall_us = -1) const
	{
		if (_enabled == false)
		{
			return;
		}

		timespec cpu1 = {};
		::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu1);
		int64_t cpu_us = ((int64_t)cpu1.tv_sec - _cpu0.tv_sec) * 1000000 + ((int64_t)cpu1.tv_nsec - _cpu0.tv_nsec) / 1000;

		if (wall_us < 0)
		{
			wall_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - _wall0).count();
		}

		logd("ThumbStat", "[THUMBSTAT] stage=%s codec=%s variant=%s wall_us=%lld cpu_us=%lld",
			 stage, cmn::GetCodecIdString(codec), variant.CStr(), (long long)wall_us, (long long)cpu_us);
	}

private:
	bool _enabled;
	timespec _cpu0 = {};
	std::chrono::steady_clock::time_point _wall0;
};
