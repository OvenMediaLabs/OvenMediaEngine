#include "precise_timer.h"

#include <utility>

#include "./log.h"

namespace ov
{
	bool PreciseTimer::IsStart()
	{
		return _is_valid;
	}

	void PreciseTimer::Start(int64_t interval)
	{
		if (IsStart() == true)
		{
			return;
		}

		_is_valid = true;
		_interval = interval;
		_start = std::chrono::high_resolution_clock::now();
	}

	void PreciseTimer::Stop()
	{
		_is_valid = false;
	}

	bool PreciseTimer::Update()
	{
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - _start).count();

		while (_end_time <= elapsed)
		{
			_end_time += _interval;
		}

		return _is_valid;
	}

	bool PreciseTimer::IsTimeout() const
	{
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - _start).count();

		if (_end_time <= elapsed)
		{
			return true;
		}

		return false;
	}

	int64_t PreciseTimer::GetElapsed() const
	{
		auto current = std::chrono::high_resolution_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current - _start).count();

		return elapsed;
	}

	int64_t PreciseTimer::GetEndtime() const
	{
		return _end_time;
	}

}  // namespace ov