//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#define USE_STATS_COUNTER 0

namespace ov
{
#if USE_STATS_COUNTER
#	define STATS_COUNTER_INCREASE_PPS() stats_counter.IncreasePps()
#	define STATS_COUNTER_INCREASE_RETRY() stats_counter.IncreaseRetry()
#	define STATS_COUNTER_INCREASE_ERROR() stats_counter.IncreaseError()

	class StatsCounter
	{
	public:
		StatsCounter()
		{
			StartTracking();
		}

		~StatsCounter()
		{
			_stop = true;

			if (_tracking_thread.joinable())
			{
				_tracking_thread.join();
			}
		}

		void IncreasePps()
		{
			_count++;
			_total_count++;
		}

		void IncreaseRetry()
		{
			_retry_count++;
			_total_retry_count++;
		}

		void IncreaseError()
		{
			_error_count++;
			_total_error_count++;
		}

		void StartTracking()
		{
			_stop			 = false;

			_tracking_thread = std::thread(
				[this]() {
					logger::ThreadHelper thread_helper;

					int64_t min		   = INT64_MAX;
					int64_t max		   = INT64_MIN;

					int64_t retry_min  = INT64_MAX;
					int64_t retry_max  = INT64_MIN;

					int64_t error_min  = INT64_MAX;
					int64_t error_max  = INT64_MIN;

					int64_t loop_count = 0;
					while (_stop == false)
					{
						int64_t count		= _count;
						_count				= 0;

						int64_t retry_count = _retry_count;
						_retry_count		= 0;

						int64_t error_count = _error_count;
						_error_count		= 0;

						if ((count > 0) || (retry_count > 0) || (error_count > 0))
						{
							loop_count++;

							min					  = std::min(min, count);
							max					  = std::max(max, count);

							retry_min			  = std::min(retry_min, retry_count);
							retry_max			  = std::max(retry_max, retry_count);

							error_min			  = std::min(error_min, error_count);
							error_max			  = std::max(error_max, error_count);

							int64_t average		  = _total_count / ((loop_count == 0) ? 1 : loop_count);
							int64_t retry_average = _total_retry_count / ((loop_count == 0) ? 1 : loop_count);
							int64_t error_average = _total_error_count / ((loop_count == 0) ? 1 : loop_count);

							logi("SockStat",
								 "[Stats Counter] Total sampling count: %ld\n"
								 "+-------+---------+---------+---------+---------+--------------+\n"
								 "| Type  | Current |   Max   |   Min   | Average |    Total     |\n"
								 "+-------+---------+---------+---------+---------+--------------+\n"
								 "| PPS   | %7ld | %7ld | %7ld | %7ld | %12ld |\n"
								 "| Retry | %7ld | %7ld | %7ld | %7ld | %12ld |\n"
								 "| Error | %7ld | %7ld | %7ld | %7ld | %12ld |\n"
								 "+-------+---------+---------+---------+---------+--------------+\n",
								 loop_count,
								 count, max, min, average, static_cast<int64_t>(_total_count),
								 retry_count, retry_max, retry_min, retry_average, static_cast<int64_t>(_total_retry_count),
								 error_count, error_max, error_min, error_average, static_cast<int64_t>(_total_error_count));
						}

						sleep(1);
					}

					logi("SockStat", "[Stats Counter] Stats counter is terminated");
				});
		}

	protected:
		std::atomic<int64_t> _count{0};
		std::atomic<int64_t> _total_count{0};

		std::atomic<int64_t> _retry_count{0};
		std::atomic<int64_t> _total_retry_count{0};

		std::atomic<int64_t> _error_count{0};
		std::atomic<int64_t> _total_error_count{0};

		std::thread _tracking_thread;
		volatile bool _stop = true;
	};

	static StatsCounter stats_counter;
#else  // USE_STATS_COUNTER
#	define STATS_COUNTER_NOOP() \
		do                       \
		{                        \
		} while (false)
#	define STATS_COUNTER_INCREASE_PPS() STATS_COUNTER_NOOP()
#	define STATS_COUNTER_INCREASE_RETRY() STATS_COUNTER_NOOP()
#	define STATS_COUNTER_INCREASE_ERROR() STATS_COUNTER_NOOP()
#endif	// USE_STATS_COUNTER
}  // namespace ov
