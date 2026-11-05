//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <arpa/inet.h>
#include <base/ovlibrary/ovlibrary.h>
#include <base/ovsocket/ovsocket.h>
#include <base/ovsocket/socket_test_helpers.h>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

namespace
{
	constexpr int CONNECT_TIMEOUT_MSEC = 1000;
	constexpr int CALLBACK_WAIT_MSEC   = 4000;
	constexpr int BACKLOG_FILLER_COUNT = 4;

	// A listener that never accepts. Its accept queue is filled by non-blocking fillers,
	// so Linux drops later SYNs and a connect to it stays in progress.
	class StalledTcpListener
	{
	public:
		~StalledTcpListener()
		{
			for (auto fd : _filler_fds)
			{
				::close(fd);
			}

			if (_listen_fd >= 0)
			{
				::close(_listen_fd);
			}
		}

		bool Listen()
		{
			_listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
			if (_listen_fd < 0)
			{
				return false;
			}

			sockaddr_in sa{};
			sa.sin_family	   = AF_INET;
			sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			sa.sin_port		   = 0;

			if ((::bind(_listen_fd, reinterpret_cast<sockaddr *>(&sa), sizeof(sa)) != 0) ||
				(::listen(_listen_fd, 0) != 0))
			{
				return false;
			}

			socklen_t len = sizeof(sa);
			if (::getsockname(_listen_fd, reinterpret_cast<sockaddr *>(&sa), &len) != 0)
			{
				return false;
			}
			_port = ntohs(sa.sin_port);

			for (int index = 0; index < BACKLOG_FILLER_COUNT; index++)
			{
				int fd = ::socket(AF_INET, SOCK_STREAM, 0);
				if (fd < 0)
				{
					return false;
				}

				::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
				::connect(fd, reinterpret_cast<sockaddr *>(&sa), sizeof(sa));
				_filler_fds.push_back(fd);
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			return true;
		}

		uint16_t Port() const
		{
			return _port;
		}

		// A probe whose connect does not complete shows that later connects stay in progress too
		bool IsStalled() const
		{
			constexpr int PROBE_MSEC = 200;

			int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
			if (fd < 0)
			{
				return false;
			}

			sockaddr_in sa{};
			sa.sin_family	   = AF_INET;
			sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			sa.sin_port		   = htons(_port);

			::connect(fd, reinterpret_cast<sockaddr *>(&sa), sizeof(sa));

			pollfd poll_fd{fd, POLLOUT, 0};
			const bool completed = (::poll(&poll_fd, 1, PROBE_MSEC) != 0);
			::close(fd);

			return (completed == false);
		}

	private:
		int _listen_fd = -1;
		uint16_t _port = 0;
		std::vector<int> _filler_fds;
	};

	class ConnectEvents : public ov::SocketAsyncInterface
	{
	public:
		void OnConnected(const std::shared_ptr<const ov::SocketError> &error) override
		{
			(error == nullptr) ? _connected++ : _connect_failed++;
		}

		void OnReadable() override
		{
		}

		void OnClosed() override
		{
		}

