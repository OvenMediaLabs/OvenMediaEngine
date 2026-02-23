//==============================================================================
//
//  OvenMediaEngine
//
//  Created by benjamin
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "stack_trace.h"

#include <cxxabi.h>
#include <errno.h>
#include <execinfo.h>
#include <stdlib.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <cinttypes>

#include "platform.h"

namespace ov
{
	String instance_version;

	String StackTrace::GetStackTrace(int offset, int line_count)
	{
		return GetStackTraceInternal(2 + offset, line_count);
	}

	String StackTrace::GetRegisters(const ucontext_t *ucontext)
	{
		ov::String registers;

#if !IS_ARM
#	if IS_64BITS
#		define APPEND_X86_64_REGISTER(name) registers.AppendFormat("%-6s: 0x%-20" PRIx64 " %" PRId64 "\n", #name, ucontext->uc_mcontext.gregs[REG_##name], ucontext->uc_mcontext.gregs[REG_##name])
#		define APPEND_X86_64_EFLAGS(condition, name) \
			if (condition)                            \
			{                                         \
				registers.AppendFormat("%s ", name);  \
			}

		{
			APPEND_X86_64_REGISTER(RAX);
			APPEND_X86_64_REGISTER(RBX);
			APPEND_X86_64_REGISTER(RCX);
			APPEND_X86_64_REGISTER(RDX);
			APPEND_X86_64_REGISTER(RSI);
			APPEND_X86_64_REGISTER(RDI);
			APPEND_X86_64_REGISTER(RBP);
			APPEND_X86_64_REGISTER(RSP);
			APPEND_X86_64_REGISTER(R8);
			APPEND_X86_64_REGISTER(R9);
			APPEND_X86_64_REGISTER(R10);
			APPEND_X86_64_REGISTER(R11);
			APPEND_X86_64_REGISTER(R12);
			APPEND_X86_64_REGISTER(R13);
			APPEND_X86_64_REGISTER(R14);
			APPEND_X86_64_REGISTER(R15);
			APPEND_X86_64_REGISTER(RIP);

			auto efl = ucontext->uc_mcontext.gregs[REG_EFL];
			registers.AppendFormat("EFLAGS: 0x%-20" PRIx64 " [ ", efl, efl);
			APPEND_X86_64_EFLAGS(efl & (1 << 0), "CF");
			APPEND_X86_64_EFLAGS(efl & (1 << 2), "PF");
			APPEND_X86_64_EFLAGS(efl & (1 << 4), "AF");
			APPEND_X86_64_EFLAGS(efl & (1 << 6), "ZF");
			APPEND_X86_64_EFLAGS(efl & (1 << 7), "SF");
			APPEND_X86_64_EFLAGS(efl & (1 << 8), "TF");
			APPEND_X86_64_EFLAGS(efl & (1 << 9), "IF");
			APPEND_X86_64_EFLAGS(efl & (1 << 10), "DF");
			APPEND_X86_64_EFLAGS(efl & (1 << 11), "OF");
			APPEND_X86_64_EFLAGS((efl >> 12) & 0x3, "IOPL");
			APPEND_X86_64_EFLAGS(efl & (1 << 14), "NT");
			APPEND_X86_64_EFLAGS(efl & (1 << 16), "RF");
			APPEND_X86_64_EFLAGS(efl & (1 << 17), "VM");
			APPEND_X86_64_EFLAGS(efl & (1 << 18), "AC");
			APPEND_X86_64_EFLAGS(efl & (1 << 19), "VIF");
			APPEND_X86_64_EFLAGS(efl & (1 << 20), "VIP");
			APPEND_X86_64_EFLAGS(efl & (1 << 21), "ID");
			registers.Append(']');
		}
#	else  // IS_64BITS
#		define APPEND_X86_REGISTER(name) registers.AppendFormat("%-6s: 0x%-20x (%d)\n", #name, ucontext->uc_mcontext.gregs[REG_##name], ucontext->uc_mcontext.gregs[REG_##name])
#		define APPEND_X86_EFLAGS(condition, name)   \
			if (condition)                           \
			{                                        \
				registers.AppendFormat("%s ", name); \
			}

		{
			APPEND_X86_REGISTER(EAX);
			APPEND_X86_REGISTER(EBX);
			APPEND_X86_REGISTER(ECX);
			APPEND_X86_REGISTER(EDX);
			APPEND_X86_REGISTER(ESI);
			APPEND_X86_REGISTER(EDI);
			APPEND_X86_REGISTER(EBP);
			APPEND_X86_REGISTER(ESP);

			auto efl = ucontext->uc_mcontext.gregs[REG_EFL];
			registers.AppendFormat("EFLAGS : 0x%-20x [ ", efl, efl);
			APPEND_X86_EFLAGS(efl & (1 << 0), "CF");
			APPEND_X86_EFLAGS(efl & (1 << 2), "PF");
			APPEND_X86_EFLAGS(efl & (1 << 4), "AF");
			APPEND_X86_EFLAGS(efl & (1 << 6), "ZF");
			APPEND_X86_EFLAGS(efl & (1 << 7), "SF");
			APPEND_X86_EFLAGS(efl & (1 << 8), "TF");
			APPEND_X86_EFLAGS(efl & (1 << 9), "IF");
			APPEND_X86_EFLAGS(efl & (1 << 10), "DF");
			APPEND_X86_EFLAGS(efl & (1 << 11), "OF");
			APPEND_X86_EFLAGS((efl >> 12) & 0x3, "IOPL");
			APPEND_X86_EFLAGS(efl & (1 << 14), "NT");
			APPEND_X86_EFLAGS(efl & (1 << 16), "RF");
			APPEND_X86_EFLAGS(efl & (1 << 17), "VM");
			APPEND_X86_EFLAGS(efl & (1 << 18), "AC");
			APPEND_X86_EFLAGS(efl & (1 << 19), "VIF");
			APPEND_X86_EFLAGS(efl & (1 << 20), "VIP");
			APPEND_X86_EFLAGS(efl & (1 << 21), "ID");
			registers.Append(']');
		}
#	endif	// IS_64BITS
#else		// !IS_ARM
		registers = "(Dumping registers is not supported on ARM)";
#endif		// !IS_ARM
		return registers;
	}

