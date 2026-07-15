//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Benjamin
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <atomic>
#include <fstream>
#include "./tsa/mutex.h"
#include "./string.h"

#define OV_LOG_DIR "logs"
#define OV_LOG_DIR_SVC "/var/log/ovenmediaengine"
#define OV_DEFAULT_LOG_FILE "ovenmediaengine.log"

namespace ov
{
	class LogWrite
	{
	public:
		LogWrite(String log_file_name, bool include_date_in_filename = false);
		virtual ~LogWrite() = default;
		void Write(const char *log, std::time_t time = 0);
		void SetLogPath(const char *log_path);
		String GetLogPath() const;
		void SetEnabled(bool enabled);

		static void SetAsService(bool start_service);

	private:
		void OpenNewFile(std::time_t time = 0) OV_REQUIRES(_log_stream_mutex);
		void SetLogPathInternal(const char *log_path) OV_REQUIRES(_log_stream_mutex);

		mutable Mutex _log_stream_mutex;
		std::ofstream _log_stream OV_GUARDED_BY(_log_stream_mutex);
		int _last_day OV_GUARDED_BY(_log_stream_mutex);
		String _log_path OV_GUARDED_BY(_log_stream_mutex);
		String _log_file_name OV_GUARDED_BY(_log_stream_mutex);
		String _log_file OV_GUARDED_BY(_log_stream_mutex);
		bool _include_date_in_filename = false;
		std::atomic<bool> _enabled{true};
		static inline std::atomic<bool> _start_service{false};
	};
}  // namespace ov
