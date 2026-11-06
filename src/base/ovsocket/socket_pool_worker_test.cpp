//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <base/ovlibrary/ovlibrary.h>
#include <base/ovsocket/ovsocket.h>
#include <base/ovsocket/socket_test_helpers.h>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/epoll.h>
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
	constexpr int SOCKET_COUNT = 50;
	using ome_test::BindToLoopback;
	using ome_test::ListenOnLoopback;
	using ome_test::ScopeExit;
	using ome_test::SendDatagram;
	using ome_test::TakeNumber;
	using ome_test::UniqueFd;
	using ome_test::WAIT_TIMEOUT_MSEC;
	using ome_test::WaitUntil;
	using ome_test::WaitUntilReadable;

	constexpr const char *EMPTY_WORKER = "socket_map: 0,";

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
		std::function<void()> on_closed;
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
			if (on_closed != nullptr)
			{
				on_closed();
			}
		}
	};

	class SocketPoolWorkerTest : public ome_test::PoolTest
	{
	};
}  // namespace

// A socket closed right after it is added to a worker is released, leaving no entry in the worker's socket map.
// SRT is used because adding an SRT socket does not wait for the worker's first epoll event.
TEST_F(SocketPoolWorkerTest, SocketClosedRightAfterAddIsReleased)
{
	auto pool = CreatePool("test-close-leak", ov::SocketType::Srt);
	ASSERT_NE(pool, nullptr);

	auto events = std::make_shared<TestEvents>();
	std::vector<std::weak_ptr<ov::Socket>> closed_sockets;

	for (int index = 0; index < SOCKET_COUNT; index++)
	{
		auto socket = pool->AllocSocket(ov::SocketFamily::Inet);
		ASSERT_NE(socket, nullptr);
		ScopeExit closer([&]() { socket->CloseImmediately(); });

		ASSERT_TRUE(socket->MakeNonBlocking(events));
		ASSERT_TRUE(socket->CloseImmediately());

		closed_sockets.push_back(socket);
	}

	EXPECT_TRUE(WaitUntil([&]() { return AllExpired(closed_sockets); }));

	auto description = pool->ToString();
	EXPECT_NE(description.IndexOf(EMPTY_WORKER), -1) << description.CStr();
}

// When the worker cannot add a socket to epoll, the socket is not left in the worker's socket map.
TEST_F(SocketPoolWorkerTest, FailedAddLeavesNoEntry)
{
	auto pool = CreatePool("test-add-fail", ov::SocketType::Tcp);
	ASSERT_NE(pool, nullptr);

	auto socket = pool->AllocSocket(ov::SocketFamily::Inet);
	ASSERT_NE(socket, nullptr);
	ScopeExit closer([&]() { socket->CloseImmediately(); });

	// A regular file accepts `O_NONBLOCK`, but `epoll_ctl(EPOLL_CTL_ADD)` rejects it with `EPERM`
	UniqueFd file(::open("/proc/self/stat", O_RDONLY));
	ASSERT_GE(file.Get(), 0);
	ASSERT_EQ(::dup2(file.Get(), socket->GetNativeHandle()), socket->GetNativeHandle());

	EXPECT_FALSE(socket->MakeNonBlocking(std::make_shared<TestEvents>()));

	auto description = pool->ToString();
	EXPECT_NE(description.IndexOf(EMPTY_WORKER), -1) << description.CStr();
}

