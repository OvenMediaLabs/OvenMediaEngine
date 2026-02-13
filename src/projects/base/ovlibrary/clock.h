#pragma once

#include <ctime>
#include <chrono>

namespace ov
{
	class Clock
	{
	public:
		Clock() = delete;
		~Clock() = delete;

		static uint64_t NowMSec()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		}

		// yy:mm:dd HH:MM:SS.ms
		static ov::String Now()
		{
			auto now = std::chrono::system_clock::now();
			auto ttime_t = std::chrono::system_clock::to_time_t(now);
			auto tp_sec = std::chrono::system_clock::from_time_t(ttime_t);
			auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - tp_sec);

			struct tm lt;
			if(localtime_r(&ttime_t, &lt) == nullptr)
			{
				return "";
			}

			char date_time_format[] = "%Y.%m.%d-%H.%M.%S";
			char time_str[] = "yyyy.mm.dd.HH-MM.SS.fff";

			strftime(time_str, strlen(time_str), date_time_format, &lt);

			ov::String result;
			result.AppendFormat("%s.%" PRId64, time_str, ms.count());

			return result;
		}

		#define GETTIMEOFDAY_TO_NTP_OFFSET 2208988800 //  Number of seconds between 1-Jan-1900 and 1-Jan-1970

		// NTP seconds to be expressed as a real number
		static double GetNtpTime()
		{
			struct timespec now;
			clock_gettime(CLOCK_REALTIME, &now);

			return (double)now.tv_sec + (double)GETTIMEOFDAY_TO_NTP_OFFSET + (double)now.tv_nsec*(double)1.0e-9;
		}

		static void	GetNtpTime(uint32_t &msw, uint32_t &lsw)
		{
			struct timespec now;
			clock_gettime(CLOCK_REALTIME, &now);

			msw = (uint32_t)(now.tv_sec) + GETTIMEOFDAY_TO_NTP_OFFSET;
			lsw = (uint32_t)((double)(now.tv_nsec/1000)*(double)(((uint64_t)1)<<32)*1.0e-6);
		}

		static uint64_t GetElapsedMiliSecondsFromNow(std::chrono::system_clock::time_point time)
		{
			auto current = std::chrono::high_resolution_clock::now();
			return std::chrono::duration_cast<std::chrono::milliseconds>(current - time).count();
		}
	};
}