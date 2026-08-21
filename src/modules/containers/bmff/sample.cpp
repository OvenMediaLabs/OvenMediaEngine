//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================

#include "bmff_private.h"
#include "sample.h"

#include <algorithm>

namespace bmff
{
	bool Samples::AppendSample(const Sample &sample)
	{
		if (sample._media_packet == nullptr)
		{
			return false;
		}

		if (_samples.size() == 0)
		{
			_start_timestamp = sample._media_packet->GetDts();
			if (sample._media_packet->GetFlag() == MediaPacketFlag::Key)
			{
				_independent = true;
			}
		}

		_end_timestamp = sample._media_packet->GetDts() + sample._media_packet->GetDuration();
		_total_duration += sample._media_packet->GetDuration();
		_total_size += sample._media_packet->GetDataLength();
		_total_count += 1;

		_total_timing_duration_us += sample.timing.duration_us;
		_highest_pts_us = (_total_count == 1) ? sample.timing.pts_us : std::max(_highest_pts_us, sample.timing.pts_us);

		_samples.push_back(sample);

		return true;
	}

	// Get Data List
	const std::vector<Sample> &Samples::GetList() const
	{
		return _samples;
	}

	// Get Data At
	const Sample &Samples::GetAt(size_t index) const
	{
		return _samples.at(index);
	}

	std::shared_ptr<Samples> Samples::PopFront(size_t count)
	{
		if (count == 0 || count > _samples.size())
		{
			// Fulfilling an over-ask partially would hand back fewer samples
			// than the caller planned around, so it is refused instead
			OV_ASSERT2(count <= _samples.size());
			return nullptr;
		}

		auto taken = std::make_shared<Samples>();

		// Taking everything hands the samples over as they are, so nothing is
		// copied; the callers that take a whole buffer are the common ones
		if (count == _samples.size())
		{
			taken->_samples = std::move(_samples);
			taken->_start_timestamp = _start_timestamp;
			taken->_end_timestamp = _end_timestamp;
			taken->_total_duration = _total_duration;
			taken->_total_size = _total_size;
			taken->_total_count = _total_count;
			taken->_independent = _independent;
			taken->_total_timing_duration_us = _total_timing_duration_us;
			taken->_highest_pts_us = _highest_pts_us;

			_samples.clear();
			_start_timestamp = 0;
			_end_timestamp = 0;
			_total_duration = 0.0;
			_total_size = 0;
			_total_count = 0;
			_independent = false;
			_total_timing_duration_us = 0;
			_highest_pts_us = 0;

			return taken;
		}

		for (size_t index = 0; index < count; index++)
		{
			taken->AppendSample(_samples[index]);
		}

		// The popped prefix held the highest pts, so the remainder needs a new one
		bool recompute_highest_pts = (taken->GetHighestPtsUs() >= _highest_pts_us);

		_samples.erase(_samples.begin(), _samples.begin() + count);

		// What the prefix took is subtracted; what only the front decides is read
		// from the new front. The end of the range does not move.
		_total_duration -= taken->GetTotalDuration();
		_total_size -= taken->GetTotalSize();
		_total_count -= taken->GetTotalCount();
		_total_timing_duration_us -= taken->GetTotalTimingDurationUs();

		if (_samples.empty() == true)
		{
			_start_timestamp = 0;
			_end_timestamp = 0;
			_independent = false;
			_highest_pts_us = 0;

			return taken;
		}

		const auto &front = _samples.front();
		_start_timestamp = front._media_packet->GetDts();
		_independent = (front._media_packet->GetFlag() == MediaPacketFlag::Key);

		if (recompute_highest_pts == true)
		{
			_highest_pts_us = front.timing.pts_us;
			for (const auto &sample : _samples)
			{
				_highest_pts_us = std::max(_highest_pts_us, sample.timing.pts_us);
			}
		}

		return taken;
	}

	// Get Start Timestamp
	int64_t Samples::GetStartTimestamp() const
	{
		return _start_timestamp;
	}

	// Get End Timestamp
	int64_t Samples::GetEndTimestamp() const
	{
		return _end_timestamp;
	}

	// Get Total Duration
	double Samples::GetTotalDuration() const
	{
		return _total_duration;
	}

	// Get Total Size
	uint32_t Samples::GetTotalSize() const
	{
		return _total_size;
	}

	// Get Total Count
	uint32_t Samples::GetTotalCount() const
	{
		return _total_count;
	}

	// Is Empty
	bool Samples::IsEmpty() const
	{
		return _samples.empty();
	}

	// Is Independent
	bool Samples::IsIndependent() const
	{
		return _independent;
	}

	int64_t Samples::GetTotalTimingDurationUs() const
	{
		return _total_timing_duration_us;
	}

	int64_t Samples::GetHighestPtsUs() const
	{
		return _highest_pts_us;
	}
}  // namespace bmff