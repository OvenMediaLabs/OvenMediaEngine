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
#include <base/modules/marker/marker.h>

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
		}

		FMP4Segment(uint64_t number, double duration_ms, const std::shared_ptr<ov::Data> &data)
		{
			_number = number;
			_duration_ms = duration_ms;
			_data = data;
			_is_completed = true;
		}

		// Parts become views of _data so the media is held once. Built unlocked: the
		// packager thread is the only writer, so _partials cannot change meanwhile.
		void SetCompleted()
		{
			size_t length = 0;
			for (const auto &partial : _partials)
			{
				length += partial->GetDataLength();
			}

			auto data = std::make_shared<ov::Data>(length);
			for (const auto &partial : _partials)
			{
				data->Append(partial->GetData());
			}

			// Views only after the last Append: Append copies a buffer that is shared
			std::deque<std::shared_ptr<FMP4Partial>> views;
			off_t offset = 0;
			for (const auto &partial : _partials)
			{
				auto partial_length = partial->GetDataLength();
				views.emplace_back(std::make_shared<FMP4Partial>(
					data->Subdata(offset, partial_length), partial->GetNumber(), partial->GetStartTimestamp(),
					partial->GetDurationMs(), partial->IsIndependent(), partial->GetTimebaseSeconds()));
				offset += partial_length;
			}

			std::unique_lock<std::shared_mutex> lock(_partials_lock);
			_data = std::move(data);
			_partials = std::move(views);
			_is_completed = true;
		}

		bool IsCompleted() const override
		{
			return _is_completed;
		}

		void SetTrackVersion(uint32_t track_version)
		{
			_track_version = track_version;
		}

		uint32_t GetTrackVersion() const override
		{
			return _track_version;
		}

		void SetDiscontinuityPoint()
		{
			_discontinuity_point = true;
		}

		bool IsDiscontinuityPoint() const override
		{
			return _discontinuity_point;
		}

		void SetCodecsParameter(const ov::String &codecs)
		{
			_codecs_parameter = codecs;
		}

		ov::String GetCodecsParameter() const override
		{
			return _codecs_parameter;
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

			_duration_ms += duration_ms;

			return true;
		}

		// Null until the segment is completed
		const std::shared_ptr<ov::Data> GetData() const override
		{
			std::shared_lock<std::shared_mutex> lock(_partials_lock);
			return _data;
		}

		size_t GetDataLength() const override
		{
			std::shared_lock<std::shared_mutex> lock(_partials_lock);
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
			std::shared_lock<std::shared_mutex> lock(_partials_lock);
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

		void AddMarkers(const std::vector<std::shared_ptr<Marker>> &markers)
		{
			_markers.insert(_markers.end(), markers.begin(), markers.end());
		}

		const std::vector<std::shared_ptr<Marker>> &GetMarkers() const override
		{
			return _markers;
		}

		void SetMarkers(const std::vector<std::shared_ptr<Marker>> &markers) override
		{
			_markers = markers;
		}

	private:
		std::atomic<bool> _is_completed = false;

		uint32_t _track_version = 0;
		bool _discontinuity_point = false;
		ov::String _codecs_parameter;

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