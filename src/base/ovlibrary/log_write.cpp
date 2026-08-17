//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Benjamin
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================

#include "log_write.h"

#include <sys/stat.h>

#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>

namespace ov
{
	LogWrite::LogWrite(String log_file_name, bool include_date_in_filename)
		: _last_day(0),
		  _log_path(OV_LOG_DIR)
	{
		if (log_file_name.IsEmpty())
		{
			_log_file_name = OV_DEFAULT_LOG_FILE;
		}
		else
		{
			_log_file_name = log_file_name;
		}

		_log_file.Format("%s/%s", _log_path.CStr(), _log_file_name.CStr());
		_include_date_in_filename = include_date_in_filename;
	}

	void LogWrite::SetLogPath(const char *log_path)
	{
		LockGuard lock_guard(_log_stream_mutex);
		SetLogPathInternal(log_path);
	}

	void LogWrite::SetLogPathInternal(const char *log_path)
	{
		_log_path = log_path;
		_log_file.Format("%s/%s", _log_path.CStr(), _log_file_name.CStr());
	}

	String LogWrite::GetLogPath() const
	{
		LockGuard lock_guard(_log_stream_mutex);
		return _log_path;
	}

	void LogWrite::OpenNewFile(std::time_t time)
	{
		if (_start_service)
		{
			SetLogPathInternal(OV_LOG_DIR_SVC);

			// Change default log path to /var once for running service
			_start_service = false;
		}

		if ((::mkdir(_log_path.CStr(), 0755) == -1) && errno != EEXIST)
		{
			return;
		}

		_log_stream.close();
		_log_stream.clear();

		if (_include_date_in_filename == true)
		{
			std::tm local_time{};
			::localtime_r(&time, &local_time);
			std::ostringstream logfile;
			logfile << _log_file << "." << std::put_time(&local_time, "%Y%m%d");
			_log_stream.open(logfile.str().c_str(), std::ofstream::out | std::ofstream::app);
		}
		else
		{
			_log_stream.open(_log_file.CStr(), std::ofstream::out | std::ofstream::app);
		}
	}

	void LogWrite::SetAsService(bool start_service)
	{
		_start_service = start_service;
	}

	void LogWrite::SetEnabled(bool enabled)
	{
		_enabled = enabled;
	}

	void LogWrite::Write(const char *log, std::time_t time)
	{
		if (_enabled == false)
		{
			return;
		}

		if (time == 0)
		{
			time = std::time(nullptr);
		}
		std::tm local_time{};
		::localtime_r(&time, &local_time);

		// One lock for the whole stream section (open/rotate/write); OpenNewFile()
		// requires it to be held by the caller.
		LockGuard lock_guard(_log_stream_mutex);

		if (!_log_stream.is_open() || _log_stream.fail())
		{
			OpenNewFile(time);
		}

		// Need to open new file?
		if (_last_day != local_time.tm_mday)
		{
			// Not first
			if (_last_day != 0)
			{
				if (_include_date_in_filename == false)
				{
					// Backup file to (filename.log.yymmdd)
					std::ostringstream logfile;
					logfile << _log_file << "." << std::put_time(&local_time, "%Y%m%d");
					::rename(_log_file.CStr(), logfile.str().c_str());
				}

				// Open new file (filename.log.yymmdd)
				OpenNewFile(time);
			}
			_last_day = local_time.tm_mday;
		}

		_log_stream << log << std::endl;
		_log_stream.flush();
	}
}  // namespace ov