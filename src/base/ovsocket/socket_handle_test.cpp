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
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <future>
#include <initializer_list>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace
{
	using ome_test::IsBlockedIn;
	using ome_test::ScopeExit;
	using ome_test::WAIT_TIMEOUT_MSEC;
	using ome_test::WaitUntil;

	constexpr int RACE_ITERATIONS	   = 200;
	constexpr char OTHER_PAYLOAD[]	   = "OTHER";
	constexpr size_t OTHER_PAYLOAD_LEN = sizeof(OTHER_PAYLOAD) - 1;

	bool IsOpen(int fd)
	{
		return ::fcntl(fd, F_GETFD) != -1;
	}

	// Runs `blocking_call` on another thread, closes `socket` once the call is blocked in one of `syscalls`,
	// and returns what the call returned, or `std::nullopt` if it is still blocked.
	// The thread owns everything it uses, so a call that never returns is detached instead of blocking the test.
	std::optional<ov::String> CloseWhileBlocked(const std::shared_ptr<ov::Socket> &socket, std::initializer_list<long> syscalls,
												std::function<ov::String()> blocking_call)
	{
		std::promise<ov::String> returned;
		auto returned_future = returned.get_future();
		auto tid			 = std::make_shared<std::atomic<pid_t>>(0);

		std::thread caller([call = std::move(blocking_call), promise = std::move(returned), tid]() mutable {
			*tid = static_cast<pid_t>(::syscall(SYS_gettid));
			promise.set_value(call());
		});

		// The close must find the call blocked in the kernel, not about to start
		EXPECT_TRUE(WaitUntil([&]() { return (*tid != 0) && IsBlockedIn(*tid, syscalls); }));
		EXPECT_EQ(returned_future.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);

		socket->CloseImmediately();

		if (returned_future.wait_for(std::chrono::milliseconds(WAIT_TIMEOUT_MSEC)) != std::future_status::ready)
		{
			caller.detach();
			return std::nullopt;
		}

		caller.join();

		return returned_future.get();
	}

	// Exposes the guard of `ov::Socket`, so a test can keep a syscall "in flight" for as long as it needs
	class HoldingSocket : public ov::Socket
	{
	public:
		HoldingSocket(PrivateToken token, const std::shared_ptr<ov::SocketPoolWorker> &worker)
			: ov::Socket(token, worker)
		{
		}

		std::unique_ptr<IoGuard> Hold()
		{
			return std::make_unique<IoGuard>(*this);
		}
	};

	// A blocking OME TCP socket connected to a raw peer on loopback
	class ConnectedPair
	{
	public:
		explicit ConnectedPair(const std::shared_ptr<ov::SocketPool> &pool)
		{
			sockaddr_in address{};
			address.sin_family		= AF_INET;
			address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			socklen_t length		= sizeof(address);

			_listen_fd				= ::socket(AF_INET, SOCK_STREAM, 0);

			if ((_listen_fd < 0) ||
				(::bind(_listen_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) ||
				(::listen(_listen_fd, 1) != 0) ||
				(::getsockname(_listen_fd, reinterpret_cast<sockaddr *>(&address), &length) != 0))
			{
				return;
			}

			socket = pool->AllocSocket(ov::SocketFamily::Inet);

			if ((socket == nullptr) ||
				(socket->Connect(ov::SocketAddress::CreateAndGetFirst("127.0.0.1", ntohs(address.sin_port)), WAIT_TIMEOUT_MSEC) != nullptr))
			{
				return;
			}

			peer_fd = ::accept(_listen_fd, nullptr, nullptr);
		}

		~ConnectedPair()
		{
			if (socket != nullptr)
			{
				socket->CloseImmediately();
			}

			if (peer_fd >= 0)
			{
				::close(peer_fd);
			}

			if (_listen_fd >= 0)
			{
				::close(_listen_fd);
			}
		}

		bool IsConnected() const
		{
			return (socket != nullptr) && (peer_fd >= 0);
		}

		std::shared_ptr<ov::Socket> socket;
		int peer_fd = -1;

	private:
		int _listen_fd = -1;
	};

	// A socket pair whose read end already holds `OTHER_PAYLOAD`, standing in for another connection
	class OtherConnection
	{
	public:
		OtherConnection()
		{
			if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0)
			{
				fds[0] = fds[1] = -1;
				return;
			}

			if (::send(fds[1], OTHER_PAYLOAD, OTHER_PAYLOAD_LEN, 0) != static_cast<ssize_t>(OTHER_PAYLOAD_LEN))
			{
				::close(fds[0]);
				::close(fds[1]);
				fds[0] = fds[1] = -1;
			}
		}

		~OtherConnection()
		{
			for (auto fd : fds)
			{
				if (fd >= 0)
				{
					::close(fd);
				}
			}
		}

		bool IsValid() const
		{
			return fds[0] >= 0;
		}

		int fds[2]{-1, -1};
	};
}  // namespace

