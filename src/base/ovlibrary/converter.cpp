//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./converter.h"

#include <regex>

namespace ov
{
	String Converter::ToString(bool flag)
	{
		return flag ? "true" : "false";
	}

	String Converter::ToString(int number)
	{
		return String::FormatString("%d", number);
	}

	String Converter::ToString(const char *str)
	{
		return str;
	}

	String Converter::ToString(unsigned int number)
	{
		return String::FormatString("%u", number);
	}

	String Converter::ToString(int64_t number)
	{
		return String::FormatString("%" PRId64, number);
	}

	String Converter::ToString(uint64_t number)
	{
		return String::FormatString("%" PRIu64, number);
	}

// Due to conflict between size_t and uint64_t in linux, make sure that the OS is only active when macOS
#if defined(__APPLE__)
	String Converter::ToString(size_t number)
	{
		return String::FormatString("%zu", number);
	}
#endif	// defined(__APPLE__)

	String Converter::ToString(float number)
	{
		return String::FormatString("%f", number);
	}

	String Converter::ToString(double number)
	{
		return String::FormatString("%f", number);
	}

	String Converter::ToString(const JsonObject &object)
	{
		return object.ToString();
	}

	String Converter::ToString(const ::Json::Value &value)
	{
		if (value.isNull())
		{
			return "";
		}

		if (value.isString())
		{
			return value.asCString();
		}

		if (value.isInt64())
		{
			return Converter::ToString(value.asInt64());
		}

		if (value.isUInt64())
		{
			return Converter::ToString(value.asUInt64());
		}

		if (value.isInt())
		{
			return Converter::ToString(value.asInt());
		}

		if (value.isUInt())
		{
			return Converter::ToString(value.asUInt());
		}

		if (value.isDouble())
		{
			return Converter::ToString(value.asDouble());
		}

		if (value.isBool())
		{
			return Converter::ToString(value.asBool());
		}

		return Json::Stringify(value);
	}

	String Converter::ToString(const std::chrono::system_clock::time_point &tp)
	{
		std::time_t t = std::chrono::system_clock::to_time_t(tp);
		char buffer[32]{0};
		::ctime_r(&t, buffer);
		// Ensure null-terminated
		buffer[OV_COUNTOF(buffer) - 1] = '\0';

		String time_string = buffer;

		return time_string.Trim();
	}

	String Converter::ToISO8601String(const std::chrono::system_clock::time_point &tp)
	{
		auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count() % 1000;
		auto time = std::chrono::system_clock::to_time_t(tp);
		std::tm local_time{};
		::localtime_r(&time, &local_time);

		std::ostringstream oss;
		oss << std::put_time(&local_time, "%Y-%m-%dT%H:%M:%S") << "." << std::setfill('0') << std::setw(3) << milliseconds;

		auto gmtoff = (local_time.tm_gmtoff >= 0) ? local_time.tm_gmtoff : -local_time.tm_gmtoff;
		char sign = (local_time.tm_gmtoff >= 0) ? '+' : '-';
		int hour_part = gmtoff / 3600;
		int min_part = (gmtoff % 3600) / 60;
		oss << sign << std::setfill('0') << std::setw(2) << hour_part << ":" << std::setfill('0') << std::setw(2) << min_part;

		ov::String time_string = oss.str().c_str();
		return time_string;
	}

	// HTTP-date    = IMF-fixdate / obs-date
	// An example of the preferred format is
	// Sun, 06 Nov 1994 08:49:37 GMT    ; IMF-fixdate
	// Examples of the two obsolete formats are
	// Sunday, 06-Nov-94 08:49:37 GMT   ; obsolete RFC 850 format
	// Sun Nov  6 08:49:37 1994         ; ANSI C's asctime() format
	String Converter::ToRFC7231String(const std::chrono::system_clock::time_point &tp) 
	{
        // Convert time_point to time_t
        auto time = std::chrono::system_clock::to_time_t(tp);

        // Convert time_t to UTC (GMT) time
        std::tm gmt_time{};
        ::gmtime_r(&time, &gmt_time);

        // Format the time in RFC 7231 format
        std::ostringstream oss;
        oss << std::put_time(&gmt_time, "%a, %d %b %Y %H:%M:%S GMT");

		ov::String time_string = oss.str().c_str();
        return time_string;
    }

