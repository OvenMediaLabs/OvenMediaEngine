//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "log_internal.h"

#include <inttypes.h>

#include <thread>

#include "platform.h"

#define OV_LOG_COLOR_RESET "\x1B[0m"

#define OV_LOG_COLOR_FG_BLACK "\x1B[30m"
#define OV_LOG_COLOR_FG_RED "\x1B[31m"
#define OV_LOG_COLOR_FG_GREEN "\x1B[32m"
#define OV_LOG_COLOR_FG_YELLOW "\x1B[33m"
#define OV_LOG_COLOR_FG_BLUE "\x1B[34m"
#define OV_LOG_COLOR_FG_MAGENTA "\x1B[35m"
#define OV_LOG_COLOR_FG_CYAN "\x1B[36m"
#define OV_LOG_COLOR_FG_WHITE "\x1B[37m"

#define OV_LOG_COLOR_FG_BR_BLACK "\x1B[90m"
#define OV_LOG_COLOR_FG_BR_RED "\x1B[91m"
#define OV_LOG_COLOR_FG_BR_GREEN "\x1B[92m"
#define OV_LOG_COLOR_FG_BR_YELLOW "\x1B[93m"
#define OV_LOG_COLOR_FG_BR_BLUE "\x1B[94m"
#define OV_LOG_COLOR_FG_BR_MAGENTA "\x1B[95m"
#define OV_LOG_COLOR_FG_BR_CYAN "\x1B[96m"
#define OV_LOG_COLOR_FG_BR_WHITE "\x1B[97m"

#define OV_LOG_COLOR_BG_BLACK "\x1B[40m"
#define OV_LOG_COLOR_BG_RED "\x1B[41m"
#define OV_LOG_COLOR_BG_GREEN "\x1B[42m"
#define OV_LOG_COLOR_BG_YELLOW "\x1B[43m"
#define OV_LOG_COLOR_BG_BLUE "\x1B[44m"
#define OV_LOG_COLOR_BG_MAGENTA "\x1B[45m"
#define OV_LOG_COLOR_BG_CYAN "\x1B[46m"
#define OV_LOG_COLOR_BG_WHITE "\x1B[47m"

#define OV_LOG_COLOR_BG_BR_BLACK "\x1B[100m"
#define OV_LOG_COLOR_BG_BR_RED "\x1B[101m"
#define OV_LOG_COLOR_BG_BR_GREEN "\x1B[102m"
#define OV_LOG_COLOR_BG_BR_YELLOW "\x1B[103m"
#define OV_LOG_COLOR_BG_BR_BLUE "\x1B[104m"
#define OV_LOG_COLOR_BG_BR_MAGENTA "\x1B[105m"
#define OV_LOG_COLOR_BG_BR_CYAN "\x1B[106m"
#define OV_LOG_COLOR_BG_BR_WHITE "\x1B[107m"

namespace ov
{
	LogInternal::LogInternal(std::string log_file_name) noexcept
		: _level(OVLogLevelTrace),
		  _log_file(log_file_name)
	{
	}

	LogInternal::~LogInternal()
	{
		_released = true;
	}

	void LogInternal::SetLogLevel(OVLogLevel level)
	{
		_level = level;
	}

	void LogInternal::ResetEnable()
	{
		if (_released)
		{
			return;
		}

		std::lock_guard<std::mutex> lock(_mutex);

		_enable_map.clear();
		_enable_list.clear();
	}

	bool LogInternal::IsEnabled(const char *tag, OVLogLevel level)
	{
		if (_released)
		{
			return false;
		}

		std::lock_guard<std::mutex> lock(_mutex);

		auto item = _enable_map.find(tag);

		if (item == _enable_map.cend())
		{
			// If there is no cached item, it finds a item in _enable_list that matches regular expression
			for (const auto &enable_item : _enable_list)
			{
				if (std::regex_match(tag, *(enable_item.regex.get())))
				{
					_enable_map[tag] = enable_item;

					break;
				}
			}

			item = _enable_map.find(tag);

			if (item == _enable_map.cend())
			{
				// If there is no match in the regular expression, info level is enabled (default)
				_enable_map[tag] = (EnableItem){
					.regex		= nullptr,
					.level		= OVLogLevelInformation,
					.is_enabled = true};
			}

			item = _enable_map.find(tag);

			if (item == _enable_map.cend())
			{
				// Item must be added
				OV_ASSERT2(false);
				return false;
			}
		}

		if (level >= item->second.level)
		{
			// Returns whether the log level for the tag is activated
			return item->second.is_enabled;
		}

		// Levels below level behave as opposed to being activated
		return (item->second.is_enabled == false);
	}

	bool LogInternal::SetEnable(const char *tag_regex, OVLogLevel level, bool is_enabled)
	{
		if (_released)
		{
			return false;
		}

		std::lock_guard<std::mutex> lock(_mutex);

		_enable_map.clear();

		try
		{
			// Find exists item
			auto item = std::find_if(_enable_list.begin(), _enable_list.end(), [tag_regex](const EnableItem &item) -> bool {
				return item.regex_string == tag_regex;
			});

			if (item == _enable_list.end())
			{
				auto reg_ex = std::make_shared<std::regex>(tag_regex);

				_enable_list.emplace_back((EnableItem){
					.regex		  = std::move(reg_ex),
					.level		  = level,
					.is_enabled	  = is_enabled,
					.regex_string = tag_regex});
			}
			else
			{
				item->level		 = level;
				item->is_enabled = is_enabled;
			}

			return true;
		}
		catch (const std::regex_error &e)
		{
			return false;
		}
	}

