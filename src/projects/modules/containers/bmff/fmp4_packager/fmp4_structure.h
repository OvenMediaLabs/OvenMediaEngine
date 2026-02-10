//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <base/modules/container/segment_storage.h>
#include <base/modules/marker/marker_box.h>

namespace bmff
{
	class FMP4Partial : public base::modules::PartialSegment
	{
	public:
		FMP4Partial(const std::shared_ptr<ov::Data> &data, int64_t number, int64_t start_timestamp, double duration_ms, bool independent, double timebase_seconds)
		{
			_data = data;
			_number = number;
			_duration_ms = duration_ms;
			_start_timestamp = start_timestamp;
			_independent = independent;
			_timebase_seconds = timebase_seconds;
		}

		int64_t GetNumber() const override
		{
			return _number;
		}

		int64_t GetStartTimestamp() const override
		{
			return _start_timestamp;
		}

		double GetDurationMs() const override
		{
			return _duration_ms;
		}

		// Get Size
		uint64_t GetDataLength() const override
		{
			return _data == nullptr ? 0 : _data->GetLength();
		}

		bool IsIndependent() const override
		{
			return _independent;
		}

		const std::shared_ptr<ov::Data> GetData() const override
		{
			return _data;
		}

		double GetTimebaseSeconds() const override
		{
			return _timebase_seconds;
		}

	private:
		int64_t _number = -1;
		int64_t _start_timestamp = 0;
		double _duration_ms = 0;
		bool _independent = false;
		std::shared_ptr<ov::Data> _data;
		double _timebase_seconds = 0.0;
	};

	class FMP4Segment : public base::modules::Segment
	{
	public:
		FMP4Segment(uint64_t number, uint64_t target_duration, double timebase_seconds)
		{
			_number = number;
			_timebase_seconds = timebase_seconds;

			// For performance, reserve memory for 4Mbps * target_duration (sec)
			_data = std::make_shared<ov::Data>(((1000.0 * 1000.0 * 4.0)/8.0) * (static_cast<double>(target_duration) / 1000.0));
		}

		FMP4Segment(uint64_t number, double duration_ms, const std::shared_ptr<ov::Data> &data)
		{
			_number = number;
			_duration_ms = duration_ms;
			_data = data;

			SetCompleted();
		}

		void SetCompleted()
		{
			_is_completed = true;
		}

		bool IsCompleted() const
		{
			return _is_completed;
		}

		bool AppendPartialData(const std::shared_ptr<ov::Data> &partial_data, int64_t start_timestamp, double duration_ms, bool independent)
		{
			if (_is_completed)
			{
				return false;
			}

			std::unique_lock<std::shared_mutex> lock(_partials_lock);

			auto partial_count = _partials.size();
			if (partial_count == 0)
			{
				_start_timestamp = start_timestamp;
			}

			_partials.emplace_back(std::make_shared<FMP4Partial>(partial_data, partial_count, start_timestamp, duration_ms, independent, _timebase_seconds));
			_last_partial_number = partial_count;

			lock.unlock();
			
			// Append data
			_duration_ms += duration_ms;
			_data->Append(partial_data);

			return true;
		}

		// Get Data
		const std::shared_ptr<ov::Data> GetData() const override
		{
			return _data;
		}

		size_t GetDataLength() const override
		{
			return _data == nullptr ? 0 : _data->GetLength();
		}

		// Get Number
		int64_t GetNumber() const override
		{
			return _number;
		}

		// Get Start Timestamp
		int64_t GetStartTimestamp() const override
		{
			return _start_timestamp;
		}

		// Get Duration
		double GetDurationMs() const override
		{
			return _duration_ms;
		}

		double GetTimebaseSeconds() const override
		{
			return _timebase_seconds;
		}

		// Get partial segment Count
		uint64_t GetPartialCount() const
		{
			std::shared_lock<std::shared_mutex> lock(_partials_lock);
			return _partials.size();
		}

		int64_t GetLastPartialNumber() const
		{
			return _last_partial_number;
		}

		// Get Partial Segment At
		std::shared_ptr<FMP4Partial> GetPartialSegment(uint64_t index) const
		{
			std::shared_lock<std::shared_mutex> lock(_partials_lock);

			if (index >= _partials.size())
			{
				return nullptr;
			}

			return _partials[index];
		}

		bool HasMarker() const override
		{
			return _markers.empty() == false;
		}

		void SetMarkers(const std::vector<std::shared_ptr<Marker>> &markers)
		{
			_markers = markers;
		}

		void AddMarkers(const std::vector<std::shared_ptr<Marker>> &markers)
		{
			_markers.insert(_markers.end(), markers.begin(), markers.end());
		}

		const std::vector<std::shared_ptr<Marker>> &GetMarkers() const override
		{
			return _markers;
		}

	private:
		bool _is_completed = false;

		int64_t _number = -1;

		int64_t _start_timestamp = 0;
		double _duration_ms = 0;

		std::deque<std::shared_ptr<FMP4Partial>> _partials;
		mutable std::shared_mutex _partials_lock;

		int64_t _last_partial_number = -1;

		// Segment Data
		std::shared_ptr<ov::Data> _data;

		std::vector<std::shared_ptr<Marker>> _markers;

		double _timebase_seconds = 0.0;
	};
}