// A socket closed as `Disconnected` never uses its number again, even after the number names another connection
TEST(SocketHandleTest, ClosedSocketDoesNotUseReusedNumber)
{
	auto pool = ov::SocketPool::Create("test-stale-number", ov::SocketType::Tcp, false);
	ASSERT_NE(pool, nullptr);
	ASSERT_TRUE(pool->Initialize(1));

	{
		ConnectedPair pair(pool);
		ASSERT_TRUE(pair.IsConnected());

		OtherConnection other;
		ASSERT_TRUE(other.IsValid());

		const int number = pair.socket->GetNativeHandle();

		// The same close as the worker's on `EPOLLHUP`, which leaves the socket `Disconnected`, not `Closed`
		ASSERT_TRUE(pair.socket->CloseImmediatelyWithState(ov::SocketState::Disconnected));
		ASSERT_FALSE(IsOpen(number));

		// The number now names the other connection.
		// `F_DUPFD` takes the number only while it is free, where `dup2()` would close a file another thread opened there.
		ASSERT_EQ(::fcntl(other.fds[0], F_DUPFD, number), number);

		char buffer[16]{};
		int receive_buffer_size = 0;
		socklen_t option_length = sizeof(receive_buffer_size);
		ASSERT_EQ(::getsockopt(number, SOL_SOCKET, SO_RCVBUF, &receive_buffer_size, &option_length), 0);

		EXPECT_FALSE(pair.socket->Recv(buffer, sizeof(buffer), true).has_value());
		EXPECT_FALSE(pair.socket->SetSockOpt<int>(SOL_SOCKET, SO_RCVBUF, receive_buffer_size * 4));
		EXPECT_FALSE(pair.socket->Send(OTHER_PAYLOAD, OTHER_PAYLOAD_LEN));

		// The other connection still holds its data and its option, and received nothing
		int current_size = 0;
		option_length	 = sizeof(current_size);
		ASSERT_EQ(::getsockopt(number, SOL_SOCKET, SO_RCVBUF, &current_size, &option_length), 0);
		EXPECT_EQ(current_size, receive_buffer_size);
		EXPECT_EQ(::recv(number, buffer, sizeof(buffer), MSG_PEEK | MSG_DONTWAIT), static_cast<ssize_t>(OTHER_PAYLOAD_LEN));
		EXPECT_EQ(::recv(other.fds[1], buffer, sizeof(buffer), MSG_DONTWAIT), -1);

		::close(number);
	}

	pool->Uninitialize();
}

// A receive running on another thread while the socket is closed never reads from a connection that reuses the number
TEST(SocketHandleTest, NumberIsNotReusedWhileRecvIsInFlight)
{
	auto pool = ov::SocketPool::Create("test-reuse-race", ov::SocketType::Tcp, false);
	ASSERT_NE(pool, nullptr);
	ASSERT_TRUE(pool->Initialize(1));

	int hits   = 0;
	int reused = 0;

	for (int iteration = 0; iteration < RACE_ITERATIONS; iteration++)
	{
		ConnectedPair pair(pool);
		ASSERT_TRUE(pair.IsConnected());

		OtherConnection other;
		ASSERT_TRUE(other.IsValid());

		const int number = pair.socket->GetNativeHandle();
		std::atomic<bool> stop{false};
		std::atomic<bool> hit{false};
		std::atomic<int> taken{-1};

		// Reads the closing socket until it reports the close or reads the other connection's data
		std::thread reader([&]() {
			char buffer[16];

			while (true)
			{
				auto result = pair.socket->Recv(buffer, sizeof(buffer), true);

				if (result.has_value() == false)
				{
					break;
				}

				if ((result.value() == OTHER_PAYLOAD_LEN) && (::memcmp(buffer, OTHER_PAYLOAD, OTHER_PAYLOAD_LEN) == 0))
				{
					hit = true;
					break;
				}
			}
		});

		// Opens descriptors until one takes the number of the closing socket.
		// The others stay open meanwhile, so a lower free number cannot keep coming back.
		std::thread opener([&]() {
			std::vector<int> others;

			while (stop == false)
			{
				int fd = ::dup(other.fds[0]);

				if (fd == number)
				{
					taken = fd;
					break;
				}

				if (fd >= 0)
				{
					others.push_back(fd);
				}
			}

			for (auto fd : others)
			{
				::close(fd);
			}
		});

		pair.socket->CloseImmediately();

		reader.join();
		WaitUntil([&]() { return taken >= 0; });
		stop = true;
		opener.join();

		hits += hit ? 1 : 0;

		if (taken >= 0)
		{
			reused++;
			::close(taken);
		}
	}

	EXPECT_EQ(hits, 0);
	EXPECT_EQ(reused, RACE_ITERATIONS);

	pool->Uninitialize();
}

