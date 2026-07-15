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

#include <functional>
#include <iostream>

namespace ov::logger
{
	// The following code is to delegate processing to `MyFormatter.Parse()` when `fmt::formatter::parse()` is called.
	// If `MyFormatter` has a `Parse()` function, it calls `MyFormatter.Parse()`, otherwise it returns `ctx.begin()`.
	using ParseContext = fmt::format_parse_context;
	using ParseResult  = ParseContext::iterator;
	using Parser	   = std::function<ParseResult(ParseContext &)>;

	template <typename Tformatter>
	constexpr auto HasParse(int) -> decltype(std::declval<Tformatter>().Parse(std::declval<ParseContext &>()), std::true_type())
	{
		return std::true_type();
	}

	template <typename Tformatter>
	constexpr auto HasParse(...) -> std::false_type
	{
		return std::false_type();
	}

	template <typename Tformatter, std::enable_if_t<decltype(HasParse<Tformatter>(0))::value, int> = 0>
	constexpr ParseResult CallParse(Tformatter &formatter, ParseContext &ctx)
	{
		return formatter.Parse(ctx);
	}

	template <typename Tformatter, std::enable_if_t<!decltype(HasParse<Tformatter>(0))::value, int> = 0>
	constexpr ParseResult CallParse(Tformatter const &formatter, ParseContext &ctx)
	{
		return ctx.begin();
	}

	// The following code is to delegate processing to `MyFormatter.Format()` when `fmt::formatter::format()` is called.
	// If `MyFormatter` has a `Format()` function, it calls `MyFormatter.Format()`, otherwise it prints in the form of `<MyClass: 0x{:x}>`.
	using FormatContext = fmt::format_context;
	using FormatResult	= FormatContext::iterator;
	template <typename Tinstance>
	using Formatter = std::function<FormatResult(Tinstance const &instance, FormatContext &)>;

	template <typename Tformatter, typename Tinstance>
	constexpr auto HasFormat(int) -> decltype(std::declval<Tformatter>().Format(
												  std::declval<Tinstance &>(),
												  std::declval<FormatContext &>()),
											  std::true_type())
	{
		return std::true_type();
	}

	template <typename Tformatter, typename Tinstance>
	constexpr auto HasFormat(...) -> std::false_type
	{
		return std::false_type();
	}

	template <typename Tformatter, typename Tinstance, std::enable_if_t<decltype(HasFormat<Tformatter, Tinstance>(0))::value, int> = 0>
	constexpr FormatResult CallFormat(
		const char *instance_name,
		Tformatter const &formatter,
		Tinstance const &instance,
		FormatContext &ctx)
	{
		return formatter.Format(instance, ctx);
	}

	template <typename Tformatter, typename Tinstance, std::enable_if_t<!decltype(HasFormat<Tformatter, Tinstance>(0))::value, int> = 0>
	constexpr FormatResult CallFormat(
		const char *instance_name,
		Tformatter const &formatter,
		Tinstance const &instance,
		FormatContext &ctx)
	{
		// Split by '::' and get the last element as the class name to exclude the namespace
		auto class_name = instance_name;

		for (auto i = instance_name; *i; ++i)
		{
			if (*i == ':')
			{
				class_name = i + 1;
			}
		}

		if (class_name[0] == '\0')
		{
			class_name = instance_name;
		}

		return fmt::format_to(ctx.out(), "<{}: 0x{:x}>", class_name, reinterpret_cast<uintptr_t>(&instance));
	}
}  // namespace ov::logger

// To support custom class for logging
// https://github.com/gabime/spdlog/wiki/1.-QuickStart#log-user-defined-objects

// If `DECLARE_FORMATTER(MyFormatter, MyClass)` is declared, when `MyClass` is output to the log,
// `MyFormatter`'s `Parse()` and `Format()` functions are called.
// If `MyFormatter` does not have `Parse()` and `Format()` functions, the default behavior is performed.
#define DECLARE_CUSTOM_FORMATTER(formatter_name, for_class)                                       \
	template <>                                                                                   \
	class fmt::formatter<for_class>                                                               \
	{                                                                                             \
	public:                                                                                       \
		constexpr auto parse(format_parse_context &ctx)                                           \
		{                                                                                         \
			return ov::logger::CallParse<formatter_name>(_formatter, ctx);                        \
		}                                                                                         \
                                                                                                  \
		template <typename FmtContext>                                                            \
		constexpr auto format(for_class const &instance, FmtContext &ctx) const                   \
		{                                                                                         \
			return ov::logger::CallFormat<formatter_name>(#for_class, _formatter, instance, ctx); \
		}                                                                                         \
                                                                                                  \
		formatter_name _formatter;                                                                \
	}
