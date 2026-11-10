//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <base/ovsocket/ovsocket.h>
#include <base/ovsocket/socket_test_helpers.h>
#include <base/provider/pull_provider/stream.h>
#include <base/provider/pull_provider/stream_motor.h>
#include <base/provider/pull_provider/stream_props.h>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <future>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace
{
	using ome_test::BindToLoopback;
	using ome_test::ScopeExit;
	using ome_test::SendDatagram;
	using ome_test::TakeNumber;
	using ome_test::UniqueFd;
	using ome_test::WaitUntil;
	using ome_test::WaitUntilReadable;

	constexpr int QUIET_MSEC = 300;

	// The numbers of the epoll instances open in this process
	std::set<int> OpenEpollNumbers()
	{
		std::set<int> numbers;

		for (const auto &entry : std::filesystem::directory_iterator("/proc/self/fd"))
		{
			std::error_code error;
			auto target = std::filesystem::read_symlink(entry.path(), error);

			if ((error.value() == 0) && (target == "anon_inode:[eventpoll]"))
			{
				numbers.insert(std::stoi(entry.path().filename().string()));
			}
		}

		return numbers;
	}

	// Starts `motor` and returns the number of the epoll instance it opened, or -1
	int StartAndGetEpollNumber(pvd::StreamMotor &motor)
	{
		const auto before = OpenEpollNumbers();

		if (motor.Start() == false)
		{
			return -1;
		}

		const auto after = OpenEpollNumbers();

		std::vector<int> opened;
		std::set_difference(after.begin(), after.end(), before.begin(), before.end(), std::back_inserter(opened));

		return (opened.size() == 1) ? opened.front() : -1;
	}

	// A stream that is never `PLAYING`, so the motor deletes it on the first readable event of its socket
	class SocketPullStream : public pvd::PullStream
	{
	public:
		SocketPullStream()
			: pvd::PullStream(std::shared_ptr<pvd::Application>(), info::Stream(StreamSourceType::Ovt), {}, std::make_shared<pvd::PullStreamProperties>())
		{
		}

		// Set on the test thread before each `StreamMotor::AddStream()`, which publishes it to the motor's worker
		std::shared_ptr<ov::Socket> socket;
		std::atomic<int> stop_count{0};

		ProcessMediaEventTrigger GetProcessMediaEventTriggerMode() override
		{
			return ProcessMediaEventTrigger::TRIGGER_EPOLL;
		}

		std::shared_ptr<ov::Socket> GetSocketForDetectingEvent() override
		{
			return socket;
		}

		ProcessMediaResult ProcessMediaPacket() override
		{
			return ProcessMediaResult::PROCESS_MEDIA_FINISH;
		}

	protected:
		bool StartStream(const std::shared_ptr<const ov::Url> &url) override
		{
			return false;
		}

		bool RestartStream(const std::shared_ptr<const ov::Url> &url) override
		{
			return false;
		}

		bool StopStream() override
		{
			stop_count++;
			return true;
		}
	};

	// A playing stream that holds the motor's worker in each of its first `HOLD_COUNT` packet calls until `Release()`,
	// and drains its socket in the calls after them
	class HoldingPullStream : public SocketPullStream
	{
	public:
		static constexpr int HOLD_COUNT = 2;

		HoldingPullStream()
		{
			SetState(State::PLAYING);

			for (int index = 0; index < HOLD_COUNT; index++)
			{
				_entered_futures[index]	 = _entered[index].get_future();
				_released_futures[index] = _released[index].get_future().share();
			}
		}

		bool WaitEntered(int index)
		{
			return _entered_futures[index].wait_for(std::chrono::milliseconds(ome_test::WAIT_TIMEOUT_MSEC)) == std::future_status::ready;
		}

		void Release(int index)
		{
			if (_is_released[index].exchange(true) == false)
			{
				_released[index].set_value();
			}
		}

		void ReleaseAll()
		{
			for (int index = 0; index < HOLD_COUNT; index++)
			{
				Release(index);
			}
		}

		ProcessMediaResult ProcessMediaPacket() override
		{
			const int index = _call_count++;

			if (index < HOLD_COUNT)
			{
				_entered[index].set_value();
				_released_futures[index].wait();
			}
			else
			{
				char buffer[64];
				while (::recv(socket->GetNativeHandle(), buffer, sizeof(buffer), MSG_DONTWAIT) > 0)
				{
				}
			}

			return ProcessMediaResult::PROCESS_MEDIA_TRY_AGAIN;
		}

		int GetCallCount() const
		{
			return _call_count;
		}

	private:
		std::promise<void> _entered[HOLD_COUNT];
		std::future<void> _entered_futures[HOLD_COUNT];
		std::promise<void> _released[HOLD_COUNT];
		std::shared_future<void> _released_futures[HOLD_COUNT];
		std::atomic<bool> _is_released[HOLD_COUNT] = {};
		std::atomic<int> _call_count{0};
	};

	class StreamMotorTest : public ome_test::PoolTest
	{
	};
}  // namespace

