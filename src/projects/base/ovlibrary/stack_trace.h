//==============================================================================
//
//  OvenMediaEngine
//
//  Created by benjamin
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#include <csignal>

#include "./string.h"

#pragma once

namespace ov
{
	class StackTrace
	{
	public:
		StackTrace() = delete;

		static String GetStackTrace(int offset = 0, int line_count = -1);
		static String GetRegisters(const ucontext_t *ucontext);

	private:
		struct ParseResult
		{
			char *module_name = nullptr;
			char *address = nullptr;
			char *function_name = nullptr;
			char *demangled_function_name = nullptr;
			char *offset = nullptr;
		};

		static String GetStackTraceInternal(int offset = 2, int line_count = -1);

		static bool ParseLinuxStyleLine(char *line, ParseResult *parse_result);
		static bool ParseMacOsStyleLine(char *line, ParseResult *parse_result);
	};
}  // namespace ov