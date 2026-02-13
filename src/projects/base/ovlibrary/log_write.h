//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Benjamin
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <fstream>
#include <mutex>

#define OV_LOG_DIR "logs"
#define OV_LOG_DIR_SVC "/var/log/ovenmediaengine"
#define OV_DEFAULT_LOG_FILE "ovenmediaengine.log"

namespace ov
{
	class LogWrite
	{
	public:
		LogWrite(std::string log_file_name, bool include_date_in_filename = false);
		virtual ~LogWrite() = default;
		void Write(const char *log, std::time_t time = 0);
		void SetLogPath(const char *log_path);
		const char *GetLogPath() const;

		static void SetAsService(bool start_service);

	private:
		void OpenNewFile(std::time_t time = 0);

		std::mutex _log_stream_mutex;
		std::ofstream _log_stream;
		int _last_day;
		std::string _log_path;
		std::string _log_file_name;
		std::string _log_file;
		bool _include_date_in_filename = false;
		static bool _start_service;
	};
}  // namespace ov
