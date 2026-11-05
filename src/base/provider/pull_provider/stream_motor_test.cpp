//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <arpa/inet.h>
#include <base/ovsocket/ovsocket.h>
#include <base/ovsocket/socket_test_helpers.h>
#include <base/provider/pull_provider/stream.h>
#include <base/provider/pull_provider/stream_motor.h>
#include <base/provider/pull_provider/stream_props.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <utility>

namespace
{
	using ome_test::BindToLoopback;
	using ome_test::ScopeExit;
	using ome_test::SendDatagram;
	using ome_test::WAIT_TIMEOUT_MSEC;
	using ome_test::WaitUntil;

	constexpr int QUIET_MSEC		= 300;

	// A motor that is stopped once, by `Stop()` or on destruction, since stopping twice closes its epoll handle twice
	class RunningMotor
	{
	public:
		~RunningMotor()
		{
			Stop();
		}

		bool Start()
		{
			_started = _motor.Start();
			return _started;
		}

		bool Stop()
		{
			return std::exchange(_started, false) ? _motor.Stop() : false;
		}

		pvd::StreamMotor *operator->()
		{
			return &_motor;
		}

	private:
		pvd::StreamMotor _motor{1};
		bool _started = false;
	};

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
}  // namespace

// A stream added again with another socket leaves the motor's epoll with its old socket
TEST(StreamMotorTest, ReconnectedStreamLeavesOldSocket)
{
	auto pool = ov::SocketPool::Create("test-motor-swap", ov::SocketType::Udp, false);
	ASSERT_NE(pool, nullptr);
	ASSERT_TRUE(pool->Initialize(1));

	auto old_socket = pool->AllocSocket(ov::SocketFamily::Inet);
	auto new_socket = pool->AllocSocket(ov::SocketFamily::Inet);
	RunningMotor motor;

	ScopeExit cleanup([&]() {
		motor.Stop();

		for (const auto &socket : {old_socket, new_socket})
		{
			if (socket != nullptr)
			{
				socket->CloseImmediately();
			}
		}

		pool->Uninitialize();
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
	ASSERT_TRUE(motor->AddStream(stream));

	stream->socket = new_socket;
	ASSERT_TRUE(motor->AddStream(stream));
	ASSERT_EQ(motor->GetStreamCount(), 1u);

	// An event on the old socket no longer reaches the stream
	ASSERT_TRUE(SendDatagram(old_port));
	std::this_thread::sleep_for(std::chrono::milliseconds(QUIET_MSEC));
	EXPECT_EQ(motor->GetStreamCount(), 1u);

	// An event on the new socket does, and the motor deletes the stream that is not playing
	ASSERT_TRUE(SendDatagram(new_port));
	EXPECT_TRUE(WaitUntil([&]() { return motor->GetStreamCount() == 0; }));
}

// A stream whose socket was closed is still deleted, and the motor drops its reference to that socket
TEST(StreamMotorTest, DelStreamAfterSocketClose)
{
	auto pool = ov::SocketPool::Create("test-motor-closed", ov::SocketType::Udp, false);
	ASSERT_NE(pool, nullptr);
	ASSERT_TRUE(pool->Initialize(1));

	auto socket = pool->AllocSocket(ov::SocketFamily::Inet);
	RunningMotor motor;

	ScopeExit cleanup([&]() {
		motor.Stop();

		if (socket != nullptr)
		{
			socket->CloseImmediately();
		}

		pool->Uninitialize();
	});

	ASSERT_NE(socket, nullptr);
	ASSERT_NE(BindToLoopback(socket), 0);
	ASSERT_TRUE(motor.Start());

	auto stream	   = std::make_shared<SocketPullStream>();
	stream->socket = socket;
	ASSERT_TRUE(motor->AddStream(stream));

	std::weak_ptr<ov::Socket> socket_watcher = socket;
	EXPECT_TRUE(socket->CloseImmediately());
	stream->socket.reset();
	socket.reset();

	// The motor's registration is the last reference
	EXPECT_FALSE(socket_watcher.expired());

	EXPECT_TRUE(motor->DelStream(stream));
	EXPECT_EQ(motor->GetStreamCount(), 0u);
	EXPECT_TRUE(socket_watcher.expired());
}

// `Stop()` stops every stream and drops the motor's references to the streams and their sockets
TEST(StreamMotorTest, StopReleasesStreamsAndSockets)
{
	auto pool = ov::SocketPool::Create("test-motor-stop", ov::SocketType::Udp, false);
	ASSERT_NE(pool, nullptr);
	ASSERT_TRUE(pool->Initialize(1));

	auto socket = pool->AllocSocket(ov::SocketFamily::Inet);
	RunningMotor motor;

	ScopeExit cleanup([&]() {
		motor.Stop();

		if (socket != nullptr)
		{
			socket->CloseImmediately();
		}

		pool->Uninitialize();
	});

	ASSERT_NE(socket, nullptr);
	ASSERT_NE(BindToLoopback(socket), 0);
	ASSERT_TRUE(motor.Start());

	auto stream	   = std::make_shared<SocketPullStream>();
	stream->socket = socket;
	ASSERT_TRUE(motor->AddStream(stream));

	std::weak_ptr<ov::Socket> socket_watcher	  = socket;
	std::weak_ptr<pvd::PullStream> stream_watcher = stream;
	EXPECT_TRUE(socket->CloseImmediately());
	stream->socket.reset();
	socket.reset();
	stream.reset();

	EXPECT_TRUE(motor.Stop());
	EXPECT_EQ(motor->GetStreamCount(), 0u);
	EXPECT_TRUE(stream_watcher.expired());
	EXPECT_TRUE(socket_watcher.expired());
}