// Adding a stream again with another socket removes its old socket from the motor's epoll
TEST_F(StreamMotorTest, ReAddedStreamDropsOldSocketFromEpoll)
{
	auto pool = CreatePool("test-motor-swap", ov::SocketType::Udp);
	ASSERT_NE(pool, nullptr);

	auto old_socket = pool->AllocSocket(ov::SocketFamily::Inet);
	auto new_socket = pool->AllocSocket(ov::SocketFamily::Inet);
	pvd::StreamMotor motor{1};

	ScopeExit cleanup([&]() {
		motor.Stop();

		for (const auto &socket : {old_socket, new_socket})
		{
			if (socket != nullptr)
			{
				socket->CloseImmediately();
			}
		}
	});

	ASSERT_NE(old_socket, nullptr);
	ASSERT_NE(new_socket, nullptr);

	auto old_port = BindToLoopback(old_socket);
	auto new_port = BindToLoopback(new_socket);
	ASSERT_NE(old_port, 0);
	ASSERT_NE(new_port, 0);

	const int motor_epoll = StartAndGetEpollNumber(motor);
	ASSERT_GE(motor_epoll, 0);

	auto stream	   = std::make_shared<SocketPullStream>();

	stream->socket = old_socket;
	ASSERT_TRUE(motor.AddStream(stream));

	stream->socket = new_socket;
	ASSERT_TRUE(motor.AddStream(stream));
	ASSERT_EQ(motor.GetStreamCount(), 1u);

	// The old socket is no longer registered in the motor's epoll
	const int result = ::epoll_ctl(motor_epoll, EPOLL_CTL_DEL, old_socket->GetNativeHandle(), nullptr);
	const int error	 = errno;
	EXPECT_EQ(result, -1);
	EXPECT_EQ(error, ENOENT);

	// An event on the old socket no longer reaches the stream
	ASSERT_TRUE(SendDatagram(old_port));
	ASSERT_TRUE(WaitUntilReadable(old_socket->GetNativeHandle()));
	std::this_thread::sleep_for(std::chrono::milliseconds(QUIET_MSEC));
	EXPECT_EQ(motor.GetStreamCount(), 1u);

	// An event on the new socket does, and the motor deletes the stream that is not playing
	ASSERT_TRUE(SendDatagram(new_port));
	EXPECT_TRUE(WaitUntil([&]() { return motor.GetStreamCount() == 0; }));
}

// An event of the old socket that the worker has already polled does not reach the stream added again with a new socket
TEST_F(StreamMotorTest, PolledEventOfOldSocketDoesNotReachReAddedStream)
{
	auto pool = CreatePool("test-motor-stale-event", ov::SocketType::Udp);
	ASSERT_NE(pool, nullptr);

	auto holder_socket = pool->AllocSocket(ov::SocketFamily::Inet);
	auto old_socket	   = pool->AllocSocket(ov::SocketFamily::Inet);
	auto new_socket	   = pool->AllocSocket(ov::SocketFamily::Inet);
	auto holder		   = std::make_shared<HoldingPullStream>();
	pvd::StreamMotor motor{1};

	ScopeExit cleanup([&]() {
		holder->ReleaseAll();
		motor.Stop();

		for (const auto &socket : {holder_socket, old_socket, new_socket})
		{
			if (socket != nullptr)
			{
				socket->CloseImmediately();
			}
		}
	});

	ASSERT_NE(holder_socket, nullptr);
	ASSERT_NE(old_socket, nullptr);
	ASSERT_NE(new_socket, nullptr);

	auto holder_port = BindToLoopback(holder_socket);
	auto old_port	 = BindToLoopback(old_socket);
	ASSERT_NE(holder_port, 0);
	ASSERT_NE(old_port, 0);
	ASSERT_NE(BindToLoopback(new_socket), 0);

	ASSERT_TRUE(motor.Start());

	auto stream = std::make_shared<SocketPullStream>();
	holder->SetId(1);
	holder->socket = holder_socket;
	stream->SetId(2);
	stream->socket = old_socket;
	ASSERT_TRUE(motor.AddStream(holder));
	ASSERT_TRUE(motor.AddStream(stream));

	// The old socket becomes readable while the first hold keeps the worker busy. The holder's socket stays readable,
	// and epoll queues a level-triggered event again as it reports it, so the next batch has the holder's event first.
	ASSERT_TRUE(SendDatagram(holder_port));
	ASSERT_TRUE(holder->WaitEntered(0));
	ASSERT_TRUE(SendDatagram(old_port));
	ASSERT_TRUE(WaitUntilReadable(old_socket->GetNativeHandle()));
	holder->Release(0);

	// The second hold stops the worker between the two events of that batch while the stream is added again
	ASSERT_TRUE(holder->WaitEntered(1));
	stream->socket = new_socket;
	ASSERT_TRUE(motor.AddStream(stream));
	holder->Release(1);

	// A third call comes from a later batch, so the event of the old socket has been handled by then
	EXPECT_TRUE(WaitUntil([&]() { return holder->GetCallCount() > HoldingPullStream::HOLD_COUNT; }));
	EXPECT_EQ(motor.GetStreamCount(), 2u);
	EXPECT_EQ(stream->stop_count.load(), 0);
}

