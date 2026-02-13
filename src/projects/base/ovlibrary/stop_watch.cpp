#include "stop_watch.h"

#include <utility>

#include "./log.h"

namespace ov
{
	StopWatch::StopWatch(String tag)
		: _tag(std::move(tag))
	{
	}

	bool StopWatch::IsStart() const
	{
		return _is_valid;
	}

	bool StopWatch::IsPaused() const
	{
		return _is_paused;
	}

	void StopWatch::Start()
	{
		if (IsStart() == true)
		{
			return;
		}

		_is_valid = true;
		_start = std::chrono::high_resolution_clock::now();
		_last = _start;
	}

	void StopWatch::Restart()
	{
		Stop();
		Start();
	}

	void StopWatch::Stop()
	{
		_is_valid = false;
	}

	void StopWatch::Pause()
	{
		if (IsStart() == false)
		{
			return;
		}

		if (IsPaused() == true)
		{
			return;
		}

		_pause_time = std::chrono::high_resolution_clock::now();
		_is_paused = true;
	}

	void StopWatch::Resume()
	{
		if (_is_paused == false)
		{
			return;
		}

		_paused_duration += std::chrono::high_resolution_clock::now() - _pause_time;
		_is_paused = false;
	}

	bool StopWatch::Update()
	{
		_last = std::chrono::high_resolution_clock::now();
		return _is_valid;
	}

	bool StopWatch::StartOrUpdate()
	{
		IsStart() ? (void)Update() : Start();

		return _is_valid;
	}

	int64_t StopWatch::Elapsed(bool nano) const
	{
		if (_is_valid)
		{
			auto current = std::chrono::high_resolution_clock::now();
			auto elapsed = current - _last - _paused_duration;

			if(nano == false)
			{
				return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
			}
			else
			{
				return std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
			}
		}

		return -1LL;
	}

	int64_t StopWatch::ElapsedUs() const
	{
		if (_is_valid)
		{
			auto current = std::chrono::high_resolution_clock::now();
			auto elapsed = current - _last - _paused_duration;

			return std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
		}

		return -1LL;
	}

	bool StopWatch::IsElapsed(int64_t milliseconds) const
	{
		return (Elapsed() >= milliseconds);
	}

	int64_t StopWatch::TotalElapsed() const
	{
		if (_is_valid)
		{
			auto current = std::chrono::high_resolution_clock::now();

			return std::chrono::duration_cast<std::chrono::milliseconds>(current - _start).count();
		}

		return -1LL;
	}

	void StopWatch::Print()
	{
		logt("StopWatch", "[%s] Elapsed: %lld (Total elapsed: %lld)", _tag.CStr(), Elapsed(), TotalElapsed());
	}
}  // namespace ov