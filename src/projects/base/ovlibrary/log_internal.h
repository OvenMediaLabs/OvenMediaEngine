//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <regex>
#include <unordered_map>

#include "./assert.h"
#include "./log.h"
#include "./log_write.h"
#include "./string.h"

#if DEBUG
#	define OV_LOG_SHOW_FILE_NAME 1
#	define OV_LOG_SHOW_FUNCTION_NAME 0
#else  // DEBUG
#	define OV_LOG_SHOW_FILE_NAME 1
#	define OV_LOG_SHOW_FUNCTION_NAME 0
#endif	// DEBUG

namespace ov
{
	class LogInternal
	{
	public:
		LogInternal(std::string log_file_name) noexcept;
		~LogInternal();

		/// First filtering rule applied to all logs
		///
		/// @param level Log level to display
		void SetLogLevel(OVLogLevel level);
		void ResetEnable();
		bool IsEnabled(const char *tag, OVLogLevel level);

		/// @param tag_regex pattern of a tag
		/// @param level Log level to display for tag
		/// @param is_enabled Activated or not
		///
		/// @remarks
		/// tag_regex = "App\..+" means "App.Hello", "App.World", "App.foo", "App.bar", ....
		/// (Reference: http://www.cplusplus.com/reference/regex/ECMAScript)
		///
		/// The tag_regex entered last has a higher priority.
		/// If the tag_regex is the same but another level is set, the first level is applied.
		/// Example 1) If the level is debug and is_enabled is false, ov::Log doesn't display the logs from debug to critical level.
		/// Example 2) If the level is warning and is_enabled is false, ov::Log doesn't display the logs from warning to critical level.
		/// Example 3) If the level is warning and is_enabled is true, ov::Log doesn't display the logs from debug to information, and it displays the logs from warning to critical level.
		/// Example 4) If the level is info and is_enabled is true, ov::Log doesn't display the debug logs, and it displays the logs from information to critical level.
		bool SetEnable(const char *tag_regex, OVLogLevel level, bool is_enabled);

		void Log(bool show_format, OVLogLevel level, const char *tag, const char *file, int line, const char *method, const char *format, va_list &arg_list);

		void SetLogPath(const char *log_path);
		const char *GetLogPath() const;

	protected:
		// This variable is used to avoid the problem of referencing incorrect heap if the log is written after LogInternal instance is released.
		// This situation occurs when the LogInternal instance declared static is disabled just before the OME is terminated and then logs are written by another module.
		bool _released = false;

		OVLogLevel _level;

		std::mutex _mutex;

		LogWrite _log_file;

		struct EnableItem
		{
			std::shared_ptr<std::regex> regex;
			OVLogLevel level;
			bool is_enabled;
			ov::String regex_string;
		};

		std::vector<EnableItem> _enable_list;

		// This map used for cache (It reduces regex matching cost)
		// key: tag
		// value: is_enabled
		std::unordered_map<ov::String, EnableItem> _enable_map;
	};
}  // namespace ov
