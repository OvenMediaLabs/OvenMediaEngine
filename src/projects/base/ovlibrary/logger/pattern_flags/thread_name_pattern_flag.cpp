//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./thread_name_pattern_flag.h"

#include "../../platform.h"
#include "../thread_helper.h"

namespace ov::logger
{
	void ThreadNamePatternFlag::format(const spdlog::details::log_msg &msg, const std::tm &, spdlog::memory_buf_t &dest)
	{
		AppendThreadName(msg.thread_id, dest);
	}

	std::unique_ptr<spdlog::custom_flag_formatter> ThreadNamePatternFlag::clone() const
	{
		return spdlog::details::make_unique<ThreadNamePatternFlag>();
	}

	void ThreadNamePatternFlag::AppendThreadName(size_t thread_id, spdlog::memory_buf_t &dest)
	{
		auto pthread_id = ThreadHelper::GetPthreadId(thread_id);
		auto name		= Platform::GetThreadName(pthread_id);

		dest.append(name, name + ::strlen(name));
	}
}  // namespace ov::logger
	