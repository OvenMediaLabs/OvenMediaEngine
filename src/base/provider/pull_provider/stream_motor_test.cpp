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

#include <algorithm>
#include <chrono>
#include <filesystem>
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

	// A stream that is never `PLAYING`, so the motor deletes it on the first readable event of its socket
	class SocketPullStream : public pvd::PullStream
	{
	public:
		SocketPullStream()
			: pvd::PullStream(std::shared_ptr<pvd::Application>(), info::Stream(StreamSourceType::Ovt), {}, std::make_shared<pvd::PullStreamProperties>())
		{
		}

		// Read only by `StreamMotor::AddStream()`, which the test thread calls
		std::shared_ptr<ov::Socket> socket;

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
			return true;
		}
	};

	class StreamMotorTest : public ome_test::PoolTest
	{
	};
}  // namespace

// A stream added again with another socket leaves the motor's epoll with its old socket
TEST_F(StreamMotorTest, ReconnectedStreamLeavesOldSocket)
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

	ASSERT_TRUE(motor.Start());

	auto stream	   = std::make_shared<SocketPullStream>();

	stream->socket = old_socket;
	ASSERT_TRUE(motor.AddStream(stream));

	stream->socket = new_socket;
	ASSERT_TRUE(motor.AddStream(stream));
	ASSERT_EQ(motor.GetStreamCount(), 1u);

	// An event on the old socket no longer reaches the stream
	ASSERT_TRUE(SendDatagram(old_port));
	ASSERT_TRUE(WaitUntilReadable(old_socket->GetNativeHandle()));
	std::this_thread::sleep_for(std::chrono::milliseconds(QUIET_MSEC));
	EXPECT_EQ(motor.GetStreamCount(), 1u);

	// An event on the new socket does, and the motor deletes the stream that is not playing
	ASSERT_TRUE(SendDatagram(new_port));
	EXPECT_TRUE(WaitUntil([&]() { return motor.GetStreamCount() == 0; }));
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

	const auto before = OpenEpollNumbers();
	ASSERT_TRUE(motor.Start());
	const auto after = OpenEpollNumbers();

	std::vector<int> opened;
	std::set_difference(after.begin(), after.end(), before.begin(), before.end(), std::back_inserter(opened));
	ASSERT_EQ(opened.size(), 1u);
	const int number = opened.front();

	EXPECT_TRUE(motor.Stop());

	auto taken = TakeNumber(UniqueFd(::epoll_create1(0)), number);
	ASSERT_EQ(taken.Get(), number);

	EXPECT_TRUE(motor.Stop());
	EXPECT_NE(::fcntl(number, F_GETFD), -1);
}
