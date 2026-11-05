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

#include <arpa/inet.h>
#include <base/ovsocket/ovsocket.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <memory>
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

	// Binds a UDP socket to an ephemeral loopback port and returns the port (0 on failure)
	inline uint16_t BindToLoopback(const std::shared_ptr<ov::Socket> &socket)
	{
		if (socket->Bind(ov::SocketAddress::CreateAndGetFirst("127.0.0.1", 0)) == false)
		{
			return 0;
		}

		sockaddr_in address{};
		socklen_t length = sizeof(address);

		if (::getsockname(socket->GetNativeHandle(), reinterpret_cast<sockaddr *>(&address), &length) != 0)
		{
			return 0;
		}

		return ntohs(address.sin_port);
	}

	// Sends one datagram to a loopback port
	inline bool SendDatagram(uint16_t port)
	{
		int fd = ::socket(AF_INET, SOCK_DGRAM, 0);

		if (fd < 0)
		{
			return false;
		}

		sockaddr_in address{};
		address.sin_family		= AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port		= htons(port);

		const char payload[]	= "x";
		auto sent				= ::sendto(fd, payload, sizeof(payload), 0, reinterpret_cast<sockaddr *>(&address), sizeof(address));
		::close(fd);

		return (sent == sizeof(payload));
	}
}  // namespace ome_test
