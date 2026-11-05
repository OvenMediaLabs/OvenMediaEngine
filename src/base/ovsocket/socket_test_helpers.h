//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================

//  Helpers shared by the socket test suites.

#pragma once

#include <sys/types.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <thread>
#include <utility>

namespace ome_test
{
	constexpr int WAIT_TIMEOUT_MSEC = 2000;

	// Polls `condition` until it holds, or returns false after `timeout_msec`
	inline bool WaitUntil(const std::function<bool()> &condition, int timeout_msec = WAIT_TIMEOUT_MSEC)
	{
		auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_msec);

		while (condition() == false)
		{
			if (std::chrono::steady_clock::now() >= deadline)
			{
				return false;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		return true;
	}

	// Whether the thread is blocked in one of `syscalls`, read from `/proc/self/task/<tid>/syscall`
	inline bool IsBlockedIn(pid_t tid, std::initializer_list<long> syscalls)
	{
		char path[64];
		::snprintf(path, sizeof(path), "/proc/self/task/%d/syscall", tid);

		auto file = ::fopen(path, "r");

		if (file == nullptr)
		{
			return false;
		}

		long number = -1;
		auto read	= ::fscanf(file, "%ld", &number);
		::fclose(file);

		// A running thread reads as "running", which `fscanf()` does not parse as a number
		return (read == 1) && (std::find(syscalls.begin(), syscalls.end(), number) != syscalls.end());
	}

	// Runs `cleanup` on every way out of the scope, including a failed `ASSERT_*`.
	// A test closes its sockets with it, since `~Socket` aborts in debug builds on a socket that was never closed.
	class ScopeExit
	{
	public:
		explicit ScopeExit(std::function<void()> cleanup)
			: _cleanup(std::move(cleanup))
		{
		}

		~ScopeExit()
		{
			_cleanup();
		}

	private:
		std::function<void()> _cleanup;
	};
}  // namespace ome_test
