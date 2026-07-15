//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <spdlog/common.h>
#include <spdlog/details/circular_q.h>
#include <spdlog/details/file_helper.h>
#include <spdlog/details/null_mutex.h>
#include <spdlog/details/os.h>
#include <spdlog/details/synchronous_factory.h>
#include <spdlog/fmt/chrono.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/sinks/base_sink.h>

#include <chrono>
#include <cstdio>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

#include "../../path_manager.h"

namespace ov::logger::sinks
{
	struct DailyRotationTime
	{
		int hour;
		int minute;
		int second;

		DailyRotationTime(int hour, int minute, int second)
			: hour(hour),
			  minute(minute),
			  second(second)
		{
		}

		bool IsValid() const
		{
			return (hour >= 0) && (hour < 24) && (minute >= 0) && (minute < 60) && (second >= 0 && second < 60);
		}

		// Calculate the next rotation time from the base time point
		std::chrono::system_clock::time_point CalculateNextTime(const std::chrono::system_clock::time_point &base_time_point) const
		{
			auto base_time	= spdlog::log_clock::to_time_t(base_time_point);
			auto base_tm	= spdlog::details::os::localtime(base_time);

			base_tm.tm_hour = hour;
			base_tm.tm_min	= minute;
			base_tm.tm_sec	= second;

			auto next_time	= spdlog::log_clock::from_time_t(std::mktime(&base_tm));

			if (next_time <= base_time_point)
			{
				next_time += std::chrono::hours(24);
			}

			return next_time;
		}

		static const DailyRotationTime &Midnight()
		{
			static DailyRotationTime midnight{0, 0, 0};

			return midnight;
		}

		static DailyRotationTime After(int hour, int minute, int second)
		{
			auto target = std::chrono::system_clock::now();

			target += std::chrono::hours(hour);
			target += std::chrono::minutes(minute);
			target += std::chrono::seconds(second);

			auto target_tm = spdlog::details::os::localtime(std::chrono::system_clock::to_time_t(target));

			return {target_tm.tm_hour, target_tm.tm_min, target_tm.tm_sec};
		}
	};

	class DailyFileSink final : public spdlog::sinks::base_sink<std::mutex>
	{
	public:
		DailyFileSink(
			ov::String base_filename,
			DailyRotationTime rotation_time					  = DailyRotationTime::Midnight(),
			const spdlog::file_event_handlers &event_handlers = {})
			: _base_filename(std::move(base_filename)),
			  _rotation_time(rotation_time),
			  _file_helper{event_handlers}
		{
			if (rotation_time.IsValid() == false)
			{
				spdlog::throw_spdlog_ex("DailyFileSink: Invalid rotation time");
			}

			_next_rotation_time_point = rotation_time.CalculateNextTime(spdlog::log_clock::now());
			_file_helper.open(_base_filename.CStr(), false);
		}

		static std::shared_ptr<DailyFileSink> Create(ov::String base_filename, DailyRotationTime rotation_time = DailyRotationTime::Midnight())
		{
			return std::make_shared<DailyFileSink>(std::move(base_filename), rotation_time);
		}

	protected:
		using Mutex		= std::mutex;
		using TimePoint = std::chrono::system_clock::time_point;

	protected:
		ov::String GenerateFileName(const TimePoint &time_point) const
		{
			std::tm tm = spdlog::details::os::localtime(spdlog::log_clock::to_time_t(time_point));

			return ov::String::FormatString(
				"%s.%04d%02d%02d",
				_base_filename.CStr(), tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
		}

		//--------------------------------------------------------------------
		// Overriding of spdlog::sinks::base_sink
		//--------------------------------------------------------------------
		void sink_it_(const spdlog::details::log_msg &msg) override
		{
			auto log_time = msg.time;

			if (log_time >= _next_rotation_time_point)
			{
				// If the time to rotate has come,
				_file_helper.close();

				bool do_rotate = false;
				ov::String rotation_file_name;

				if (_last_log_time_point != TimePoint::min())
				{
					// Rename the file to the log date if it has been output at least once before rotation
					rotation_file_name = GenerateFileName(_last_log_time_point);

					// If the target to rotate already exists, add a sequence number to the file name to prevent conflicts
					if (PathManager::IsFile(rotation_file_name))
					{
						for (int index = 0; index < 1000; index++)
						{
							auto new_file_name = ov::String::FormatString("%s.%d", rotation_file_name.CStr(), index);

							if (PathManager::IsFile(new_file_name) == false)
							{
								do_rotate		   = true;
								rotation_file_name = new_file_name;
								break;
							}
						}
					}
					else
					{
						do_rotate = true;
					}
				}

				if (do_rotate)
				{
					PathManager::Rename(_base_filename, rotation_file_name);
				}

				_file_helper.open(_base_filename.CStr(), false);
				_next_rotation_time_point = _rotation_time.CalculateNextTime(log_time);
			}
			else
			{
				_last_log_time_point = log_time;
			}

			spdlog::memory_buf_t formatted;
			base_sink<Mutex>::formatter_->format(msg, formatted);

			_file_helper.write(formatted);
		}

		void flush_() override
		{
			_file_helper.flush();
		}

	private:
		ov::String _base_filename;
		const DailyRotationTime _rotation_time;
		TimePoint _last_log_time_point = TimePoint::min();
		TimePoint _next_rotation_time_point;
		spdlog::details::file_helper _file_helper;
	};
}  // namespace ov::logger::sinks