// A stream whose socket was closed is still deleted, and the motor drops its reference to that socket
TEST_F(StreamMotorTest, DelStreamAfterSocketClose)
{
	auto pool = CreatePool("test-motor-closed", ov::SocketType::Udp);
	ASSERT_NE(pool, nullptr);

	auto socket = pool->AllocSocket(ov::SocketFamily::Inet);
	pvd::StreamMotor motor{1};

	ScopeExit cleanup([&]() {
		motor.Stop();

		if (socket != nullptr)
		{
			socket->CloseImmediately();
		}
	});

	ASSERT_NE(socket, nullptr);
	ASSERT_NE(BindToLoopback(socket), 0);
	ASSERT_TRUE(motor.Start());

	auto stream	   = std::make_shared<SocketPullStream>();
	stream->socket = socket;
	ASSERT_TRUE(motor.AddStream(stream));

	std::weak_ptr<ov::Socket> socket_watcher = socket;
	EXPECT_TRUE(socket->CloseImmediately());
	stream->socket.reset();
	socket.reset();

	// The motor's registration is the last reference
	EXPECT_FALSE(socket_watcher.expired());

	EXPECT_TRUE(motor.DelStream(stream));
	EXPECT_EQ(motor.GetStreamCount(), 0u);
	EXPECT_TRUE(socket_watcher.expired());
}

// `Stop()` stops every stream and drops the motor's references to the streams and their sockets
TEST_F(StreamMotorTest, StopReleasesStreamsAndSockets)
{
	auto pool = CreatePool("test-motor-stop", ov::SocketType::Udp);
	ASSERT_NE(pool, nullptr);

	auto socket = pool->AllocSocket(ov::SocketFamily::Inet);
	pvd::StreamMotor motor{1};

	ScopeExit cleanup([&]() {
		motor.Stop();

		if (socket != nullptr)
		{
			socket->CloseImmediately();
		}
	});

	ASSERT_NE(socket, nullptr);
	ASSERT_NE(BindToLoopback(socket), 0);
	ASSERT_TRUE(motor.Start());

	auto stream	   = std::make_shared<SocketPullStream>();
	stream->socket = socket;
	ASSERT_TRUE(motor.AddStream(stream));

	std::weak_ptr<ov::Socket> socket_watcher	  = socket;
	std::weak_ptr<pvd::PullStream> stream_watcher = stream;
	EXPECT_TRUE(socket->CloseImmediately());
	stream->socket.reset();
	socket.reset();
	stream.reset();

	EXPECT_TRUE(motor.Stop());
	EXPECT_EQ(motor.GetStreamCount(), 0u);
	EXPECT_TRUE(stream_watcher.expired());
	EXPECT_TRUE(socket_watcher.expired());
}

// A second `Stop()` leaves alone the file that has taken the number of the epoll instance closed by the first one
TEST_F(StreamMotorTest, SecondStopLeavesReusedNumber)
{
	pvd::StreamMotor motor{1};
	ScopeExit cleanup([&]() { motor.Stop(); });

	const int number = StartAndGetEpollNumber(motor);
	ASSERT_GE(number, 0);

	EXPECT_TRUE(motor.Stop());

	auto taken = TakeNumber(UniqueFd(::epoll_create1(0)), number);
	ASSERT_EQ(taken.Get(), number);

	EXPECT_TRUE(motor.Stop());
	EXPECT_NE(::fcntl(number, F_GETFD), -1);
}
