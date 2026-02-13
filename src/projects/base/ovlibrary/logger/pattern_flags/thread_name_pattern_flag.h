//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include <spdlog/pattern_formatter.h>

namespace ov::logger
{
	// https://github.com/gabime/spdlog/wiki/3.-Custom-formatting#extending-spdlog-with-your-own-flags
	class ThreadNamePatternFlag : public spdlog::custom_flag_formatter
	{
	public:
		constexpr static const char FLAG = 'N';

		//--------------------------------------------------------------------
		// Overriding of spdlog::custom_flag_formatter
		//--------------------------------------------------------------------
		void format(const spdlog::details::log_msg &msg, const std::tm &, spdlog::memory_buf_t &dest) override;
		std::unique_ptr<custom_flag_formatter> clone() const override;

	protected:
		void AppendThreadName(size_t thread_id, spdlog::memory_buf_t &dest);
	};
}  // namespace ov::logger