// When a callback closes another socket polled in the same batch and drops its last reference,
// the worker still holds that socket until its event is handled, and the event does not reach its callback.
TEST_F(SocketPoolWorkerTest, SocketClosedByEarlierEventInSameBatchStaysAliveUntilItsEvent)
{
	auto pool = CreatePool("test-batch-close", ov::SocketType::Udp);
	ASSERT_NE(pool, nullptr);

	auto gate_events   = std::make_shared<TestEvents>();
	auto first_events  = std::make_shared<TestEvents>();
	auto second_events = std::make_shared<TestEvents>();

	auto gate		   = pool->AllocSocket(ov::SocketFamily::Inet);
	auto first		   = pool->AllocSocket(ov::SocketFamily::Inet);
	auto second		   = pool->AllocSocket(ov::SocketFamily::Inet);

	// Closes the sockets on every way out, including an early return.
	// `first` and `second` move to the holders below, which are closed the same way.
	ScopeExit closer([&]() {
		for (auto &socket : {gate, first, second})
		{
			if (socket != nullptr)
			{
				socket->CloseImmediately();
			}
		}
	});

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
	// So whichever event the batch returns first closes the other socket,
	// which only the worker's copy of the batch then holds until its own event is handled.
	std::weak_ptr<ov::Socket> first_watcher	 = first;
	std::weak_ptr<ov::Socket> second_watcher = second;
	auto first_handle						 = first->GetNativeHandle();
	auto second_handle						 = second->GetNativeHandle();
	auto holder_mutex						 = std::make_shared<std::mutex>();
	auto first_holder						 = std::make_shared<std::shared_ptr<ov::Socket>>(std::move(first));
	auto second_holder						 = std::make_shared<std::shared_ptr<ov::Socket>>(std::move(second));
	auto alive_after_close					 = std::make_shared<std::atomic<bool>>(false);

	// Takes a socket out of its holder, so only one of the callbacks and the cleanup below gets it
	auto take								 = [holder_mutex](const std::shared_ptr<std::shared_ptr<ov::Socket>> &holder) {
		std::lock_guard<std::mutex> lock(*holder_mutex);
		return std::move(*holder);
	};

	ScopeExit holder_closer([take, first_holder, second_holder]() {
		for (const auto &holder : {first_holder, second_holder})
		{
			auto socket = take(holder);

			if (socket != nullptr)
			{
				socket->CloseImmediately();
			}
		}
	});

	auto close_holder = [take, alive_after_close](const std::shared_ptr<std::shared_ptr<ov::Socket>> &holder, const std::weak_ptr<ov::Socket> &watcher) {
		return [take, alive_after_close, holder, watcher]() {
			auto socket = take(holder);

			if (socket != nullptr)
			{
				socket->CloseImmediately();
				socket.reset();
				*alive_after_close = (watcher.expired() == false);
			}
		};
	};
	first_events->on_readable  = close_holder(second_holder, second_watcher);
	second_events->on_readable = close_holder(first_holder, first_watcher);

	ASSERT_TRUE(SendDatagram(gate_port));
	ASSERT_TRUE(worker_gate.WaitEntered());

	ASSERT_TRUE(SendDatagram(first_port));
	ASSERT_TRUE(SendDatagram(second_port));
	ASSERT_TRUE(WaitUntilReadable(first_handle));
	ASSERT_TRUE(WaitUntilReadable(second_handle));
	worker_gate.Release();

	EXPECT_TRUE(WaitUntil([&]() { return first_watcher.expired() || second_watcher.expired(); }));
	EXPECT_TRUE(alive_after_close->load());
	EXPECT_EQ(first_events->readable_count.load() + second_events->readable_count.load(), 1);
}

// A socket closed by another thread before the worker delivers its first epoll event
// makes the pending `MakeNonBlocking()` fail instead of waiting forever.
TEST_F(SocketPoolWorkerTest, SocketClosedBeforeFirstEventStopsWaiting)
{
	auto pool = CreatePool("test-first-event", ov::SocketType::Tcp);
	ASSERT_NE(pool, nullptr);

	uint16_t listen_port = 0;
	auto listen_fd		 = ListenOnLoopback(&listen_port);
	ASSERT_GE(listen_fd.Get(), 0);

	// The gate holds the worker inside its callback, so no first epoll event is delivered meanwhile
	WorkerGate worker_gate;
	auto gate_events		 = std::make_shared<TestEvents>();
	gate_events->on_readable = worker_gate.MakeCallback();

	auto gate				 = pool->AllocSocket(ov::SocketFamily::Inet);
	ASSERT_NE(gate, nullptr);
	ScopeExit gate_closer([&]() { gate->CloseImmediately(); });

	ASSERT_TRUE(gate->MakeNonBlocking(gate_events));
	ASSERT_EQ(gate->Connect(ov::SocketAddress::CreateAndGetFirst("127.0.0.1", listen_port), WAIT_TIMEOUT_MSEC), nullptr);
	ASSERT_TRUE(WaitUntil([&]() { return gate->GetState() == ov::SocketState::Connected; }));

	UniqueFd peer_fd(::accept(listen_fd.Get(), nullptr, nullptr));
	ASSERT_GE(peer_fd.Get(), 0);
	ASSERT_EQ(::send(peer_fd.Get(), "x", 1, 0), 1);
	ASSERT_TRUE(worker_gate.WaitEntered());

	auto socket = pool->AllocSocket(ov::SocketFamily::Inet);
	ASSERT_NE(socket, nullptr);
	ScopeExit socket_closer([&]() {
		if (socket != nullptr)
		{
			socket->CloseImmediately();
		}
	});

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
}