// A close wakes a receive blocked on another thread, and the number is released once that receive returns
TEST(SocketHandleTest, CloseWakesBlockedRecv)
{
	auto pool = ov::SocketPool::Create("test-wake-recv", ov::SocketType::Tcp, false);
	ASSERT_NE(pool, nullptr);
	ASSERT_TRUE(pool->Initialize(1));

	{
		ConnectedPair pair(pool);
		ASSERT_TRUE(pair.IsConnected());

		const int number = pair.socket->GetNativeHandle();

		auto returned	 = CloseWhileBlocked(pair.socket, {SYS_recvfrom, SYS_recvmsg}, [socket = pair.socket]() {
			   char buffer[16];
			   auto result = socket->Recv(buffer, sizeof(buffer), false);

			   return result.has_value() ? ov::String("data") : result.error()->GetMessage();
		});

		ASSERT_TRUE(returned.has_value()) << "Recv() is still blocked on a closed socket";
		EXPECT_EQ(returned.value(), "Socket is closed");
		EXPECT_EQ(pair.socket->GetState(), ov::SocketState::Closed);
		EXPECT_FALSE(IsOpen(number));
	}

	pool->Uninitialize();
}

// A close wakes an accept blocked on another thread, and the number is released once that accept returns
TEST(SocketHandleTest, CloseWakesBlockedAccept)
{
	auto pool = ov::SocketPool::Create("test-wake-accept", ov::SocketType::Tcp, false);
	ASSERT_NE(pool, nullptr);
	ASSERT_TRUE(pool->Initialize(1));

	auto socket = pool->AllocSocket(ov::SocketFamily::Inet);
	ASSERT_NE(socket, nullptr);
	ScopeExit closer([&]() { socket->CloseImmediately(); });
	ASSERT_TRUE(socket->Bind(ov::SocketAddress::CreateAndGetFirst("127.0.0.1", 0)));
	ASSERT_TRUE(socket->Listen());

	const int number = socket->GetNativeHandle();

	auto returned	 = CloseWhileBlocked(socket, {SYS_accept, SYS_accept4}, [socket]() {
		   ov::SocketAddress client;

		   return socket->Accept(&client).IsValid() ? ov::String("accepted") : ov::String("not accepted");
	});

	ASSERT_TRUE(returned.has_value()) << "Accept() is still blocked on a closed socket";
	EXPECT_EQ(returned.value(), "not accepted");
	EXPECT_FALSE(IsOpen(number));

	pool->Uninitialize();
}

// A close wakes a UDP receive blocked on another thread, and the number is released once that receive returns
TEST(SocketHandleTest, CloseWakesBlockedRecvFrom)
{
	auto pool = ov::SocketPool::Create("test-wake-recvfrom", ov::SocketType::Udp, false);
	ASSERT_NE(pool, nullptr);
	ASSERT_TRUE(pool->Initialize(1));

	auto socket = pool->AllocSocket(ov::SocketFamily::Inet);
	ASSERT_NE(socket, nullptr);
	ScopeExit closer([&]() { socket->CloseImmediately(); });
	ASSERT_TRUE(socket->Bind(ov::SocketAddress::CreateAndGetFirst("127.0.0.1", 0)));

	const int number = socket->GetNativeHandle();

	auto returned	 = CloseWhileBlocked(socket, {SYS_recvfrom, SYS_recvmsg}, [socket]() {
		   auto data  = std::make_shared<ov::Data>(1500);
		   auto error = socket->RecvFrom(data, nullptr, false);

		   return (error != nullptr) ? error->GetMessage() : ov::String("data");
	});

	ASSERT_TRUE(returned.has_value()) << "RecvFrom() is still blocked on a closed socket";
	EXPECT_EQ(returned.value(), "Socket is closed");
	EXPECT_EQ(socket->GetState(), ov::SocketState::Closed);
	EXPECT_FALSE(IsOpen(number));

	pool->Uninitialize();
}

