//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#define __STDC_FORMAT_MACROS 1

#include <inttypes.h>

#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <string>

#include "./json.h"
#include "./string.h"

namespace ov
{
	class Converter
	{
	public:
		Converter() = delete;
		~Converter() = delete;

		static ov::String ToString(bool flag);
		static ov::String ToString(int number);
		static ov::String ToString(const char *str);
		static ov::String ToString(unsigned int number);
		static ov::String ToString(int64_t number);
		static ov::String ToString(uint64_t number);
		// Due to conflict between size_t and uint64_t in linux, make sure that the OS is only active when macOS
#if defined(__APPLE__)
		static ov::String ToString(size_t number);
#endif	// defined(__APPLE__)
		static ov::String ToString(float number);
		static ov::String ToString(double number);
		static ov::String ToString(const ov::JsonObject &object);
		static ov::String ToString(const ::Json::Value &value);
		static ov::String ToString(const std::chrono::system_clock::time_point &tp);

		static ov::String ToISO8601String(const std::chrono::system_clock::time_point &tp);
		// From ISO8601 string to time_point
		static std::chrono::system_clock::time_point FromISO8601(const ov::String &iso8601_string);

		// HTTP Date format
		static String ToRFC7231String(const std::chrono::system_clock::time_point &tp);

		static ov::String ToSiString(int64_t number, int precision);

		static ov::String BitToString(int64_t bits);
		static ov::String BytesToString(int64_t bytes);

		static std::time_t ToTime(uint32_t year, uint32_t mon, uint32_t day, uint32_t hour, uint32_t min, bool isdst);

		static int32_t ToInt32(const char *str, int base = 10);
		static int32_t ToInt32(const ::Json::Value &value, int base = 10);

		static uint16_t ToUInt16(const char *str, int base = 10);
		static uint32_t ToUInt32(const char *str, int base = 10);

		static uint32_t ToUInt32(const ::Json::Value &value, int base = 10);

		static int64_t ToInt64(const char *str, int base = 10);
		static int64_t ToInt64(const ::Json::Value &value, int base = 10);
		static uint64_t ToUInt64(const char *str, int base = 10);

		static bool ToBool(const char *str);
		static bool ToBool(const ::Json::Value &value);

		static float ToFloat(const char *str);
		static float ToFloat(const ::Json::Value &value);

		static double ToDouble(const char *str);
		static double ToDouble(const ::Json::Value &value);

		static uint64_t SecondsToNtpTs(double seconds);
		static double NtpTsToSeconds(uint64_t ntp_timestamp);
		static double NtpTsToSeconds(uint32_t msw, uint32_t lsw);

		static uint32_t ToSynchSafe(uint32_t value);

		// Rescale timestamp value from one timescale to another
		static int64_t Rescale(int64_t value, int64_t to_timescale, int64_t from_timescale);
	};
}  // namespace ov