// `Close()` on a non-blocking socket that another close has already run on moves the socket to `Closed` at once,
// without queueing a command
TEST_F(SocketPoolWorkerTest, CloseAfterCloseStartedMovesToClosed)
{
	auto pool = CreatePool("test-close-after", ov::SocketType::Udp);
	ASSERT_NE(pool, nullptr);

	auto socket = pool->AllocSocket(ov::SocketFamily::Inet);
	ASSERT_NE(socket, nullptr);
	ScopeExit closer([&]() { socket->CloseImmediately(); });

	ASSERT_TRUE(socket->MakeNonBlocking(std::make_shared<TestEvents>()));
	ASSERT_NE(BindToLoopback(socket), 0);

	// The same close as the worker's on `EPOLLHUP`, which leaves the socket `Disconnected`
	ASSERT_TRUE(socket->CloseImmediatelyWithState(ov::SocketState::Disconnected));
	ASSERT_EQ(socket->GetState(), ov::SocketState::Disconnected);

	EXPECT_TRUE(socket->Close());
	EXPECT_EQ(socket->GetState(), ov::SocketState::Closed);
	EXPECT_FALSE(socket->HasCommand());
}

// A socket cannot be added to a worker that has stopped, and leaves no entry in it
TEST_F(SocketPoolWorkerTest, AddToStoppedWorkerFails)
{
	auto pool = CreatePool("test-add-stopped", ov::SocketType::Udp);
	ASSERT_NE(pool, nullptr);

	auto socket = pool->AllocSocket(ov::SocketFamily::Inet);
	ASSERT_NE(socket, nullptr);

	pool->Uninitialize();

	EXPECT_FALSE(socket->MakeNonBlocking(std::make_shared<TestEvents>()));
	EXPECT_EQ(socket->GetBlockingMode(), ov::BlockingMode::Blocking);

	auto description = socket->GetSocketPoolWorker()->ToString();
	EXPECT_NE(description.IndexOf(EMPTY_WORKER), -1) << description.CStr();

	socket->CloseImmediately();
}

// The worker's epoll handle is closed by `Uninitialize()` even while a socket still holds the worker,
// and an add that follows is refused before it reaches the number that another file now uses
TEST_F(SocketPoolWorkerTest, EpollHandleIsClosedByUninitialize)
{
	auto pool = CreatePool("test-epoll-lifetime", ov::SocketType::Udp);
	ASSERT_NE(pool, nullptr);

	auto socket = pool->AllocSocket(ov::SocketFamily::Inet);
	ASSERT_NE(socket, nullptr);
	ScopeExit closer([&]() { socket->CloseImmediately(); });

	const int epoll_handle = socket->GetSocketPoolWorker()->GetNativeHandle();
	ASSERT_NE(::fcntl(epoll_handle, F_GETFD), -1);

	pool->Uninitialize();

	EXPECT_EQ(::fcntl(epoll_handle, F_GETFD), -1);

	// The number now names an epoll instance of the test, which may already have taken the free number
	auto other_epoll = TakeNumber(UniqueFd(::epoll_create1(0)), epoll_handle);
	ASSERT_EQ(other_epoll.Get(), epoll_handle);

	// A plain thread, so that an add that reaches the test's epoll and waits for its first event fails this test
	auto added_promise = std::make_shared<std::promise<bool>>();
	auto added		   = added_promise->get_future();
	std::thread adder([socket, added_promise]() {
		added_promise->set_value(socket->MakeNonBlocking(std::make_shared<TestEvents>()));
	});

	if (added.wait_for(std::chrono::milliseconds(WAIT_TIMEOUT_MSEC)) == std::future_status::ready)
	{
		adder.join();
		EXPECT_FALSE(added.get());
	}
	else
	{
		adder.detach();
		ADD_FAILURE() << "MakeNonBlocking() reached the epoll number of the stopped worker";
	}

	const int result	 = ::epoll_ctl(epoll_handle, EPOLL_CTL_DEL, socket->GetNativeHandle(), nullptr);
	const int error_code = errno;
	EXPECT_EQ(result, -1);
	EXPECT_EQ(error_code, ENOENT);

	socket->CloseImmediately();
}

