//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan Kwon
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <cmath>
#include <limits>

#include <base/info/vhost_app_name.h>


#include "base/ovlibrary/ovlibrary.h"
#include "base/ovlibrary/tsa/mutex.h"

namespace info
{
	typedef uint32_t managed_queue_id_t;

	class ManagedQueue
	{
		// Create URN specification for queue
		/*
			[URN Pattern]
				- mngq:v={VhostName}#{AppName}[s=/{StreamName}]:p={PART}:n={NAME}

			[PART]
				- pvd: Provider
				- imr: Inbound Mediarouter
				- trs: Transcoder
				- omr: Outbound Mediarouter
				- pub: Publisher
				- och: Ochestrator

			[NAME]
				- filter_{video|audio}
				- encoder_{codec_name}_{trackid}
				- decoder_{codec_name}_{trackid}
				- appworker_[{protocol}]_{id}
				- stremworker_[{protocol}]_{id}

			examples
				- mngq:v=#default#app:s=stream:p=trs:n=decoer_h264_0
				- mngq:v=#default#app:s=stream:p=trs:n=filter_video
				- mngq:v=#default#app:s=stream:p=trs:n=filter_audio
				- mngq:v=#default#app:s=stream:p=trs:n=encoder_opus_0
				- mngq:v=#default#app:s=stream:p=trs:n=encoder_h264_1
				- mngq:v=#default#app:p=imr:n=indicator
				- mngq:v=#default#app:p=omr:n=appworker
				- mngq:v=#default#app:p=pub:n=appworker
		*/
	public:
		enum class ThresholdMode : uint8_t
		{
			CountBased = 0,
			TimeBased  = 1
		};

		static const char *GetThresholdModeString(ThresholdMode threshold_mode)
		{
			switch (threshold_mode)
			{
			case ThresholdMode::CountBased:
				return "CountBased";
			case ThresholdMode::TimeBased:
				return "TimeBased";
			default:
				return "Unknown";
			}
		}

		class URN
		{
		public:
			URN(info::VHostAppName vhost_app_name, ov::String stream_name = nullptr, ov::String part = nullptr, ov::String name = nullptr)
				: _vhost_app_name(vhost_app_name),
				  _stream_name(stream_name),
				  _part(part),
				  _name(name)
			{
			}

			URN(ov::String vhost_app_name, ov::String stream_name = nullptr, ov::String part = nullptr, ov::String name = nullptr)
				: _vhost_app_name(vhost_app_name),
				  _stream_name(stream_name),
				  _part(part),
				  _name(name)
			{
			}

			info::VHostAppName& GetVHostAppName()
			{
				return _vhost_app_name;
			}
			ov::String& GetStreamName()
			{
				return _stream_name;
			}
			ov::String& GetPart()
			{
				return _part;
			}
			ov::String& GetName()
			{
				return _name;
			}

			ov::String ToString()
			{
				ov::String str = "mngq";

				if (_vhost_app_name.IsValid())
				{
					str.Append(":");
					str.Append("v=");
					str.Append(_vhost_app_name.ToString());
				}
				if (!_stream_name.IsEmpty())
				{
					str.Append(":");
					str.Append("s=");
					str.Append(_stream_name);
				}

				if (!_part.IsEmpty())
				{
					str.Append(":");
					str.Append("p=");
					str.Append(_part);
				}

				if (!_name.IsEmpty())
				{
					str.Append(":");
					str.Append("n=");
					str.Append(_name);
				}

				return str;
			}

		protected:
			info::VHostAppName _vhost_app_name;
			ov::String _stream_name;
			ov::String _part;
			ov::String _name;
		};

	public:
		explicit ManagedQueue(size_t threshold = 0)
			: _peak(0),
			  _size(0),
			  _threshold(threshold),
			  _threshold_mode(ThresholdMode::CountBased),
			  _threshold_value(threshold),
			  _threshold_exceeded_time_ms(0),
			  _buffering_delay(0),
			  _input_message_count(0),
			  _output_message_count(0),
			  _input_message_per_second(0),
			  _output_message_per_second(0),
			  _waiting_time_in_us(0),
			  _drop_message_count(0){};

		void SetId(info::managed_queue_id_t id)
		{
			_id = id;
		}

		info::managed_queue_id_t GetId() const
		{
			return _id;
		}

		ov::String GetTypeName() const
		{
			ov::SharedLockGuard lock_guard(_name_mutex);
			return _type_name;
		}

