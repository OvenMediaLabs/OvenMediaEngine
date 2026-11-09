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
#include <fcntl.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <poll.h>
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

	// Owns a file descriptor and closes it on destruction
	class UniqueFd
	{
	public:
		explicit UniqueFd(int fd = -1)
			: _fd(fd)
		{
		}

		UniqueFd(UniqueFd &&other) noexcept
			: _fd(std::exchange(other._fd, -1))
		{
		}

		UniqueFd &operator=(UniqueFd &&other) noexcept
		{
			if (this != &other)
			{
				Reset();
				_fd = std::exchange(other._fd, -1);
			}

			return *this;
		}

		~UniqueFd()
		{
			Reset();
		}

		int Get() const
		{
			return _fd;
		}

		void Reset()
		{
			if (_fd >= 0)
			{
				::close(std::exchange(_fd, -1));
			}
		}

	private:
		int _fd;
	};

	// Moves `fd` to `number`, and returns the descriptor at `number`,
	// or at another number if `number` was taken (invalid on failure).
	// `F_DUPFD` takes the number only while it is free, where `dup2()` would close a file another thread opened there.
	inline UniqueFd TakeNumber(UniqueFd fd, int number)
	{
		if (fd.Get() == number)
		{
			return fd;
		}

		return UniqueFd(::fcntl(fd.Get(), F_DUPFD, number));
	}

	// Listens on an ephemeral loopback TCP port, and returns the listening descriptor (invalid on failure)
	inline UniqueFd ListenOnLoopback(uint16_t *port, int backlog = 1)
	{
		UniqueFd fd(::socket(AF_INET, SOCK_STREAM, 0));

		sockaddr_in address{};
		address.sin_family		= AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		socklen_t length		= sizeof(address);

		if ((fd.Get() < 0) ||
			(::bind(fd.Get(), reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) ||
			(::listen(fd.Get(), backlog) != 0) ||
			(::getsockname(fd.Get(), reinterpret_cast<sockaddr *>(&address), &length) != 0))
		{
			return UniqueFd();
		}

		*port = ntohs(address.sin_port);
		return fd;
	}

	// Uninitializes the pool from `CreatePool()` after the test body, on every way out including a failed `ASSERT_*`.
	// For a test that has already called `Uninitialize()`, the call here finds no worker to stop.
	class PoolTest : public ::testing::Test
	{
	protected:
		// Returns `nullptr` if the pool could not be initialized
		std::shared_ptr<ov::SocketPool> CreatePool(const char *name, ov::SocketType type, bool thread_per_socket = false)
		{
			auto pool = ov::SocketPool::Create(name, type, thread_per_socket);

			if ((pool == nullptr) || (pool->Initialize(1) == false))
			{
				return nullptr;
			}

			_pool = pool;

			return pool;
		}

		void TearDown() override
		{
			if (_pool != nullptr)
			{
				_pool->Uninitialize();
			}
		}

	protected:
		std::shared_ptr<ov::SocketPool> _pool;
	};

	// Reads the local port from the handle (0 on failure).
	// Unlike `Socket::GetLocalAddress()`, it also sees a port that `Connect()` assigned implicitly.
	inline uint16_t LocalPortOf(const std::shared_ptr<ov::Socket> &socket)
	{
		sockaddr_in address{};
		socklen_t length = sizeof(address);

		if (::getsockname(socket->GetNativeHandle(), reinterpret_cast<sockaddr *>(&address), &length) != 0)
		{
			return 0;
		}

		return ntohs(address.sin_port);
	}

	// Binds a UDP socket to an ephemeral loopback port and returns the port (0 on failure)
	inline uint16_t BindToLoopback(const std::shared_ptr<ov::Socket> &socket)
	{
		return socket->Bind(ov::SocketAddress::CreateAndGetFirst("127.0.0.1", 0)) ? LocalPortOf(socket) : 0;
	}

	// Sends one datagram to a loopback port.
	// The kernel can deliver it after this returns, so a test that relies on the arrival calls `WaitUntilReadable()`.
	inline bool SendDatagram(uint16_t port)
	{
		UniqueFd fd(::socket(AF_INET, SOCK_DGRAM, 0));

		if (fd.Get() < 0)
		{
			return false;
		}

		sockaddr_in address{};
		address.sin_family		= AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port		= htons(port);

		const char payload[]	= "x";
		auto sent				= ::sendto(fd.Get(), payload, sizeof(payload), 0, reinterpret_cast<sockaddr *>(&address), sizeof(address));

		return (sent == sizeof(payload));
	}

	// Waits until `poll()` reports data queued on `native_handle`.
	// It does not wait for an epoll watching the handle to be woken.
	inline bool WaitUntilReadable(int native_handle)
	{
		return WaitUntil([native_handle]() {
			pollfd entry{native_handle, POLLIN, 0};
			return (::poll(&entry, 1, 0) == 1) && ((entry.revents & POLLIN) != 0);
		});
	}
}  // namespace ome_test