// A socket that leaves the worker after `Uninitialize()` does not call into the closed epoll number,
// which another epoll instance now uses
TEST_F(SocketPoolWorkerTest, DeleteAfterUninitializeSkipsReusedNumber)
{
	auto pool = CreatePool("test-epoll-late-delete", ov::SocketType::Udp);
	ASSERT_NE(pool, nullptr);

	auto socket = pool->AllocSocket(ov::SocketFamily::Inet);
	ASSERT_NE(socket, nullptr);
	ScopeExit closer([&]() { socket->CloseImmediately(); });
	ASSERT_TRUE(socket->MakeNonBlocking(std::make_shared<TestEvents>()));
	auto port = BindToLoopback(socket);
	ASSERT_NE(port, 0);

	const int epoll_handle = socket->GetSocketPoolWorker()->GetNativeHandle();

	// Not closable, so the stopping worker does not close it, and the socket still counts itself added to the worker
	socket->SetState(ov::SocketState::Disconnected);
	pool->Uninitialize();

	auto other_epoll = TakeNumber(UniqueFd(::epoll_create1(0)), epoll_handle);
	ASSERT_EQ(other_epoll.Get(), epoll_handle);

	// The test epoll watches the socket's file under the socket's number.
	// A duplicate keeps the file open, so the registration outlives the socket's own descriptor.
	UniqueFd file(::dup(socket->GetNativeHandle()));
	ASSERT_GE(file.Get(), 0);

	epoll_event event{};
	event.events = EPOLLIN;
	ASSERT_EQ(::epoll_ctl(epoll_handle, EPOLL_CTL_ADD, socket->GetNativeHandle(), &event), 0);

	EXPECT_TRUE(socket->CloseImmediately());

	// The registration is still there, so the test epoll reports the datagram
	ASSERT_TRUE(SendDatagram(port));
	epoll_event ready{};
	EXPECT_EQ(::epoll_wait(epoll_handle, &ready, 1, WAIT_TIMEOUT_MSEC), 1);
}

// A socket closed without ever joining epoll no longer counts toward its worker,
// so the idle thread-per-socket worker is released
TEST_F(SocketPoolWorkerTest, SocketClosedOutsideEpollReleasesWorker)
{
	// Several ticks of the 1 s timer that releases idle workers
	constexpr int RELEASE_WAIT_MSEC = 5000;

	auto pool						= CreatePool("test-count-close", ov::SocketType::Tcp, true);
	ASSERT_NE(pool, nullptr);

	auto socket = pool->AllocSocket(ov::SocketFamily::Inet);
	ASSERT_NE(socket, nullptr);
	EXPECT_NE(pool->ToString().IndexOf("workers: 1"), -1);

	EXPECT_TRUE(socket->CloseImmediately());
	EXPECT_TRUE(WaitUntil([&]() { return pool->ToString().IndexOf("workers: 0") != -1; }, RELEASE_WAIT_MSEC));
}

