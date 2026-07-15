//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#include "latency_probe.h"

#ifdef OME_LATENCY_PROBE

#include <fcntl.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <chrono>

#include "path_manager.h"
#include "string.h"

namespace ov
{
	// Resolved once, thread-safe via function-local static initialization.
	static const String &LatencyProbeDir()
	{
		static const String dir = []() -> String {
			const char *env = ::getenv("OME_LATENCY_PROBE_DIR");
			String d = (env != nullptr && env[0] != '\0') ? String(env) : String("/dev/shm");

			PathManager::MakeDirectoryRecursive(d);
			::fprintf(stderr, "[OME_LATENCY_PROBE] serving-path instrumentation active, writing logs to %s/latency_probe.log\n", d.CStr());

			return d;
		}();

		return dir;
	}

	// Single append-only log file shared by every subsystem, opened once.
	static int LatencyProbeFd()
	{
		// 0600 + O_CLOEXEC: records can include request URIs, so keep the file owner-only and
		// prevent the fd from leaking into forked/exec'd child processes.
		static const int s_fd = ::open(PathManager::Combine(LatencyProbeDir(), "latency_probe.log").CStr(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);

		return s_fd;
	}

	void LatencyProbeLog(const char *tag, const char *fmt, ...)
	{
		const int fd = LatencyProbeFd();
		if (fd < 0)
		{
			return;
		}

		const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

		// One record = "<epoch_ms> <TAG> <fields>\n", built into a fixed buffer and written with a
		// single ::write(). O_APPEND makes each such write atomic, so records from concurrent
		// threads never interleave.
		char buf[512];
		int off = ::snprintf(buf, sizeof(buf), "%lld %s ", static_cast<long long>(now_ms), tag);
		if (off < 0)
		{
			return;
		}
		if (off > static_cast<int>(sizeof(buf)) - 2)
		{
			off = static_cast<int>(sizeof(buf)) - 2;
		}

		va_list ap;
		va_start(ap, fmt);
		const int written = ::vsnprintf(buf + off, sizeof(buf) - static_cast<size_t>(off) - 1, fmt, ap);
		va_end(ap);
		if (written > 0)
		{
			off += written;
			if (off > static_cast<int>(sizeof(buf)) - 1)
			{
				off = static_cast<int>(sizeof(buf)) - 1;  // truncated to fit the newline
			}
		}

		buf[off++] = '\n';
		[[maybe_unused]] auto w = ::write(fd, buf, static_cast<size_t>(off));
	}
}  // namespace ov

#endif	// OME_LATENCY_PROBE