		std::atomic<int> _connected{0};
		std::atomic<int> _connect_failed{0};
	};
}  // namespace

// A non-blocking TCP connect that never completes must be reported as a failure
// through `OnConnected()` once its timeout passes.
TEST(SocketConnectTimeoutTest, NonBlockingTcpConnectReportsTimeout)
{
	StalledTcpListener listener;
	ASSERT_TRUE(listener.Listen());

	if (listener.IsStalled() == false)
	{
		GTEST_SKIP() << "The listener does not keep a connect in progress";
	}

	auto pool = ov::SocketPool::Create("test-conn-timeout", ov::SocketType::Tcp, false);
	ASSERT_NE(pool, nullptr);
	ASSERT_TRUE(pool->Initialize(1));
	ome_test::ScopeExit pool_closer([&]() { pool->Uninitialize(); });

	auto events = std::make_shared<ConnectEvents>();
	auto client = pool->AllocSocket(ov::SocketFamily::Inet);
	ASSERT_NE(client, nullptr);
	ome_test::ScopeExit client_closer([&]() { client->CloseImmediately(); });
	ASSERT_TRUE(client->MakeNonBlocking(events));

	const auto start = std::chrono::steady_clock::now();
	auto error		 = client->Connect(ov::SocketAddress::CreateAndGetFirst("127.0.0.1", listener.Port()), CONNECT_TIMEOUT_MSEC);
	ASSERT_EQ(error, nullptr);

	ome_test::WaitUntil([&]() { return (events->_connected + events->_connect_failed) != 0; }, CALLBACK_WAIT_MSEC);
	const auto elapsed = std::chrono::steady_clock::now() - start;

	EXPECT_EQ(events->_connected.load(), 0);
	EXPECT_EQ(events->_connect_failed.load(), 1) << "No connect timeout callback within " << CALLBACK_WAIT_MSEC << "ms";
	EXPECT_GE(elapsed, std::chrono::milliseconds(CONNECT_TIMEOUT_MSEC));

	// The callback runs before the worker dispatches the queued close that sets the state
	EXPECT_TRUE(ome_test::WaitUntil([&]() { return client->GetState() == ov::SocketState::Error; }));

	// The close does not report the connect again
	EXPECT_EQ(events->_connected.load(), 0);
	EXPECT_EQ(events->_connect_failed.load(), 1);
}

// A non-blocking TCP connect without a timeout is never timed out by the worker
TEST(SocketConnectTimeoutTest, NonBlockingTcpConnectWithoutTimeoutIsNotTimedOut)
{
	constexpr int OBSERVE_MSEC = 500;

	StalledTcpListener listener;
	ASSERT_TRUE(listener.Listen());

	if (listener.IsStalled() == false)
	{
		GTEST_SKIP() << "The listener does not keep a connect in progress";
	}

	auto pool = ov::SocketPool::Create("test-conn-no-timeout", ov::SocketType::Tcp, false);
	ASSERT_NE(pool, nullptr);
	ASSERT_TRUE(pool->Initialize(1));
	ome_test::ScopeExit pool_closer([&]() { pool->Uninitialize(); });

	auto events = std::make_shared<ConnectEvents>();
	auto client = pool->AllocSocket(ov::SocketFamily::Inet);
	ASSERT_NE(client, nullptr);
	ome_test::ScopeExit client_closer([&]() { client->CloseImmediately(); });
	ASSERT_TRUE(client->MakeNonBlocking(events));

	auto error = client->Connect(ov::SocketAddress::CreateAndGetFirst("127.0.0.1", listener.Port()), 0);
	EXPECT_EQ(error, nullptr);

	std::this_thread::sleep_for(std::chrono::milliseconds(OBSERVE_MSEC));

	EXPECT_EQ(events->_connected.load(), 0);
	EXPECT_EQ(events->_connect_failed.load(), 0);
	EXPECT_EQ(client->GetState(), ov::SocketState::Connecting);
}

// A blocking TCP connect that never completes fails with a timeout error once its timeout passes
TEST(SocketConnectTimeoutTest, BlockingTcpConnectReportsTimeout)
{
	StalledTcpListener listener;
	ASSERT_TRUE(listener.Listen());

	if (listener.IsStalled() == false)
	{
		GTEST_SKIP() << "The listener does not keep a connect in progress";
	}

	auto pool = ov::SocketPool::Create("test-conn-blocking", ov::SocketType::Tcp, false);
	ASSERT_NE(pool, nullptr);
	ASSERT_TRUE(pool->Initialize(1));
	ome_test::ScopeExit pool_closer([&]() { pool->Uninitialize(); });

	auto client = pool->AllocSocket(ov::SocketFamily::Inet);
	ASSERT_NE(client, nullptr);
	ome_test::ScopeExit client_closer([&]() { client->CloseImmediately(); });

	const auto start   = std::chrono::steady_clock::now();
	auto error		   = client->Connect(ov::SocketAddress::CreateAndGetFirst("127.0.0.1", listener.Port()), CONNECT_TIMEOUT_MSEC);
	const auto elapsed = std::chrono::steady_clock::now() - start;

	ASSERT_NE(error, nullptr);
	EXPECT_NE(error->GetMessage().IndexOf("Connection timed out"), -1) << error->What();
	EXPECT_GE(elapsed, std::chrono::milliseconds(CONNECT_TIMEOUT_MSEC));
	EXPECT_EQ(client->GetState(), ov::SocketState::Error);
}

// A blocking TCP connect closed on another thread reports the close, and the socket stays `Closed`
TEST(SocketConnectTimeoutTest, BlockingTcpConnectClosedWhileWaiting)
{
	// About 50 ms of 1 ms samples, so a short wait on another lock is not taken for the connect wait
	constexpr int BLOCKED_SAMPLE_COUNT = 50;

	StalledTcpListener listener;
	ASSERT_TRUE(listener.Listen());

	if (listener.IsStalled() == false)
	{
		GTEST_SKIP() << "The listener does not keep a connect in progress";
	}

	auto pool = ov::SocketPool::Create("test-conn-closed", ov::SocketType::Tcp, false);
	ASSERT_NE(pool, nullptr);
	ASSERT_TRUE(pool->Initialize(1));
	ome_test::ScopeExit pool_closer([&]() { pool->Uninitialize(); });

	auto client = pool->AllocSocket(ov::SocketFamily::Inet);
	ASSERT_NE(client, nullptr);
	ome_test::ScopeExit client_closer([&]() { client->CloseImmediately(); });

	std::shared_ptr<const ov::SocketError> error;
	std::atomic<pid_t> tid{0};
	std::thread connector([&]() {
		tid = static_cast<pid_t>(::syscall(SYS_gettid));
		error = client->Connect(ov::SocketAddress::CreateAndGetFirst("127.0.0.1", listener.Port()), CONNECT_TIMEOUT_MSEC);
	});

	// The close must find the connect waiting for its result, not about to start
	int blocked_samples = 0;
	EXPECT_TRUE(ome_test::WaitUntil([&]() {
		const bool blocked = (client->GetState() == ov::SocketState::Connecting) && ome_test::IsBlockedIn(tid, {SYS_futex});
		blocked_samples	   = blocked ? (blocked_samples + 1) : 0;
		return blocked_samples >= BLOCKED_SAMPLE_COUNT;
	}));

	EXPECT_TRUE(client->CloseImmediately());
	connector.join();

	ASSERT_NE(error, nullptr);
	EXPECT_EQ(error->GetMessage(), "Socket is closed") << error->What();
	EXPECT_EQ(client->GetState(), ov::SocketState::Closed);
}