	bool StackTrace::ParseLinuxStyleLine(char *line, ParseResult *parse_result)
	{
		char *begin_name = nullptr;
		char *begin_offset = nullptr;
		char *end_offset = nullptr;
		char *begin_address = nullptr;
		char *end_address = nullptr;

		//
		// Try to parse the linux style backtrace line
		//
		// For example:
		// ./StackTraceTest(_Z16TemplateFunctionIiET_v+0x9) [0x402c87]
		//   |             |                          |   | |        |
		//  line      begin_name        begin_offset--+   | |        |
		//                                    end_offset--+ |        |
		//                                   begin_address--+        +--end_address
		//
		// Skip "./" prefix if exists
		if ((line[0] == '.') && (line[1] == '/'))
		{
			line += 2;
		}

		char *current = line;

		while (*current != '\0')
		{
			switch (*current)
			{
				case '(':
					begin_name = current;
					break;

				case '+':
					if (begin_name != nullptr)
					{
						begin_offset = current;
					}
					break;

				case ')':
					if (begin_name != nullptr)
					{
						end_offset = current;
					}
					break;

				case '[':
					begin_address = current;
					break;

				case ']':
					end_address = current;
					break;

				default:
					break;
			}

			++current;
		}

		if ((begin_name == nullptr) && (begin_address == nullptr))
		{
			// Does not seem to be linux-style line
			return false;
		}

		parse_result->module_name = nullptr;
		parse_result->address = nullptr;
		parse_result->function_name = nullptr;
		parse_result->demangled_function_name = nullptr;
		parse_result->offset = nullptr;

		if ((begin_name != nullptr) && (end_offset != nullptr))
		{
			*begin_name++ = '\0';
			if (begin_offset != nullptr)
			{
				*begin_offset++ = '\0';
			}
			*end_offset++ = '\0';

			// Before:
			// StackTraceTest(_Z16TemplateFunctionIiET_v+0x9) [0x402c87]
			//               |                          |   |
			//          begin_name        begin_offset--+   |
			//                                  end_offset--+
			// After (! means nullptr):
			// StackTraceTest!_Z16TemplateFunctionIiET_v!0x9! [0x402c87]
			//                |                          |   |
			//            begin_name       begin_offset--+   |
			//                                   end_offset--+

			// Demangle the name
			int status = 0;
			parse_result->function_name = begin_name;
			parse_result->demangled_function_name = abi::__cxa_demangle(begin_name, nullptr, nullptr, &status);
			parse_result->offset = begin_offset;
		}

		if ((begin_address != nullptr) && (end_address != nullptr))
		{
			*begin_address++ = '\0';
			*end_address++ = '\0';

			// Before:
			// StackTraceTest!_Z16TemplateFunctionIiET_v!0x9! [0x402c87]
			//                                                |        |
			//                                 begin_address--+        +--end_address
			// After (! means nullptr):
			// StackTraceTest!_Z16TemplateFunctionIiET_v!0x9! !0x402c87!
			//                                                 |        |
			//                                  begin_address--+        +--end_address
			parse_result->address = begin_address;
		}

		parse_result->module_name = line;

		return true;
	}

