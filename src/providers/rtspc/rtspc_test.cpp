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
#include <base/provider/pull_provider/stream_props.h>
#include <gtest/gtest.h>
#include <sys/socket.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include "rtspc_stream.h"

namespace
{
	class TestApplicationInfo : public info::Application
	{
	public:
		using info::Application::GetInvalidApplication;
	};

	// Stands in for the `RtpRtcp` node, so interleaved data reaches a registered previous node
	class DiscardingNode : public ov::Node
	{
	public:
		DiscardingNode()
			: ov::Node(NodeType::Rtp)
		{
		}

		bool OnDataReceivedFromPrevNode(NodeType from_node, const std::shared_ptr<ov::Data> &data) override
		{
			return true;
		}

		bool OnDataReceivedFromNextNode(NodeType from_node, const std::shared_ptr<const ov::Data> &data) override
		{
			return true;
		}
	};
}  // namespace

namespace pvd
{
	// Drives `PullStream::Stop()` -> `RtspcStream::StopStream()` against a loopback TCP peer playing the RTSP server
	class RtspcStreamTest : public ome_test::PoolTest
	{
	protected:
		static constexpr int CONNECT_TIMEOUT_MSEC	   = 3000;
		static constexpr int STOP_LIMIT_MSEC		   = 1000;
		static constexpr int RECV_TIMEOUT_MSEC		   = 1000;
		static constexpr int SEND_TIMEOUT_MSEC		   = 1000;
		static constexpr int PROCESS_COUNT_BEFORE_STOP = 100;
		static constexpr int MOTOR_WAIT_USEC		   = 100;
		static constexpr int FRAME_PAYLOAD_SIZE		   = 1400;
		static constexpr int FRAME_INTERVAL_USEC	   = 100;

	protected:
		void SetUp() override
		{
			auto pool = CreatePool("test-rtspc-stop", ov::SocketType::Tcp);
			ASSERT_NE(pool, nullptr);

			uint16_t port = 0;
			_listener	  = ome_test::ListenOnLoopback(&port);
			ASSERT_GE(_listener.Get(), 0);

			_socket = pool->AllocSocket(ov::SocketFamily::Inet);
			ASSERT_NE(_socket, nullptr);
			ASSERT_TRUE(_socket->MakeBlocking());
			ASSERT_EQ(_socket->Connect(ov::SocketAddress::CreateAndGetFirst("127.0.0.1", port), CONNECT_TIMEOUT_MSEC), nullptr);

			_server = ome_test::UniqueFd(::accept(_listener.Get(), nullptr, nullptr));
			ASSERT_GE(_server.Get(), 0);

			timeval timeout{RECV_TIMEOUT_MSEC / 1000, (RECV_TIMEOUT_MSEC % 1000) * 1000};
			ASSERT_EQ(::setsockopt(_server.Get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)), 0);

			_stream = std::make_shared<RtspcStream>(
				std::shared_ptr<PullApplication>(), info::Stream(TestApplicationInfo::GetInvalidApplication(), StreamSourceType::RtspPull),
				std::vector<ov::String>{}, std::make_shared<PullStreamProperties>());

			auto url		   = ov::String::FormatString("rtsp://127.0.0.1:%u/app/stream", port);
			_stream->_curr_url = ov::Url::Parse(url);
			ASSERT_NE(_stream->_curr_url, nullptr);

			std::atomic_store(&_stream->_signalling_socket, _socket);
		}

		void TearDown() override
		{
			_stream.reset();
			ome_test::PoolTest::TearDown();
		}

		// The state a handshake leaves the stream in when it reaches `PLAYING`
		void StartPlaying()
		{
			{
				ov::LockGuard lock(_stream->_connection_lock);
				_stream->_connection_ready = true;
			}

			_stream->SetState(Stream::State::PLAYING);
		}

		// Keeps `ProcessMediaPacket()` from sending GET_PARAMETER, which the server side never reads
		void PostponePing()
		{
			_stream->_rtsp_session_timeout_sec = DEFAULT_RTSP_SESSION_TIMEOUT_SEC;
			_stream->_ping_timer.Start();
		}

