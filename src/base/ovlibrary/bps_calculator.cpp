//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "bps_calculator.h"

#include "memory_utilities.h"

namespace ov
{
	BpsCalculator::BpsCalculator()
	{
		_stop_watch.Start();

		_timer.Push(
			[this](void *parameter) -> DelayQueueAction {
				constexpr int BITS_COUNT = OV_COUNTOF(_bits);
				int64_t bits = _current_bits.exchange(0L);

				LockGuard lock_guard(_mutex);
				_acc_bits -= _bits[0];
				// Shift
				::memmove(&(_bits[0]), &(_bits[1]), sizeof(_bits[0]) * (BITS_COUNT - 1));
				_bits[BITS_COUNT - 1] = bits;
				_acc_bits += bits;

				_bits_count = std::min(_bits_count + 1, BITS_COUNT);

				return DelayQueueAction::Repeat;
			},
			1000);

		_timer.Start();
	}

	void BpsCalculator::AddBits(int64_t bits)
	{
		_current_bits += bits;
		_total_bits += bits;
	}

	int64_t BpsCalculator::GetTotalBits() const
	{
		return _total_bits;
	}

	int64_t BpsCalculator::GetTotalElapsed() const
	{
		return _stop_watch.Elapsed();
	}

	int64_t BpsCalculator::GetBps() const
	{
		SharedLockGuard lock_guard(_mutex);

		if (_bits_count == 0)
		{
			return _acc_bits;
		}

		return _acc_bits / _bits_count;
	}
}  // namespace ov