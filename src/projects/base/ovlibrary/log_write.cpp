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
	bool LogWrite::_start_service = false;

	LogWrite::LogWrite(std::string log_file_name, bool include_date_in_filename)
		: _last_day(0),
		  _log_path(OV_LOG_DIR)
	{
		if (log_file_name.empty())
		{
			_log_file_name = OV_DEFAULT_LOG_FILE;
		}
		else
		{
			_log_file_name = log_file_name;
		}

		_log_file = _log_path + std::string("/") + log_file_name;
		_include_date_in_filename = include_date_in_filename;
	}

	void LogWrite::SetLogPath(const char *log_path)
	{
		_log_path = log_path;
		_log_file = log_path + std::string("/") + _log_file_name;
	}

	const char *LogWrite::GetLogPath() const
	{
		return _log_path.c_str();
	}

	void LogWrite::OpenNewFile(std::time_t time)
	{
		if (_start_service)
		{
			SetLogPath(OV_LOG_DIR_SVC);

			// Change default log path to /var once for running service
			_start_service = false;
		}

		if ((::mkdir(_log_path.c_str(), 0755) == -1) && errno != EEXIST)
		{
			return;
		}

		std::lock_guard<std::mutex> lock_guard(_log_stream_mutex);
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
			_log_stream.open(_log_file, std::ofstream::out | std::ofstream::app);
		}
	}

	void LogWrite::SetAsService(bool start_service)
	{
		_start_service = start_service;
	}

	void LogWrite::Write(const char *log, std::time_t time)
	{
		if (time == 0)
		{
			time = std::time(nullptr);
		}
		std::tm local_time{};
		::localtime_r(&time, &local_time);

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
					::rename(_log_file.c_str(), logfile.str().c_str());
				}

				// Open new file (filename.log.yymmdd)
				OpenNewFile(time);
			}
			_last_day = local_time.tm_mday;
		}

		std::lock_guard<std::mutex> lock_guard(_log_stream_mutex);
		_log_stream << log << std::endl;
		_log_stream.flush();
	}
}  // namespace ov