		// Set threshold in count-based mode.
		void SetThreshold(size_t threshold)
		{
			ov::LockGuard lock_guard(_name_mutex);

			_threshold_mode = ThresholdMode::CountBased;
			_threshold_value = threshold;

			_threshold = threshold;
		}

		// Set threshold in time-base mode. (millisecond)
		// Effective count is estimated from input_message_per_second * time_ms 
		void SetThresholdByTime(size_t time_ms)
		{
			ov::LockGuard lock_guard(_name_mutex);

			_threshold_mode = ThresholdMode::TimeBased;
			_threshold_value = time_ms;

			_threshold = 0;
		}

		ThresholdMode GetThresholdMode() const
		{
			ov::SharedLockGuard lock_guard(_name_mutex);
			return _threshold_mode;
		}

		// Get user-configured threshold value. The unit depends on the threshold mode.
		//   CountBased mode → count
		//   TimeBased  mode → milliseconds
		size_t GetThresholdValue() const
		{
			ov::SharedLockGuard lock_guard(_name_mutex);
			return _threshold_value;
		}

		size_t GetThreshold() const
		{
			ov::SharedLockGuard lock_guard(_name_mutex);
			return _threshold;
		}

		size_t GetPeak() const
		{
			return _peak;
		}

		size_t GetSize() const
		{
			return _size;
		}

		size_t GetInputMessagePerSecond() const
		{
			return _input_message_per_second;
		}

		size_t GetOutputMessagePerSecond() const
		{
			return _output_message_per_second;
		}

		uint64_t GetDropCount() const
		{
			return _drop_message_count;
		}

		int64_t GetWaitingTimeInUs() const
		{
			return _waiting_time_in_us;
		}

		// Dwell-time distribution: microseconds each item waited in the queue, recorded per
		// Dequeue into log2 buckets so percentiles stay cheap on the hot path.
		void RecordDwellUs(int64_t dwell_us)
		{
			if (dwell_us < 0)
			{
				dwell_us = 0;
			}

			// Bucket index = right-shifts until zero (~log2), clamped to the last bucket.
			int bucket = 0;
			int64_t value = dwell_us;
			while (value > 0 && bucket < DWELL_BUCKETS - 1)
			{
				value >>= 1;
				bucket++;
			}

			_dwell_hist[bucket].fetch_add(1, std::memory_order_relaxed);
			_dwell_count.fetch_add(1, std::memory_order_relaxed);
			_dwell_sum_us.fetch_add(static_cast<uint64_t>(dwell_us), std::memory_order_relaxed);

			// compare_exchange loops so min/max stay correct even without the caller's lock.
			int64_t prev_min = _dwell_min_us.load(std::memory_order_relaxed);
			while (dwell_us < prev_min && !_dwell_min_us.compare_exchange_weak(prev_min, dwell_us, std::memory_order_relaxed))
			{
			}

			int64_t prev_max = _dwell_max_us.load(std::memory_order_relaxed);
			while (dwell_us > prev_max && !_dwell_max_us.compare_exchange_weak(prev_max, dwell_us, std::memory_order_relaxed))
			{
			}
		}

		int64_t GetDwellPercentileUs(double percentile) const
		{
			uint64_t total = _dwell_count.load(std::memory_order_relaxed);
			if (total == 0)
			{
				return 0;
			}

			// Nearest-rank: ceil so the rank is not biased downward, clamped to [1, total].
			uint64_t target = static_cast<uint64_t>(std::ceil(static_cast<double>(total) * percentile));
			if (target < 1)
			{
				target = 1;
			}
			if (target > total)
			{
				target = total;
			}

			// RecordDwellUs puts dwell 0 in bucket 0 and dwell in [2^(k-1), 2^k - 1] in
			// bucket k, so a bucket's reported value is its lower bound: 0 for bucket 0,
			// 2^(i-1) otherwise.
			uint64_t cumulative = 0;
			for (int i = 0; i < DWELL_BUCKETS; i++)
			{
				cumulative += _dwell_hist[i].load(std::memory_order_relaxed);
				if (cumulative >= target)
				{
					return (i == 0) ? 0 : (static_cast<int64_t>(1) << (i - 1));
				}
			}

			return static_cast<int64_t>(1) << (DWELL_BUCKETS - 2);
		}

