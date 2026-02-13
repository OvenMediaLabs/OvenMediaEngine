#pragma once

#include "./string.h"

#include <chrono>
#include <mutex>
#include <atomic>

namespace ov
{
	class PreciseTimer
	{
	public:
		PreciseTimer() = default;

		bool IsStart();
		void Start(int64_t interval);
		void Stop();
		bool Update();

		bool IsTimeout() const;
		int64_t GetElapsed() const;
		int64_t GetEndtime() const;
		
	protected:

		int64_t _interval { 0 }; 	// milliseconds
		int64_t _end_time { 0 };	// milliseconds

		bool _is_valid { false };

		std::chrono::high_resolution_clock::time_point _start;
	};
}