// A socket closed after joining epoll stops counting toward its worker too, so the idle worker is released
TEST_F(SocketPoolWorkerTest, SocketClosedInEpollReleasesWorker)
{
	// Several ticks of the 1 s timer that releases idle workers
	constexpr int RELEASE_WAIT_MSEC = 5000;

	auto pool						= CreatePool("test-count-epoll", ov::SocketType::Udp, true);
	ASSERT_NE(pool, nullptr);

	auto socket = pool->AllocSocket(ov::SocketFamily::Inet);
	ASSERT_NE(socket, nullptr);
	ScopeExit closer([&]() { socket->CloseImmediately(); });

	ASSERT_TRUE(socket->MakeNonBlocking(std::make_shared<TestEvents>()));
	EXPECT_NE(pool->ToString().IndexOf("workers: 1"), -1);

	EXPECT_TRUE(socket->CloseImmediately());
	EXPECT_TRUE(WaitUntil([&]() { return pool->ToString().IndexOf("workers: 0") != -1; }, RELEASE_WAIT_MSEC));
}

// A thread-per-socket worker is not released as idle while the close callback of its socket is still to run
TEST_F(SocketPoolWorkerTest, WorkerIsNotReleasedBeforeCloseCallback)
{
	// Several ticks of the 1 s timer that releases idle workers
	constexpr int RELEASE_WAIT_MSEC = 5000;
	constexpr int HOLD_MSEC			= 2500;

	auto pool						= CreatePool("test-close-callback", ov::SocketType::Tcp, true);
	ASSERT_NE(pool, nullptr);

	uint16_t listen_port = 0;
	auto listen_fd		 = ListenOnLoopback(&listen_port);
	ASSERT_GE(listen_fd.Get(), 0);

	// The gate holds the worker inside the close callback
	WorkerGate worker_gate;
	auto events		  = std::make_shared<TestEvents>();
	events->on_closed = worker_gate.MakeCallback();

	auto socket		  = pool->AllocSocket(ov::SocketFamily::Inet);
	ASSERT_NE(socket, nullptr);
	ScopeExit closer([&]() { socket->CloseImmediately(); });

	ASSERT_TRUE(socket->MakeNonBlocking(events));
	ASSERT_EQ(socket->Connect(ov::SocketAddress::CreateAndGetFirst("127.0.0.1", listen_port), WAIT_TIMEOUT_MSEC), nullptr);
	ASSERT_TRUE(WaitUntil([&]() { return socket->GetState() == ov::SocketState::Connected; }));

	EXPECT_TRUE(socket->CloseImmediately());
	ASSERT_TRUE(worker_gate.WaitEntered());

	std::this_thread::sleep_for(std::chrono::milliseconds(HOLD_MSEC));
	EXPECT_NE(pool->ToString().IndexOf("workers: 1"), -1) << "The worker was released before the close callback returned";

	worker_gate.Release();
	EXPECT_TRUE(WaitUntil([&]() { return pool->ToString().IndexOf("workers: 0") != -1; }, RELEASE_WAIT_MSEC));
}

// An uninitialized thread-per-socket pool creates no worker, so none is left running without an owner to stop it
TEST_F(SocketPoolWorkerTest, UninitializedPoolCreatesNoWorker)
{
	auto pool = CreatePool("test-no-worker", ov::SocketType::Tcp, true);
	ASSERT_NE(pool, nullptr);

	pool->Uninitialize();

	EXPECT_EQ(pool->AllocSocket(ov::SocketFamily::Inet), nullptr);
	EXPECT_NE(pool->ToString().IndexOf("workers: 0"), -1);
}

// `CloseImmediately()` moves the socket to `Closed`, so a later `Close()` of a blocking socket returns at once
TEST_F(SocketPoolWorkerTest, CloseImmediatelyMovesToClosed)
{
	auto pool = CreatePool("test-close-now", ov::SocketType::Tcp);
	ASSERT_NE(pool, nullptr);

	auto socket = pool->AllocSocket(ov::SocketFamily::Inet);
	ASSERT_NE(socket, nullptr);
	EXPECT_EQ(socket->GetBlockingMode(), ov::BlockingMode::Blocking);

	ASSERT_TRUE(socket->CloseImmediately());
	EXPECT_EQ(socket->GetState(), ov::SocketState::Closed);
	EXPECT_FALSE(socket->Close());
}