	bool StackTrace::ParseMacOsStyleLine(char *line, ov::StackTrace::ParseResult *parse_result)
	{
		char *begin_name = nullptr;
		char *begin_offset = nullptr;
		char *begin_address = nullptr;

		//
		// Try to parse the macOS style backtrace line
		//
		// For example:
		// 2   StackTraceTest                      0x000000010c4ae2f8 _ZN2ov10StackTrace9ShowTraceEv + 1234
		//     |                                   |                  |                                |
		//    line                  begin_address--+      begin_name--+                  begin_offset--+
		//

		// Since the module name may contain spaces, it must be parsed from the back

		int token_count = 0;
		char *current = line;

		// Trim the line

		// Move to end of the "line"
		while (*current != '\0')
		{
			current++;
		}

		while (current > line)
		{
			if ((*current != ' ') && (*current != '\0'))
			{
				break;
			}

			*current-- = '\0';
		}

		// Extract "begin_address" and "begin_name" and "begin_offset" from the "line"
		while (current > line)
		{
			if (*current == ' ')
			{
				switch (token_count)
				{
					case 0:
						begin_offset = current + 1;
						break;

					case 1:
						// +
						break;

					case 2:
						begin_name = current + 1;
						break;

					case 3:
						begin_address = current + 1;

					default:
						break;
				}

				token_count++;
				*current = '\0';
			}

			if (token_count == 4)
			{
				break;
			}

			current--;
		}

		if (token_count != 4)
		{
			return false;
		}

		// Find the module name
		current = line;
		token_count = 0;

		while (*current != '\0')
		{
			if (*current == ' ')
			{
				token_count++;
				current++;

				continue;
			}

			if (token_count > 0)
			{
				break;
			}

			current++;
		}

		if (token_count == 0)
		{
			// Cannot find module name
			return false;
		}

		parse_result->module_name = current;
		parse_result->address = begin_address;
		// Demangle the name
		int status = 0;
		parse_result->function_name = begin_name;
		parse_result->demangled_function_name = abi::__cxa_demangle(begin_name, nullptr, nullptr, &status);
		parse_result->offset = begin_offset;

		return true;
	}

	String StackTrace::GetStackTraceInternal(int offset, int line_count)
	{
		void *addr_list[64];

		String log;

		int buffer_size = ::backtrace(addr_list, sizeof(addr_list) / sizeof(addr_list[0]));

		if (buffer_size == 0)
		{
			return "";
		}

		char **symbol_list = ::backtrace_symbols(addr_list, buffer_size);
		int count = (line_count >= 0) ? std::min(line_count + offset, buffer_size) : buffer_size;

		// Called by signal handler (AbortHandler -> GetStackTrace -> GetStackTraceInternal):
		// #0: GetStackTraceInternal()
		// #1: GetStackTrace()
		// #2: AbortHandler()

		// Called by GetStackTrace (GetStackTrace -> GetStackTraceInternal);
		// #0: GetStackTraceInternal()
		// #1: GetStackTrace()
		for (int i = offset; i < count; ++i)
		{
			char *line = symbol_list[i];

			if (line[0] == '\0')
			{
				// empty line
				continue;
			}

			ParseResult parse_result;
			bool result = false;

			result = result || ParseLinuxStyleLine(line, &parse_result);
			result = result || ParseMacOsStyleLine(line, &parse_result);

			if (log.IsEmpty() == false)
			{
				log.Append('\n');
			}

			if (result)
			{
				const char *module_name = ((parse_result.module_name == nullptr) || (parse_result.module_name[0] == '\0')) ? "?" : parse_result.module_name;
				const char *name = (parse_result.demangled_function_name == nullptr) ? parse_result.function_name : parse_result.demangled_function_name;

				if ((name == nullptr) || (name[0] == '\0'))
				{
					name = "?";
				}

				log.AppendFormat("#%-3d %-35s %s %s + %s",
								 (i - offset),
								 module_name,
								 (parse_result.address == nullptr) ? "?" : parse_result.address,
								 name,
								 (parse_result.offset == nullptr) ? "0x0" : parse_result.offset);
			}
			else
			{
				log.AppendFormat("#%-3d || %s", (i - offset), line);
			}

			if (parse_result.demangled_function_name != nullptr)
			{
				::free(parse_result.demangled_function_name);
			}
		}

		::free(symbol_list);

		return log;
	}
}  // namespace ov