		// Sends interleaved RTP-sized frames from the server side, paced like a stream,
		// until the client stops reading or `sending` turns false
		void SendInterleavedFrames(const std::atomic<bool> &sending)
		{
			// `$`, channel 0, the 2-byte payload length, and the payload
			std::string frame(4 + FRAME_PAYLOAD_SIZE, '\0');
			frame[0] = '$';
			frame[2] = static_cast<char>(FRAME_PAYLOAD_SIZE >> 8);
			frame[3] = static_cast<char>(FRAME_PAYLOAD_SIZE & 0xFF);

			timeval timeout{SEND_TIMEOUT_MSEC / 1000, (SEND_TIMEOUT_MSEC % 1000) * 1000};
			EXPECT_EQ(::setsockopt(_server.Get(), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)), 0);

			while (sending.load() &&
				   (::send(_server.Get(), frame.data(), frame.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(frame.size())))
			{
				std::this_thread::sleep_for(std::chrono::microseconds(FRAME_INTERVAL_USEC));
			}
		}

		// Everything the server side has received until the client closes the connection or the timeout passes
		std::string ReceiveAll()
		{
			std::string received;
			char buffer[4096];

			while (true)
			{
				auto read_bytes = ::recv(_server.Get(), buffer, sizeof(buffer), 0);

				if (read_bytes <= 0)
				{
					return received;
				}

				received.append(buffer, read_bytes);
			}
		}

	protected:
		ome_test::UniqueFd _listener;
		ome_test::UniqueFd _server;
		std::shared_ptr<ov::Socket> _socket;
		std::shared_ptr<RtspcStream> _stream;
	};

	// The origin closes the connection gracefully while the stream is playing:
	// the motor then stops the stream on its own thread, where nobody is left to read a `TEARDOWN` response,
	// so the stop must not wait for one
	TEST_F(RtspcStreamTest, StopOfPlayingStreamDoesNotWaitForTeardownResponse)
	{
		StartPlaying();
		ASSERT_EQ(::shutdown(_server.Get(), SHUT_WR), 0);

		auto begin = std::chrono::steady_clock::now();
		EXPECT_TRUE(_stream->PullStream::Stop());
		auto elapsed_msec = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count();

		EXPECT_LT(elapsed_msec, STOP_LIMIT_MSEC);
		EXPECT_EQ(_stream->GetState(), Stream::State::STOPPED);
		EXPECT_EQ(ReceiveAll().rfind("TEARDOWN ", 0), 0U);
	}

	// A stream deleted through the API is `TERMINATED` before it is stopped, and the stop must leave it so:
	// the collector reconnects a stream in `ERROR` or `STOPPED`
	TEST_F(RtspcStreamTest, StopKeepsTerminatedState)
	{
		StartPlaying();
		ASSERT_TRUE(_stream->Terminate());

		EXPECT_TRUE(_stream->PullStream::Stop());

		EXPECT_EQ(_stream->GetState(), Stream::State::TERMINATED);
	}

	// The collector stops a stream whose media the motor may still be processing
	// (an unused stream is deleted, or no input arrives):
	// the stop must not release the connection or the node links during `ProcessMediaPacket()`.
	// A data race here shows up only under ThreadSanitizer.
	TEST_F(RtspcStreamTest, StopIsSerializedWithMediaProcessing)
	{
		StartPlaying();
		PostponePing();
		_stream->RegisterPrevNode(std::make_shared<DiscardingNode>());

		std::atomic<bool> sending{true};
		std::thread server([&] { SendInterleavedFrames(sending); });

		std::atomic<bool> stopped{false};
		std::atomic<int> process_count{0};
		std::thread motor([&] {
			while (stopped.load() == false)
			{
				_stream->ProcessMediaPacket();
				process_count++;

				// Stands in for the motor's `epoll_wait()` between two events
				std::this_thread::sleep_for(std::chrono::microseconds(MOTOR_WAIT_USEC));
			}
		});

		EXPECT_TRUE(ome_test::WaitUntil([&] { return process_count.load() >= PROCESS_COUNT_BEFORE_STOP; }));
		EXPECT_TRUE(_stream->PullStream::Stop());

		stopped = true;
		motor.join();
		sending = false;
		server.join();
	}

	// The motor may enter `ProcessMediaPacket()` after a stop, having checked `PLAYING` before it:
	// such a call must do nothing, which also keeps the motor from stopping the stream again
	TEST_F(RtspcStreamTest, ProcessMediaAfterStopDoesNothing)
	{
		StartPlaying();
		PostponePing();

		EXPECT_TRUE(_stream->PullStream::Stop());

		EXPECT_EQ(_stream->ProcessMediaPacket(), PullStream::ProcessMediaResult::PROCESS_MEDIA_TRY_AGAIN);
	}
}  // namespace pvd
