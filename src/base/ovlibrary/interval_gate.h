//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2026 OvenMedia Labs. All rights reserved.
//
//==============================================================================

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace ov
{
	// A thread-safe, self-repeating interval gate: TryConsume() returns true for at most one
	// caller per `interval_ms` window, then closes until the interval elapses.
	class IntervalGate
	{
	public:
		explicit IntervalGate(int64_t interval_ms)
			: _interval_ms(interval_ms)
		{
		}

		// Returns true at most once per interval, then starts a new interval.
		// Exactly one thread wins the CAS; all others get false until the next interval.
		bool TryConsume()
		{
			const int64_t now = NowMs();
			int64_t last	  = _last_fire_ms.load(std::memory_order_relaxed);

			if ((now - last) < _interval_ms)
			{
				return false;
			}

			return _last_fire_ms.compare_exchange_strong(last, now, std::memory_order_relaxed);
		}

		// Returns true if a TryConsume() would currently fire, without consuming.
		bool IsReady() const
		{
			return (NowMs() - _last_fire_ms.load(std::memory_order_relaxed)) >= _interval_ms;
		}

		// Close the gate at "now" so the next TryConsume() waits a full interval.
		void Reset()
		{
			_last_fire_ms.store(NowMs(), std::memory_order_relaxed);
		}

	private:
		static int64_t NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(
					   std::chrono::steady_clock::now().time_since_epoch())
				.count();
		}

		const int64_t _interval_ms;
		std::atomic<int64_t> _last_fire_ms{0};
	};
}  // namespace ov