	void LogInternal::Log(bool show_format, OVLogLevel level, const char *tag, const char *file, int line, const char *method, const char *format, va_list &arg_list)
	{
		if (_released)
		{
			return;
		}

		if (level < _level)
		{
			// Disabled log level
			return;
		}

		if (tag == nullptr)
		{
			tag = "";
		}

		if (IsEnabled(tag, level) == false)
		{
			// Disabled log level for the tag
			return;
		}

		constexpr const char *log_level[] = {
			"T",
			"D",
			"I",
			"W",
			"E",
			"C"};

		constexpr const char *color_prefix[] = {
			OV_LOG_COLOR_FG_BR_BLACK,
			OV_LOG_COLOR_FG_CYAN,
			OV_LOG_COLOR_FG_WHITE,
			OV_LOG_COLOR_FG_YELLOW,
			OV_LOG_COLOR_FG_BR_RED,
			OV_LOG_COLOR_FG_BR_WHITE OV_LOG_COLOR_BG_RED};

		constexpr const char *color_suffix[] = {
			OV_LOG_COLOR_RESET,
			OV_LOG_COLOR_RESET,
			OV_LOG_COLOR_RESET,
			OV_LOG_COLOR_RESET,
			OV_LOG_COLOR_RESET,
			OV_LOG_COLOR_RESET};

		// Obtain current time in milliseconds
		auto current	 = std::chrono::system_clock::now();
		auto mseconds	 = std::chrono::duration_cast<std::chrono::milliseconds>(current.time_since_epoch()).count() % 1000;

		// Obtain current hours/minutes/seconds
		std::time_t time = std::time(nullptr);
		std::tm local_time{};
		::localtime_r(&time, &local_time);

		ov::String log;

#if OV_LOG_SHOW_FILE_NAME
		ov::String fileName = file;

		{
			ssize_t position = fileName.IndexOfRev('/');

			if (position >= 0)
			{
				fileName = fileName.Substring(position + 1);
			}
		}
#endif	// OV_LOG_SHOW_FILE_NAME

#if OV_LOG_SHOW_FUNCTION_NAME
		ov::String func = method;

		{
			int position = func.IndexOf('(');

			if (position >= 0)
			{
				func = func.Left(position);
			}

			position = func.IndexOfRev(' ');

			if (position >= 0)
			{
				func = func.Substring(position + 1);
			}
		}
#endif	// OV_LOG_SHOW_FUNCTION_NAME

		if (show_format)
		{
			auto tid  = ov::Platform::GetThreadId();
			auto name = ov::Platform::GetThreadName();

			// log format
			//  [<date> <time>] <tag> <log level> <thread id> | <message>
			log.Format(
				""
				// color
				"%s"
#if DEBUG
				// In DEBUG mode, the year is not displayed
				"["
#else	// DEBUG

				// date,
				"[%04d-"
#endif	// DEBUG

				// time ([mm-dd hh:mm:ss.sss])
				"%02d-%02d %02d:%02d:%02d.%03" PRId64 "]"

				// <log level>
				" %s"
				// <thread id>
				" [%s:%lu]"
				// tag
				"%s%s"
				// |
				" | "
#if OV_LOG_SHOW_FILE_NAME
				// File:Line
				"%s:%-4d | "
#endif	// OV_LOG_SHOW_FILE_NAME
#if OV_LOG_SHOW_FUNCTION_NAME
				// Method
				"%s() | "
#endif	// OV_LOG_SHOW_FUNCTION_NAME
				,
				"",
#if DEBUG
				local_time.tm_mon + 1, local_time.tm_mday,
#else	// DEBUG
				1900 + local_time.tm_year, local_time.tm_mon + 1, local_time.tm_mday,
#endif	// DEBUG
				local_time.tm_hour, local_time.tm_min, local_time.tm_sec, mseconds,
				log_level[level],
				name,
				tid,
				(tag[0] == '\0') ? "" : " ", tag

#if OV_LOG_SHOW_FILE_NAME
				,
				fileName.CStr(), line
#endif	// OV_LOG_SHOW_FILE_NAME
#if OV_LOG_SHOW_FUNCTION_NAME
				,
				func.CStr()
#endif	// OV_LOG_SHOW_FUNCTION_NAME
			);
		}

		// Append messages
		log.AppendVFormat(format, arg_list);
		if (show_format)
		{
			if (level < OVLogLevelWarning)
			{
				::fprintf(stdout, "%s%s%s\n", color_prefix[level], log.CStr(), color_suffix[level]);
				::fflush(stdout);
			}
			else
			{
				::fprintf(stderr, "%s%s%s\n", color_prefix[level], log.CStr(), color_suffix[level]);
				::fflush(stderr);
			}
		}

		_log_file.Write(log.CStr());
	}

	void LogInternal::SetLogPath(const char *log_path)
	{
		if (_released)
		{
			return;
		}

		_log_file.SetLogPath(log_path);
	}

	const char *LogInternal::GetLogPath() const
	{
		if (_released)
		{
			return "";
		}

		return _log_file.GetLogPath();
	}
}  // namespace ov
