//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "logger.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>

#include "sinks/sinks.h"

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

namespace ov::logger
{
	constexpr spdlog::level::level_enum LogLevelToSpdlogLevel(LogLevel level)
	{
		switch (level)
		{
			case LogLevel::Trace:
				return spdlog::level::trace;

			case LogLevel::Debug:
				return spdlog::level::debug;

			case LogLevel::Info:
				return spdlog::level::info;

			case LogLevel::Warn:
				return spdlog::level::warn;

			case LogLevel::Error:
				return spdlog::level::err;

			case LogLevel::Critical:
				return spdlog::level::critical;
		}

		return spdlog::level::off;
	}

	struct DailyFilenameCalculator
	{
		static spdlog::filename_t calc_filename(const spdlog::filename_t &filename, const tm &now_tm)
		{
			return spdlog::fmt_lib::format(
				SPDLOG_FMT_STRING(SPDLOG_FILENAME_T("{}.{:04d}-{:02d}-{:02d}")),
				filename, now_tm.tm_year + 1900, now_tm.tm_mon + 1, now_tm.tm_mday);
		}
	};

	std::shared_ptr<Logger> GetLogger(const char *tag, const LogOptions &options)
	{
		static std::mutex logger_map_mutex;
		static std::map<std::string, std::shared_ptr<Logger>> logger_map;

		std::lock_guard lock_guard(logger_map_mutex);

		{
			auto logger = logger_map.find(tag);

			if (logger != logger_map.end())
			{
				return logger->second;
			}
		}

		std::vector<spdlog::sink_ptr> sinks;

		if (options.to_stdout_err)
		{
			auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

			sink->set_color(spdlog::level::trace, OV_LOG_COLOR_FG_CYAN);
			sink->set_color(spdlog::level::debug, OV_LOG_COLOR_FG_CYAN);
			sink->set_color(spdlog::level::info, OV_LOG_COLOR_FG_WHITE);
			sink->set_color(spdlog::level::warn, OV_LOG_COLOR_FG_YELLOW);
			sink->set_color(spdlog::level::err, OV_LOG_COLOR_FG_RED);
			sink->set_color(spdlog::level::critical, OV_LOG_COLOR_FG_BR_WHITE OV_LOG_COLOR_BG_RED);

			sinks.push_back(sink);
		}

		std::optional<spdlog::spdlog_ex> error;

		if (options.to_file)
		{
			try
			{
				auto sink = sinks::DailyFileSink::Create(options.to_file.value());

				sinks.push_back(sink);
			}
			catch (const spdlog::spdlog_ex &ex)
			{
				error = ex;
			}
		}

		auto internal_logger = std::make_shared<spdlog::logger>(tag, sinks.begin(), sinks.end());

		// [2025-02-12 17:54:01.445] I [SPRTMP-t41935:423153] Publisher | stream.cpp:294  | [stream(1423176515)] LLHLS Publisher Application - All StreamWorker has been stopped

		// https://github.com/gabime/spdlog/wiki/3.-Custom-formatting#pattern-flags
		auto formatter		 = std::make_unique<spdlog::pattern_formatter>();
		formatter
			->add_flag<ThreadNamePatternFlag>(ThreadNamePatternFlag::FLAG)
			.set_pattern("%^[%Y-%m-%d %H:%M:%S.%e%z] %L [%N:%t] %n | %s:%-4# | %v%$");

		// TODO(dimiden): Need to initialize logger according to the contents of Logger.xml here
		internal_logger->set_formatter(std::move(formatter));

		auto logger		= std::make_shared<Logger>(internal_logger);

		logger_map[tag] = logger;

		if (error.has_value())
		{
			logger->Error(__FILE__, __LINE__, __PRETTY_FUNCTION__,
						  "Failed to create file for logging: {}", error->what());
		}

		return logger;
	}

	std::shared_ptr<Logger> GetLogger(const char *tag)
	{
		return GetLogger(tag, {});
	}

	void Logger::SetLogPattern(const char *pattern)
	{
		_logger->set_pattern(pattern);
	}

	void Logger::Log(LogLevel level, const char *file, int line, const char *function, const std::string &message)
	{
		_logger->log(spdlog::source_loc{file, line, function}, LogLevelToSpdlogLevel(level), message);
		_logger->flush();
	}
}  // namespace ov::logger