// A closed socket cannot join an epoll instance of the caller,
// and removing it once released leaves alone the file that now holds its number in that instance
TEST(SocketHandleTest, ExternalEpollRegistrationAfterClose)
{
	auto pool = ov::SocketPool::Create("test-external-epoll", ov::SocketType::Tcp, false);
	ASSERT_NE(pool, nullptr);
	ASSERT_TRUE(pool->Initialize(1));

	int epoll_fd = ::epoll_create1(0);
	ASSERT_GE(epoll_fd, 0);

	{
		ConnectedPair pair(pool);
		ASSERT_TRUE(pair.IsConnected());

		epoll_event event{};
		event.events = EPOLLIN;

		ASSERT_TRUE(pair.socket->AddToExternalEpoll(epoll_fd, &event));
		EXPECT_TRUE(pair.socket->DeleteFromExternalEpoll(epoll_fd));
		ASSERT_TRUE(pair.socket->AddToExternalEpoll(epoll_fd, &event));

		// Opened before the close, so it cannot take the number the close frees
		OtherConnection other;
		ASSERT_TRUE(other.IsValid());

		const int number = pair.socket->GetNativeHandle();

		ASSERT_TRUE(pair.socket->CloseImmediately());
		ASSERT_FALSE(IsOpen(number));

		EXPECT_FALSE(pair.socket->AddToExternalEpoll(epoll_fd, &event));

		// The number now names the other connection, registered in the same epoll instance
		ASSERT_EQ(::fcntl(other.fds[0], F_DUPFD, number), number);
		ASSERT_EQ(::epoll_ctl(epoll_fd, EPOLL_CTL_ADD, number, &event), 0);

		EXPECT_TRUE(pair.socket->DeleteFromExternalEpoll(epoll_fd));
		EXPECT_EQ(::epoll_ctl(epoll_fd, EPOLL_CTL_MOD, number, &event), 0);

		::close(number);
	}

	::close(epoll_fd);
	pool->Uninitialize();
}

// Socket IDs are not reused, unlike the numbers of native handles
TEST(SocketHandleTest, IdIsNotReused)
{
	auto pool = ov::SocketPool::Create("test-socket-id", ov::SocketType::Tcp, false);
	ASSERT_NE(pool, nullptr);
	ASSERT_TRUE(pool->Initialize(1));

	auto first = pool->AllocSocket(ov::SocketFamily::Inet);
	ASSERT_NE(first, nullptr);

	const auto first_id		= first->GetId();
	const auto first_number = first->GetNativeHandle();
	ASSERT_TRUE(first->CloseImmediately());

	auto second = pool->AllocSocket(ov::SocketFamily::Inet);
	ASSERT_NE(second, nullptr);

	if (second->GetNativeHandle() != first_number)
	{
		// Another thread took the number first, so the ID is not compared against a reused number
		EXPECT_TRUE(second->CloseImmediately());
		pool->Uninitialize();
		GTEST_SKIP();
	}

	EXPECT_NE(second->GetId(), first_id);
	EXPECT_TRUE(second->CloseImmediately());

	pool->Uninitialize();
}

// A close leaves the handle open while a syscall is in flight, and the last one to return closes it.
// Meanwhile the socket can still leave an epoll instance of the caller.
TEST(SocketHandleTest, HandleOutlivesCloseWhileSyscallIsInFlight)
{
	auto pool = ov::SocketPool::Create("test-in-flight", ov::SocketType::Tcp, false);
	ASSERT_NE(pool, nullptr);
	ASSERT_TRUE(pool->Initialize(1));

	int epoll_fd = ::epoll_create1(0);
	ASSERT_GE(epoll_fd, 0);

	{
		auto socket = pool->AllocSocket<HoldingSocket>(ov::SocketFamily::Inet);
		ASSERT_NE(socket, nullptr);
		ScopeExit closer([&]() { socket->CloseImmediately(); });

		const int number = socket->GetNativeHandle();

		epoll_event event{};
		event.events = EPOLLIN;
		ASSERT_TRUE(socket->AddToExternalEpoll(epoll_fd, &event));

		auto in_flight = socket->Hold();

		ASSERT_TRUE(socket->CloseImmediately());
		EXPECT_FALSE(socket->IsValid());
		EXPECT_TRUE(IsOpen(number));

		EXPECT_FALSE(socket->AddToExternalEpoll(epoll_fd, &event));
		EXPECT_TRUE(socket->DeleteFromExternalEpoll(epoll_fd));

		// Removed by the call above while the handle is still open
		EXPECT_EQ(::epoll_ctl(epoll_fd, EPOLL_CTL_DEL, number, nullptr), -1);
		EXPECT_EQ(errno, ENOENT);

		in_flight.reset();
		EXPECT_FALSE(IsOpen(number));
	}

	::close(epoll_fd);
	pool->Uninitialize();
}