	// From ISO8601 string to time_point
	std::chrono::system_clock::time_point Converter::FromISO8601(const String &iso8601)
	{
		// Only support these formats
		// 2023-09-27T00:00:00.000Z or 2023-09-27T00:00:00.000+09:00
		std::string iso8601_string = iso8601.CStr();
		std::tm tm{};
		std::istringstream iss(iso8601_string);
		iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");

		if (iss.fail())
		{
			throw std::invalid_argument("Invalid ISO 8601 string");
		}

		int milliseconds = 0;

		// Extract milliseconds using regex - Deprecated because of performance
		// std::regex milliseconds_regex("\\.([0-9]+)");
		// std::smatch milliseconds_match;
		// if (std::regex_search(iso8601_string, milliseconds_match, milliseconds_regex))
		// {
		// 	milliseconds = std::stoi(milliseconds_match[1]);
		// }
		if (iss.peek() == '.')
		{
			iss.ignore();  // Skip the period
			iss >> milliseconds;
			if (iss.fail())
			{
				throw std::invalid_argument("Invalid ISO 8601 string");
			}
		}

		// Extract timezone offset using regex - Deprecated because of performance
		// std::regex timezone_regex("([+-]\\d{2})(:?\\d{2})?");
		// std::smatch timezone_match;
		// if (std::regex_search(iso8601_string, timezone_match, timezone_regex))
		// {
		// 	int hours = std::stoi(timezone_match[1]);
		// 	int minutes = (timezone_match.size() > 2) ? std::stoi(timezone_match[2]) : 0;
		// 	tm.tm_hour -= hours;
		// 	tm.tm_min -= minutes;
		// }

		if (iss.peek() == 'Z')
		{
			iss.ignore();  // UTC (Zulu time)
			tm.tm_gmtoff = 0;
		}
		else if (iss.peek() == '+' || iss.peek() == '-')
		{
			char sign;
			int hours=0, minutes=0;
			iss >> sign;
			
			// Read hours
			for (int i = 0; i < 2 && std::isdigit(iss.peek()); ++i)
			{
				hours = hours * 10 + (iss.get() - '0');
			}

			// Check if there are minutes
			if (iss.peek() == ':')
			{
				iss.ignore();
				if (std::isdigit(iss.peek()))
				{
					iss >> minutes;
				}
			}
			else if (!iss.eof())
			{
				// Read minutes
				for (int i = 0; i < 2 && std::isdigit(iss.peek()); ++i)
				{
					minutes = minutes * 10 + (iss.get() - '0');
				}
			}

			if (sign == '-')
			{
				hours = -hours;
				minutes = -minutes;
			}

			tm.tm_hour -= hours;
			tm.tm_min -= minutes;
		}

		// Convert to time_point
		auto time = std::chrono::system_clock::from_time_t(timegm(&tm));
		time += std::chrono::milliseconds(milliseconds);

		return time;
	}

	String Converter::ToSiString(int64_t number, int precision)
	{
		String suf[] = {"", "K", "M", "G", "T", "P", "E", "Z", "Y"};

		if (number == 0)
		{
			return "0";
		}

		int64_t abs_number = std::abs(number);
		int8_t place = std::floor(std::log10(abs_number) / std::log10(1000));
		if (place > 8)
		{
			place = 8;
		}

		double num = number / std::pow(1000, place);

		String si_number;
		si_number.Format("%.*f%s", precision, num, suf[place].CStr());

		return si_number;
	}

	String Converter::BitToString(int64_t bits)
	{
		return ToSiString(bits, 2) + "b";
	}

	String Converter::BytesToString(int64_t bytes)
	{
		return ToSiString(bytes, 2) + "B";
	}

	std::time_t Converter::ToTime(uint32_t year, uint32_t mon, uint32_t day, uint32_t hour, uint32_t min, bool isdst)
	{
		//TODO: Set timezone with new parmaeter
		//setenv("TZ", "/usr/share/zoneinfo/America/Los_Angeles", 1); // POSIX-specific

		std::tm tm{};

		tm.tm_year = year - 1900;
		tm.tm_mon = mon - 1;
		tm.tm_mday = day;
		tm.tm_hour = hour;
		tm.tm_min = min;
		tm.tm_isdst = isdst ? 1 : 0;

		return std::mktime(&tm);
	}

	int32_t Converter::ToInt32(const char *str, int base)
	{
		if (str != nullptr)
		{
			try
			{
				return std::stoi(str, nullptr, base);
			}
			catch (std::exception &e)
			{
			}
		}

		return 0;
	}

	int32_t Converter::ToInt32(const ::Json::Value &value, int base)
	{
		if (value.isInt())
		{
			return value.asInt();
		}

		return ToInt32(ToString(value), base);
	}

