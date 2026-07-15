//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#if defined(__clang__) || defined(__GNUC__)
/// Defines a printf-style format attribute for functions.
///
/// @param fmt_index position of format string (1-based)
/// @param first_arg position of first variable argument (1-based)
///
/// @note This macro is supported only on GCC and Clang.
/// @note If this macro is used in a class member function, since 'this' is the first argument, `fmt_index` and `first_arg` should be increased by `1`
#	define OV_PRINTF_FORMAT(fmt_index, first_arg) __attribute__((format(printf, fmt_index, first_arg)))
#else
#	define OV_PRINTF_FORMAT(fmt_index, first_arg)
#endif
