#pragma once

#include "./string.h"

#include <chrono>
#include <mutex>
#include <atomic>

namespace ov
{
	class StopWatch
	{
	public:
		StopWatch() = default;
		explicit StopWatch(String tag);

		bool IsStart() const;
		bool IsPaused() const;
		void Start();
		void Restart();
		void Stop();
		bool Update();
		// Start if not started, or update the last time if already started.
		bool StartOrUpdate();

		void Pause();
		void Resume();

		int64_t Elapsed(bool nano=false) const;
		int64_t ElapsedUs() const;
		bool IsElapsed(int64_t milliseconds) const;
		int64_t TotalElapsed() const;

		void Print();

	protected:
		String _tag;

		bool _is_valid { false };
		std::chrono::high_resolution_clock::time_point _start;
		std::chrono::high_resolution_clock::time_point _last;

		bool _is_paused { false };
		std::chrono::high_resolution_clock::time_point _pause_time;
		std::chrono::high_resolution_clock::duration _paused_duration = std::chrono::high_resolution_clock::duration::zero();
	};
}