	uint16_t Converter::ToUInt16(const char *str, int base)
	{
		if (str != nullptr)
		{
			try
			{
				return static_cast<uint16_t>(std::stoi(str, nullptr, base));
			}
			catch (std::exception &e)
			{
			}
		}

		return 0;
	}

	uint32_t Converter::ToUInt32(const char *str, int base)
	{
		if (str != nullptr)
		{
			try
			{
				return static_cast<uint32_t>(std::stoul(str, nullptr, base));
			}
			catch (std::exception &e)
			{
			}
		}

		return 0;
	}

	uint32_t Converter::ToUInt32(const ::Json::Value &value, int base)
	{
		if (value.isUInt())
		{
			return static_cast<uint32_t>(value.asUInt());
		}

		return ToUInt32(ToString(value), base);
	}

	int64_t Converter::ToInt64(const char *str, int base)
	{
		if (str != nullptr)
		{
			try
			{
				return std::stoll(str, nullptr, base);
			}
			catch (std::exception &e)
			{
			}
		}

		return 0L;
	}

	int64_t Converter::ToInt64(const ::Json::Value &value, int base)
	{
		if (value.isInt64())
		{
			return value.asInt64();
		}

		return ToInt64(ToString(value), base);
	}

	uint64_t Converter::ToUInt64(const char *str, int base)
	{
		if (str != nullptr)
		{
			try
			{
				return std::stoull(str, nullptr, base);
			}
			catch (std::exception &e)
			{
			}
		}

		return 0UL;
	}

	bool Converter::ToBool(const char *str)
	{
		if (str == nullptr)
		{
			return false;
		}

		String value = str;
		value.MakeLower();

		if (value == "true")
		{
			return true;
		}
		else if (value == "false")
		{
			return false;
		}

		return (ToInt64(str) != 0);
	}

	bool Converter::ToBool(const ::Json::Value &value)
	{
		if (value.isBool())
		{
			return value.asBool();
		}

		return ToBool(ToString(value));
	}

	float Converter::ToFloat(const char *str)
	{
		if (str != nullptr)
		{
			try
			{
				return std::stof(str, nullptr);
			}
			catch (std::exception &e)
			{
			}
		}

		return 0.0f;
	}

	float Converter::ToFloat(const ::Json::Value &value)
	{
		return ToDouble(value);
	}

	double Converter::ToDouble(const char *str)
	{
		if (str != nullptr)
		{
			try
			{
				return std::stod(str, nullptr);
			}
			catch (std::exception &e)
			{
			}
		}

		return 0.0;
	}

	double Converter::ToDouble(const ::Json::Value &value)
	{
		if (value.isDouble())
		{
			return value.asDouble();
		}

		return ToDouble(ToString(value));
	}

#define EPOCH 2208988800ULL
#define NTP_SCALE_FRAC 4294967296ULL

	uint64_t Converter::SecondsToNtpTs(double seconds)
	{
		uint64_t ntp_timestamp = 0;
		double ipart, fraction;
		fraction = modf(seconds, &ipart);

		ntp_timestamp = (((uint64_t)ipart) << 32) + (fraction * NTP_SCALE_FRAC);

		return ntp_timestamp;
	}

	double Converter::NtpTsToSeconds(uint64_t ntp_timestamp)
	{
		uint32_t msw = ntp_timestamp >> 32;
		uint32_t lsw = ntp_timestamp & 0xFFFFFFFF;
		return NtpTsToSeconds(msw, lsw);
	}

	double Converter::NtpTsToSeconds(uint32_t msw, uint32_t lsw)
	{
		double seconds = (double)msw;
		double frac = (double)lsw / (double)NTP_SCALE_FRAC;

		return seconds + frac;
	}

	uint32_t Converter::ToSynchSafe(uint32_t value)
	{
		unsigned int a, b, c, d, x_final = 0x0;

		a = value & 0x7f;
		b = (value >> 7) & 0x7f;
		c = (value >> 14) & 0x7f;
		d = (value >> 21) & 0x7f;

		x_final = x_final | a;
		x_final = x_final | (b << 8);
		x_final = x_final | (c << 16);
		x_final = x_final | (d << 24);

		return x_final;
	}

	int64_t Converter::Rescale(int64_t value, int64_t to_timescale, int64_t from_timescale)
	{
		return ((value / from_timescale) * to_timescale) + (((value % from_timescale) * to_timescale + (from_timescale / 2)) / from_timescale);
	}
}  // namespace ov
