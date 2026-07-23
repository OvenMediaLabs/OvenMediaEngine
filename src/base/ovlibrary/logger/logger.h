//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <spdlog/fmt/bundled/format.h>

#include <cstdarg>
#include <map>
#include <memory>
#include <optional>

#include "./pattern_flags/pattern_flags.h"
#include "./thread_helper.h"
#include "formatters/formatters.h"

// Forward declaration of spdlog::logger
namespace spdlog
{
	class logger;
}  // namespace spdlog

namespace ov::logger
{
	enum class LogLevel
	{
		Trace,
		Debug,
		Info,
		Warn,
		Error,
		Critical,
	};

	struct LogOptions
	{
		bool to_stdout_err = true;
		std::optional<ov::String> to_file;
	};

	class Logger
	{
	public:
		Logger(std::shared_ptr<spdlog::logger> logger)
			: _logger(logger)
		{
		}

		// Since the Logger is configured with spdlog, please refer to spdlog's pattern flags for more details.
		//
		// https://github.com/gabime/spdlog/wiki/3.-Custom-formatting#pattern-flags
		//
		// | flag     | meaning                                                                                    | example                                                    |
		// +----------+--------------------------------------------------------------------------------------------+------------------------------------------------------------+
		// | %v       | The actual text to log                                                                     | some user text"                                            |
		// | %t       | Thread id                                                                                  | 1232"                                                      |
		// | %P       | Process id                                                                                 | 3456"                                                      |
		// | %n       | Logger's name                                                                              | some logger name"                                          |
		// | %l       | The log level of the message                                                               | debug", "info", etc                                        |
		// | %L       | Short log level of the message                                                             | D", "I", etc                                               |
		// | %a       | Abbreviated weekday name                                                                   | Thu"                                                       |
		// | %A       | Full weekday name                                                                          | Thursday"                                                  |
		// | %b       | Abbreviated month name                                                                     | Aug"                                                       |
		// | %B       | Full month name                                                                            | August"                                                    |
		// | %c       | Date and time representation                                                               | Thu Aug 23 15:35:46 2014"                                  |
		// | %C       | Year in 2 digits                                                                           | 14"                                                        |
		// | %Y       | Year in 4 digits                                                                           | 2014"                                                      |
		// | %D or %x | Short MM/DD/YY date                                                                        | 08/23/14"                                                  |
		// | %m       | Month 01-12                                                                                | 11"                                                        |
		// | %d       | Day of month 01-31                                                                         | 29"                                                        |
		// | %H       | Hours in 24 format 00-23                                                                   | 23"                                                        |
		// | %I       | Hours in 12 format 01-12                                                                   | 11"                                                        |
		// | %M       | Minutes 00-59                                                                              | 59"                                                        |
		// | %S       | Seconds 00-59                                                                              | 58"                                                        |
		// | %e       | Millisecond part of the current second 000-999                                             | 678"                                                       |
		// | %f       | Microsecond part of the current second 000000-999999                                       | 056789"                                                    |
		// | %F       | Nanosecond part of the current second 000000000-999999999                                  | 256789123"                                                 |
		// | %p       | AM/PM                                                                                      | AM"                                                        |
		// | %r       | 12 hour clock                                                                              | 02:55:02 PM"                                               |
		// | %R       | 24-hour HH:MM time, equivalent to %H:%M                                                    | 23:55"                                                     |
		// | %T or %X | ISO 8601 time format (HH:MM:SS), equivalent to %H:%M:%S                                    | 23:55:59"                                                  |
		// | %z       | ISO 8601 offset from UTC in timezone ([+/-]HH:MM)                                          | "+02:00"                                                   |
		// | %E       | Seconds since the epoch                                                                    | 1528834770"                                                |
		// | %%       | The % sign                                                                                 | "%"                                                        |
		// | %+       | spdlog's default format                                                                    | "[2014-10-31 23:46:59.678] [mylogger] [info] Some message" |
		// | %^       | start color range (can be used only once)                                                  | "[mylogger] [info(green)] Some message"                    |
		// | %$       | end color range (for example %^[+++]%$ %v) (can be used only once)                         | [+++] Some message                                         |
		// | %@       | Source file and line (use SPDLOG_TRACE(..),                                                | some/dir/my_file.cpp:123                                   |
		// |          | SPDLOG_INFO(...) etc. instead of spdlog::trace(...)) Same as %g:%#                         |                                                            |
		// | %s       | Basename of the source file (use SPDLOG_TRACE(..), SPDLOG_INFO(...) etc.)                  | my_file.cpp                                                |
		// | %g       | Full or relative path of the source file as appears in                                     | some/dir/my_file.cpp                                       |
		// |          | spdlog::source_loc (use SPDLOG_TRACE(..), SPDLOG_INFO(...) etc.)                           |                                                            |
		// | %#       | Source line (use SPDLOG_TRACE(..), SPDLOG_INFO(...) etc.)                                  | 123                                                        |
		// | %!       | Source function (use SPDLOG_TRACE(..), SPDLOG_INFO(...) etc. see tweakme for pretty-print) | my_func                                                    |
		// | %o       | Elapsed time in milliseconds since previous message                                        | 456                                                        |
		// | %i       | Elapsed time in microseconds since previous message                                        | 456                                                        |
		// | %u       | Elapsed time in nanoseconds since previous message                                         | 11456                                                      |
		// | %O       | Elapsed time in seconds since previous message                                             | 4                                                          |
		void SetLogPattern(const char *pattern);

		template <typename... Targs>
		inline void Trace(const char *file, int line, const char *function, const char *format, Targs &&...args)
		{
			return Log(LogLevel::Trace, file, line, function, fmt::vformat(format, fmt::make_format_args(args...)));
		}

		template <typename... Targs>
		inline void Debug(const char *file, int line, const char *function, const char *format, Targs &&...args)
		{
			return Log(LogLevel::Debug, file, line, function, fmt::vformat(format, fmt::make_format_args(args...)));
		}

		template <typename... Targs>
		inline void Info(const char *file, int line, const char *function, const char *format, Targs &&...args)
		{
			return Log(LogLevel::Info, file, line, function, fmt::vformat(format, fmt::make_format_args(args...)));
		}

		template <typename... Targs>
		inline void Warn(const char *file, int line, const char *function, const char *format, Targs &&...args)
		{
			return Log(LogLevel::Warn, file, line, function, fmt::vformat(format, fmt::make_format_args(args...)));
		}

		template <typename... Targs>
		inline void Error(const char *file, int line, const char *function, const char *format, Targs &&...args)
		{
			return Log(LogLevel::Error, file, line, function, fmt::vformat(format, fmt::make_format_args(args...)));
		}

		template <typename... Targs>
		inline void Critical(const char *file, int line, const char *function, const char *format, Targs &&...args)
		{
			return Log(LogLevel::Critical, file, line, function, fmt::vformat(format, fmt::make_format_args(args...)));
		}

	protected:
		void Log(LogLevel level, const char *file, int line, const char *function, const std::string &message);

	private:
		std::shared_ptr<spdlog::logger> _logger;
	};

	std::shared_ptr<Logger> GetLogger(const char *tag, const LogOptions &options);
	std::shared_ptr<Logger> GetLogger(const char *tag);
}  // namespace ov::logger
