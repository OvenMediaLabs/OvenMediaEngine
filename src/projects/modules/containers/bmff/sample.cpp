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

	void Samples::PopFront()
	{
		_samples.erase(_samples.begin());
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
}  // namespace bmff