		int64_t GetDwellAvgUs() const
		{
			uint64_t total = _dwell_count.load(std::memory_order_relaxed);
			if (total == 0)
			{
				return 0;
			}

			return static_cast<int64_t>(_dwell_sum_us.load(std::memory_order_relaxed) / total);
		}

		uint64_t GetDwellCount() const
		{
			return _dwell_count.load(std::memory_order_relaxed);
		}

		int64_t GetDwellMinUs() const
		{
			if (_dwell_count.load(std::memory_order_relaxed) == 0)
			{
				return 0;
			}

			return _dwell_min_us.load(std::memory_order_relaxed);
		}

		int64_t GetDwellMaxUs() const
		{
			return _dwell_max_us.load(std::memory_order_relaxed);
		}

		int64_t GetThresholdExceededTimeMs() const
		{
			return _threshold_exceeded_time_ms.load();
		}

		void SetUrn(std::shared_ptr<URN> urn, const char* type_name)
		{
			ov::LockGuard lock_guard(_name_mutex);

			_type_name = type_name;
			_urn = urn;
		}

		std::shared_ptr<URN> GetUrn() const
		{
			ov::SharedLockGuard lock_guard(_name_mutex);
			return _urn;
		}

		ov::String ToString() const
		{
			ov::SharedLockGuard lock_guard(_name_mutex);
			if(_urn == nullptr)
			{
				return "No Urn";
			}

			return _urn->ToString();
		}

		info::managed_queue_id_t IssueUniqueQueueId()
		{
			static std::atomic<info::managed_queue_id_t> last_issued_queue_id(100);

			return last_issued_queue_id++;
		}

	protected:
		// ID of the queue (set once at registration)
		std::atomic<managed_queue_id_t> _id{0};

		// Name of the queue
		mutable ov::SharedMutex _name_mutex;
		std::shared_ptr<URN> _urn OV_GUARDED_BY(_name_mutex);

		// Type of template
		ov::String _type_name OV_GUARDED_BY(_name_mutex);

		// The ATOMIC metric members below are written only under the derived queue's
		// `_mutex` (kept consistent with the queue contents there);
		// they are atomic so that lock-free diagnostic/monitoring reads
		// (`GetSize()`/`GetInfoString()`/...) are well-defined.
		// TSA cannot model atomics, hence no `OV_GUARDED_BY`.
		// (`_threshold*` members are a separate group, guarded by `_name_mutex`.)

		// Peak size of the queue
		std::atomic<size_t> _peak{0};

		// Current size of the queue
		std::atomic<size_t> _size{0};

		// Threshold value computed according to the Threshold Mode.
		// 0 : No threshold
		size_t _threshold OV_GUARDED_BY(_name_mutex) = 0;

		// Threshold mode
		ThresholdMode _threshold_mode OV_GUARDED_BY(_name_mutex) = ThresholdMode::CountBased;

		// Threshold value: count (CountBased) or milliseconds (TimeBased)
		size_t _threshold_value OV_GUARDED_BY(_name_mutex) = 0;

		// threshold_exceeded_time increases from the point the queue is exceeded
		std::atomic<int64_t> _threshold_exceeded_time_ms{0};

		// Buffering delay (milliseconds).
		std::atomic<int> _buffering_delay{0};

		// Input Message Count
		std::atomic<int64_t> _input_message_count{0};
		std::atomic<int64_t> _last_input_message_count{0};
		// Output Message Count
		std::atomic<int64_t> _output_message_count{0};
		std::atomic<int64_t> _last_output_message_count{0};

		// Input Message Per Second
		std::atomic<size_t> _input_message_per_second{0};

		// Output Message Per Second
		std::atomic<size_t> _output_message_per_second{0};

		// Average Waiting Time(microseconds)
		std::atomic<int64_t> _waiting_time_in_us{0};

		// Drop Count
		std::atomic<uint64_t> _drop_message_count{0};

		// Dwell-time histogram (log2 microsecond buckets), see RecordDwellUs()
		static constexpr int DWELL_BUCKETS = 48;
		std::atomic<uint64_t> _dwell_hist[DWELL_BUCKETS] = {};
		std::atomic<uint64_t> _dwell_count{0};
		std::atomic<uint64_t> _dwell_sum_us{0};
		std::atomic<int64_t> _dwell_min_us{std::numeric_limits<int64_t>::max()};
		std::atomic<int64_t> _dwell_max_us{0};
	};

}  // namespace info