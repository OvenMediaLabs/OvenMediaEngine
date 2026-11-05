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
#include <fcntl.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
	constexpr int SOCKET_COUNT		   = 50;
	constexpr int WAIT_TIMEOUT_MSEC	   = 2000;
	constexpr const char *EMPTY_WORKER = "socket_map: 0,";

	bool WaitUntil(const std::function<bool()> &condition)
	{
		auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(WAIT_TIMEOUT_MSEC);

		while (condition() == false)
		{
			if (std::chrono::steady_clock::now() >= deadline)
			{
				return false;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		return true;
	}

	bool AllExpired(const std::vector<std::weak_ptr<ov::Socket>> &sockets)
	{
		for (auto &socket : sockets)
		{
			if (socket.expired() == false)
			{
				return false;
			}
		}

		return true;
	}

	// Binds a UDP socket to an ephemeral loopback port and returns the port (0 on failure)
	uint16_t BindToLoopback(const std::shared_ptr<ov::Socket> &socket)
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

	bool SendDatagram(uint16_t port)
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

	// Listens on an ephemeral loopback TCP port and returns the listening fd (-1 on failure)
	int ListenOnLoopback(uint16_t *port)
	{
		int fd = ::socket(AF_INET, SOCK_STREAM, 0);

		if (fd < 0)
		{
			return -1;
		}

		sockaddr_in address{};
		address.sin_family		= AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		socklen_t length		= sizeof(address);

		if ((::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) ||
			(::listen(fd, 1) != 0) ||
			(::getsockname(fd, reinterpret_cast<sockaddr *>(&address), &length) != 0))
		{
			::close(fd);
			return -1;
		}

		*port = ntohs(address.sin_port);
		return fd;
	}

	// Holds the worker inside a socket callback until `Release()`, so the worker handles no other event meanwhile.
	// The callback state is shared, so a callback that runs after the test returns touches no destroyed object.
	class WorkerGate
	{
	public:
		~WorkerGate()
		{
			Release();
		}

		// Only the first call holds the worker, so a later event on the gate socket returns at once
		std::function<void()> MakeCallback()
		{
			auto entered  = _entered;
			auto released = _released_future;
			auto fired	  = std::make_shared<std::atomic<bool>>(false);

			return [entered, released, fired]() {
				if (fired->exchange(true) == false)
				{
					entered->set_value();
					released.wait();
				}
			};
		}

		bool WaitEntered()
		{
			return _entered_future.wait_for(std::chrono::milliseconds(WAIT_TIMEOUT_MSEC)) == std::future_status::ready;
		}

		void Release()
		{
			if (_is_released.exchange(true) == false)
			{
				_released.set_value();
			}
		}

	private:
		std::shared_ptr<std::promise<void>> _entered = std::make_shared<std::promise<void>>();
		std::future<void> _entered_future			 = _entered->get_future();
		std::promise<void> _released;
		std::shared_future<void> _released_future = _released.get_future().share();
		std::atomic<bool> _is_released{false};
	};

	class TestEvents : public ov::SocketAsyncInterface
	{
	public:
		std::function<void()> on_readable;
		std::atomic<int> readable_count{0};

		void OnConnected(const std::shared_ptr<const ov::SocketError> &error) override
		{
		}

		void OnReadable() override
		{
			readable_count++;

			if (on_readable != nullptr)
			{
				on_readable();
			}
		}

		void OnClosed() override
		{
		}
	};
}  // namespace

// A socket closed right after it is added to a worker is released, leaving no entry in the worker's socket map.
// SRT is used because adding an SRT socket does not wait for the worker's first epoll event.
TEST(SocketPoolWorkerTest, SocketClosedRightAfterAddIsReleased)
{
	auto pool = ov::SocketPool::Create("test-close-leak", ov::SocketType::Srt, false);
	ASSERT_NE(pool, nullptr);
	ASSERT_TRUE(pool->Initialize(1));

	auto events = std::make_shared<TestEvents>();
	std::vector<std::weak_ptr<ov::Socket>> closed_sockets;

	for (int index = 0; index < SOCKET_COUNT; index++)
	{
		auto socket = pool->AllocSocket(ov::SocketFamily::Inet);
		ASSERT_NE(socket, nullptr);
		ASSERT_TRUE(socket->MakeNonBlocking(events));
		ASSERT_TRUE(socket->CloseImmediately());

		closed_sockets.push_back(socket);
	}

	EXPECT_TRUE(WaitUntil([&]() { return AllExpired(closed_sockets); }));

	auto description = pool->ToString();
	EXPECT_NE(description.IndexOf(EMPTY_WORKER), -1) << description.CStr();

	pool->Uninitialize();
}

// When the worker cannot add a socket to epoll, the socket is not left in the worker's socket map.
TEST(SocketPoolWorkerTest, FailedAddLeavesNoEntry)
{
	auto pool = ov::SocketPool::Create("test-add-fail", ov::SocketType::Tcp, false);
	ASSERT_NE(pool, nullptr);
	ASSERT_TRUE(pool->Initialize(1));

	auto socket = pool->AllocSocket(ov::SocketFamily::Inet);
	ASSERT_NE(socket, nullptr);

	// A regular file accepts `O_NONBLOCK`, but `epoll_ctl(EPOLL_CTL_ADD)` rejects it with `EPERM`
	int file = ::open("/proc/self/stat", O_RDONLY);
	ASSERT_GE(file, 0);
	ASSERT_EQ(::dup2(file, socket->GetNativeHandle()), socket->GetNativeHandle());
	::close(file);

	EXPECT_FALSE(socket->MakeNonBlocking(std::make_shared<TestEvents>()));

	auto description = pool->ToString();
	EXPECT_NE(description.IndexOf(EMPTY_WORKER), -1) << description.CStr();

	socket->CloseImmediately();
	pool->Uninitialize();
}

// When a callback closes another socket polled in the same batch and drops its last reference,
// the worker still holds that socket until its event is handled, and skips the event as closed.
TEST(SocketPoolWorkerTest, SocketClosedByEarlierEventInSameBatchIsSkipped)
{
	auto pool = ov::SocketPool::Create("test-batch-close", ov::SocketType::Udp, false);
	ASSERT_NE(pool, nullptr);
	ASSERT_TRUE(pool->Initialize(1));

	auto gate_events   = std::make_shared<TestEvents>();
	auto first_events  = std::make_shared<TestEvents>();
	auto second_events = std::make_shared<TestEvents>();

	auto gate		   = pool->AllocSocket(ov::SocketFamily::Inet);
	auto first		   = pool->AllocSocket(ov::SocketFamily::Inet);
	auto second		   = pool->AllocSocket(ov::SocketFamily::Inet);
	ASSERT_NE(gate, nullptr);
	ASSERT_NE(first, nullptr);
	ASSERT_NE(second, nullptr);

	// The gate holds the worker inside its callback while both other sockets become readable,
	// so the next `epoll_wait()` returns them in one batch
	WorkerGate worker_gate;
	gate_events->on_readable = worker_gate.MakeCallback();

	// `Bind()` instead of `Connect()`, since a connected socket queues a close callback that keeps it alive
	ASSERT_TRUE(gate->MakeNonBlocking(gate_events));
	ASSERT_TRUE(first->MakeNonBlocking(first_events));
	ASSERT_TRUE(second->MakeNonBlocking(second_events));

	auto gate_port	 = BindToLoopback(gate);
	auto first_port	 = BindToLoopback(first);
	auto second_port = BindToLoopback(second);
	ASSERT_NE(gate_port, 0);
	ASSERT_NE(first_port, 0);
	ASSERT_NE(second_port, 0);

	// From here, the test reaches each socket only through the callback of the other, which closes it.
	// So whichever event the batch returns first closes the other socket, whose event is then skipped,
	// while the worker's copy of the batch still holds it.
	std::weak_ptr<ov::Socket> first_watcher	 = first;
	std::weak_ptr<ov::Socket> second_watcher = second;
	auto holder_mutex						 = std::make_shared<std::mutex>();
	auto first_holder						 = std::make_shared<std::shared_ptr<ov::Socket>>(std::move(first));
	auto second_holder						 = std::make_shared<std::shared_ptr<ov::Socket>>(std::move(second));

	auto close_holder						 = [holder_mutex](const std::shared_ptr<std::shared_ptr<ov::Socket>> &holder) {
		   return [holder_mutex, holder]() {
			   std::shared_ptr<ov::Socket> socket;
			   {
				   std::lock_guard<std::mutex> lock(*holder_mutex);
				   socket = std::move(*holder);
			   }

			   if (socket != nullptr)
			   {
				   socket->CloseImmediately();
			   }
		   };
	};
	first_events->on_readable  = close_holder(second_holder);
	second_events->on_readable = close_holder(first_holder);

	ASSERT_TRUE(SendDatagram(gate_port));
	ASSERT_TRUE(worker_gate.WaitEntered());

	ASSERT_TRUE(SendDatagram(first_port));
	ASSERT_TRUE(SendDatagram(second_port));
	worker_gate.Release();

	EXPECT_TRUE(WaitUntil([&]() { return first_watcher.expired() || second_watcher.expired(); }));
	EXPECT_EQ(first_events->readable_count.load() + second_events->readable_count.load(), 1);

	for (const auto &holder : {first_holder, second_holder})
	{
		std::shared_ptr<ov::Socket> socket;
		{
			std::lock_guard<std::mutex> lock(*holder_mutex);
			socket = std::move(*holder);
		}

		if (socket != nullptr)
		{
			socket->CloseImmediately();
		}
	}

	gate->CloseImmediately();
	pool->Uninitialize();
}

// A socket closed by another thread before the worker delivers its first epoll event
// makes the pending `MakeNonBlocking()` fail instead of waiting forever.
TEST(SocketPoolWorkerTest, SocketClosedBeforeFirstEventStopsWaiting)
{
	auto pool = ov::SocketPool::Create("test-first-event", ov::SocketType::Tcp, false);
	ASSERT_NE(pool, nullptr);
	ASSERT_TRUE(pool->Initialize(1));

	uint16_t listen_port = 0;
	int listen_fd		 = ListenOnLoopback(&listen_port);
	ASSERT_GE(listen_fd, 0);

	// The gate holds the worker inside its callback, so no first epoll event is delivered meanwhile
	WorkerGate worker_gate;
	auto gate_events		 = std::make_shared<TestEvents>();
	gate_events->on_readable = worker_gate.MakeCallback();

	auto gate				 = pool->AllocSocket(ov::SocketFamily::Inet);
	ASSERT_NE(gate, nullptr);
	ASSERT_TRUE(gate->MakeNonBlocking(gate_events));
	ASSERT_EQ(gate->Connect(ov::SocketAddress::CreateAndGetFirst("127.0.0.1", listen_port), WAIT_TIMEOUT_MSEC), nullptr);
	ASSERT_TRUE(WaitUntil([&]() { return gate->GetState() == ov::SocketState::Connected; }));

	int peer_fd = ::accept(listen_fd, nullptr, nullptr);
	ASSERT_GE(peer_fd, 0);
	ASSERT_EQ(::send(peer_fd, "x", 1, 0), 1);
	ASSERT_TRUE(worker_gate.WaitEntered());

	auto socket = pool->AllocSocket(ov::SocketFamily::Inet);
	ASSERT_NE(socket, nullptr);

	// A plain thread, not `std::async()`, so that a hang fails this test instead of blocking it on the future
	auto added_promise = std::make_shared<std::promise<bool>>();
	auto added		   = added_promise->get_future();
	std::thread adder([socket, added_promise]() {
		added_promise->set_value(socket->MakeNonBlocking(std::make_shared<TestEvents>()));
	});

	// The socket is in the worker's map, so the close below waits on `_worker_mutex` until the add finishes,
	// and runs before the first epoll event whether or not `MakeNonBlocking()` is already waiting
	bool added_to_map = WaitUntil([&]() { return pool->ToString().IndexOf("socket_map: 2,") != -1; });
	EXPECT_TRUE(added_to_map);

	// Closed only once the socket is in the map, so the close lands between the add and the first epoll event
	if (added_to_map)
	{
		socket->CloseImmediately();
	}

	auto status = added.wait_for(std::chrono::milliseconds(WAIT_TIMEOUT_MSEC));
	worker_gate.Release();

	if (status == std::future_status::ready)
	{
		adder.join();
		EXPECT_FALSE(added.get());
		EXPECT_EQ(socket->GetBlockingMode(), ov::BlockingMode::Blocking);

		std::vector<std::weak_ptr<ov::Socket>> closed_sockets{socket};
		socket.reset();

		EXPECT_TRUE(WaitUntil([&]() { return AllExpired(closed_sockets); }));
		EXPECT_TRUE(WaitUntil([&]() { return pool->ToString().IndexOf("socket_map: 1,") != -1; }));
	}
	else
	{
		adder.detach();
		ADD_FAILURE() << "MakeNonBlocking() is still waiting for the first epoll event of a closed socket";
	}

	// The gate is closed before its peer, so the peer's FIN reaches no callback
	gate->CloseImmediately();
	::close(peer_fd);
	::close(listen_fd);
	pool->Uninitialize();
}

// `Close()` on a non-blocking socket whose native socket is already closed only moves the socket to `Closed`
TEST(SocketPoolWorkerTest, CloseAfterNativeCloseMovesToClosed)
{
	auto pool = ov::SocketPool::Create("test-close-after", ov::SocketType::Udp, false);
	ASSERT_NE(pool, nullptr);
	ASSERT_TRUE(pool->Initialize(1));

	auto socket = pool->AllocSocket(ov::SocketFamily::Inet);
	ASSERT_NE(socket, nullptr);
	ASSERT_TRUE(socket->MakeNonBlocking(std::make_shared<TestEvents>()));
	ASSERT_NE(BindToLoopback(socket), 0);

	// `CloseImmediately()` keeps the current state, as the worker's close keeps `Disconnected`/`Error`
	ASSERT_TRUE(socket->CloseImmediately());
	ASSERT_NE(socket->GetState(), ov::SocketState::Closed);

	EXPECT_TRUE(socket->Close());
	EXPECT_TRUE(WaitUntil([&]() { return socket->GetState() == ov::SocketState::Closed; }));

	pool->Uninitialize();
}
