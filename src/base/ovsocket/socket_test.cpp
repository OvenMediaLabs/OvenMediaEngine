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
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <srt/srt.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// macOS lacks `MSG_NOSIGNAL`; fall back to no flag so the peer helpers stay portable.
#ifndef MSG_NOSIGNAL
#	define MSG_NOSIGNAL 0
#endif

namespace
{
	constexpr int LOOPBACK_TIMEOUT_MSEC = 3000;

	ov::SocketAddress LoopbackAddress(uint16_t port)
	{
		return ov::SocketAddress::CreateAndGetFirst("127.0.0.1", port);
	}

	// Reads the local port bound to a socket directly from its `fd`. Unlike
	// `Socket::GetLocalAddress()` (only populated by `Bind()`), this also works for
	// a UDP socket whose local port was assigned implicitly by `Connect()`.
	uint16_t LocalPortOf(const std::shared_ptr<ov::Socket> &socket)
	{
		sockaddr_in sa{};
		socklen_t len = sizeof(sa);
		if (::getsockname(socket->GetSocket().GetNativeHandle(), reinterpret_cast<sockaddr *>(&sa), &len) != 0)
		{
			return 0;
		}
		return ntohs(sa.sin_port);
	}

	// Reads exactly `want` bytes into `out`. A TCP stream may legally arrive across
	// several reads (even on loopback), so this loops; returns `false` on
	// error/disconnect/timeout.
	bool RecvExactly(const std::shared_ptr<ov::Socket> &socket, void *out, size_t want)
	{
		auto *cursor	= static_cast<uint8_t *>(out);
		size_t received = 0;
		while (received < want)
		{
			auto result = socket->Recv(cursor + received, want - received);
			if (result.has_value() == false)
			{
				return false;
			}
			received += result.value();
		}
		return true;
	}

	// A minimal POSIX TCP server that accepts a single connection. It lets the
	// test act as the remote peer of the `ov::Socket` under test.
	class PosixTcpPeer
	{
	public:
		~PosixTcpPeer()
		{
			CloseConnection();
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

			int yes = 1;
			::setsockopt(_listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

			// Bound `accept()` so a failed client connect cannot hang `WaitAccepted()`.
			timeval tv = {LOOPBACK_TIMEOUT_MSEC / 1000, (LOOPBACK_TIMEOUT_MSEC % 1000) * 1000};
			::setsockopt(_listen_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

			sockaddr_in sa{};
			sa.sin_family	   = AF_INET;
			sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			sa.sin_port		   = 0;	 // ephemeral

			if (::bind(_listen_fd, reinterpret_cast<sockaddr *>(&sa), sizeof(sa)) != 0)
			{
				return false;
			}
			if (::listen(_listen_fd, 1) != 0)
			{
				return false;
			}

			socklen_t len = sizeof(sa);
			if (::getsockname(_listen_fd, reinterpret_cast<sockaddr *>(&sa), &len) != 0)
			{
				return false;
			}
			_port = ntohs(sa.sin_port);
			return true;
		}

		// Accepts in a background thread so the `ov::Socket` blocking `Connect()` and
		// this `accept()` can proceed concurrently. `accept()` is bounded by the
		// listener's `SO_RCVTIMEO`, so a failed client connect cannot hang `WaitAccepted()`.
		void AcceptAsync()
		{
			_accept_thread = std::thread([this]() {
				_conn_fd = ::accept(_listen_fd, nullptr, nullptr);
			});
		}

		void WaitAccepted()
		{
			if (_accept_thread.joinable())
			{
				_accept_thread.join();
			}
		}

		uint16_t Port() const
		{
			return _port;
		}
		bool IsConnected() const
		{
			return _conn_fd >= 0;
		}

		// Sends the whole buffer; a TCP stream may accept a short write, so loop until
		// everything is queued. Returns total bytes sent, or the failing return value.
		ssize_t Send(const void *data, size_t length)
		{
			auto *cursor = static_cast<const uint8_t *>(data);
			size_t sent	 = 0;
			while (sent < length)
			{
				ssize_t written = ::send(_conn_fd, cursor + sent, length - sent, MSG_NOSIGNAL);
				if (written <= 0)
				{
					return written;
				}
				sent += static_cast<size_t>(written);
			}
			return static_cast<ssize_t>(sent);
		}

		void CloseConnection()
		{
			if (_accept_thread.joinable())
			{
				_accept_thread.join();
			}
			if (_conn_fd >= 0)
			{
				::close(_conn_fd);
				_conn_fd = -1;
			}
		}

	private:
		int _listen_fd = -1;
		std::atomic<int> _conn_fd{-1};
		uint16_t _port = 0;
		std::thread _accept_thread;
	};

	// A plain POSIX UDP socket that the test uses to send datagrams to the
	// `ov::Socket` under test.
	class PosixUdpPeer
	{
	public:
		~PosixUdpPeer()
		{
			if (_fd >= 0)
			{
				::close(_fd);
			}
		}

		bool Open()
		{
			_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
			if (_fd < 0)
			{
				return false;
			}

			sockaddr_in sa{};
			sa.sin_family	   = AF_INET;
			sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			sa.sin_port		   = 0;	 // ephemeral

			if (::bind(_fd, reinterpret_cast<sockaddr *>(&sa), sizeof(sa)) != 0)
			{
				return false;
			}

			socklen_t len = sizeof(sa);
			if (::getsockname(_fd, reinterpret_cast<sockaddr *>(&sa), &len) != 0)
			{
				return false;
			}
			_port = ntohs(sa.sin_port);
			return true;
		}

		uint16_t Port() const
		{
			return _port;
		}

		ssize_t SendTo(uint16_t dst_port, const void *data, size_t length)
		{
			sockaddr_in da{};
			da.sin_family	   = AF_INET;
			da.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			da.sin_port		   = htons(dst_port);
			return ::sendto(_fd, data, length, 0, reinterpret_cast<sockaddr *>(&da), sizeof(da));
		}

	private:
		int _fd		   = -1;
		uint16_t _port = 0;
	};

	// A libsrt peer (listener) that the OME SRT socket under test connects to, so
	// the test can drive SRT message delivery and peer shutdown directly.
	class SrtPeer
	{
	public:
		~SrtPeer()
		{
			CloseConnection();
			if (_listener != SRT_INVALID_SOCK)
			{
				::srt_close(_listener);
			}
		}

		bool Listen()
		{
			_listener = ::srt_create_socket();
			if (_listener == SRT_INVALID_SOCK)
			{
				return false;
			}
			Configure(_listener);

			sockaddr_in sa{};
			sa.sin_family	   = AF_INET;
			sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			sa.sin_port		   = 0;	 // ephemeral

			if (::srt_bind(_listener, reinterpret_cast<sockaddr *>(&sa), sizeof(sa)) == SRT_ERROR)
			{
				return false;
			}
			if (::srt_listen(_listener, 1) == SRT_ERROR)
			{
				return false;
			}

			int len = sizeof(sa);
			if (::srt_getsockname(_listener, reinterpret_cast<sockaddr *>(&sa), &len) == SRT_ERROR)
			{
				return false;
			}
			_port = ntohs(sa.sin_port);
			return true;
		}

		// Accepts in a background thread so the OME blocking `Connect()` and this
		// accept can proceed concurrently. The listener is non-blocking and the
		// accept is polled up to a timeout, so a failed client connect cannot hang
		// `WaitAccepted()`.
		void AcceptAsync()
		{
			_accept_thread = std::thread([this]() {
				for (int waited = 0; waited < LOOPBACK_TIMEOUT_MSEC; waited += 5)
				{
					SRTSOCKET accepted = ::srt_accept(_listener, nullptr, nullptr);
					if (accepted != SRT_INVALID_SOCK)
					{
						_accepted = accepted;
						return;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
				}
			});
		}

		void WaitAccepted()
		{
			if (_accept_thread.joinable())
			{
				_accept_thread.join();
			}
		}

		uint16_t Port() const
		{
			return _port;
		}
		bool IsConnected() const
		{
			return _accepted != SRT_INVALID_SOCK;
		}

		int Send(const void *data, size_t length)
		{
			return ::srt_sendmsg2(_accepted, reinterpret_cast<const char *>(data), static_cast<int>(length), nullptr);
		}

		void CloseConnection()
		{
			if (_accept_thread.joinable())
			{
				_accept_thread.join();
			}
			if (_accepted != SRT_INVALID_SOCK)
			{
				::srt_close(_accepted);
				_accepted = SRT_INVALID_SOCK;
			}
		}

	private:
		// Match OME's SRT configuration (live transtype + message API) so the
		// handshake succeeds. The listener uses non-blocking accept (so a failed
		// connect cannot hang the accept thread) and blocking send.
		static void Configure(SRTSOCKET s)
		{
			int live = SRTT_LIVE;
			::srt_setsockopt(s, 0, SRTO_TRANSTYPE, &live, sizeof(live));
			int yes = 1;
			::srt_setsockopt(s, 0, SRTO_MESSAGEAPI, &yes, sizeof(yes));
			int async = 0;
			::srt_setsockopt(s, 0, SRTO_RCVSYN, &async, sizeof(async));
			int sync = 1;
			::srt_setsockopt(s, 0, SRTO_SNDSYN, &sync, sizeof(sync));
		}

		SRTSOCKET _listener = SRT_INVALID_SOCK;
		std::atomic<SRTSOCKET> _accepted{SRT_INVALID_SOCK};
		uint16_t _port = 0;
		std::thread _accept_thread;
	};

	// Aborts the process if not disarmed within the deadline. A deadlock in the
	// code under test would otherwise hang the whole binary forever; this turns
	// it into a visible crash with a message instead. The deadline is generous,
	// so a healthy run (which finishes in well under a second) never trips it.
	class Watchdog
	{
	public:
		Watchdog(std::chrono::milliseconds deadline, std::string label)
			: _label(std::move(label))
		{
			_thread = std::thread([this, deadline]() {
				std::unique_lock<std::mutex> lock(_mutex);
				if (_cv.wait_for(lock, deadline, [this]() { return _done; }) == false)
				{
					::fprintf(stderr, "[Watchdog] '%s' exceeded its deadline - aborting (possible deadlock)\n", _label.c_str());
					::fflush(stderr);
					::abort();
				}
			});
		}

		~Watchdog()
		{
			{
				std::lock_guard<std::mutex> lock(_mutex);
				_done = true;
			}
			_cv.notify_all();
			if (_thread.joinable())
			{
				_thread.join();
			}
		}

	private:
		std::mutex _mutex;
		std::condition_variable _cv;
		bool _done = false;
		std::string _label;
		std::thread _thread;
	};
}  // namespace

// ---------------------------------------------------------------------------
// Base fixture: owns a one-worker socket pool and closes the socket under test
// on teardown (so an early `ASSERT` failure does not trip the `~Socket` "not
// closed" assertion).
// ---------------------------------------------------------------------------
class SocketTestBase : public ::testing::Test
{
protected:
	void InitPool(const char *name, ov::SocketType type)
	{
		_pool = ov::SocketPool::Create(name, type, false);
		ASSERT_NE(_pool, nullptr);
		ASSERT_TRUE(_pool->Initialize(1));
	}

	void TearDown() override
	{
		if (_client != nullptr && _client->GetState() == ov::SocketState::Connected)
		{
			_client->Close();
		}
		if (_pool != nullptr)
		{
			_pool->Uninitialize();
		}
	}

	std::shared_ptr<ov::SocketPool> _pool;
	std::shared_ptr<ov::Socket> _client;
};

class SocketRecvTcpTest : public SocketTestBase
{
protected:
	void SetUp() override
	{
		InitPool("test-recv-tcp", ov::SocketType::Tcp);
	}

	// Returns a blocking `ov::Socket` TCP client connected to `peer`.
	std::shared_ptr<ov::Socket> ConnectClient(PosixTcpPeer &peer)
	{
		auto client = _pool->AllocSocket(ov::SocketFamily::Inet);
		if (client == nullptr)
		{
			return nullptr;
		}

		client->MakeBlocking();
		timeval tv = {LOOPBACK_TIMEOUT_MSEC / 1000, (LOOPBACK_TIMEOUT_MSEC % 1000) * 1000};
		client->SetRecvTimeout(tv);

		peer.AcceptAsync();
		auto error = client->Connect(LoopbackAddress(peer.Port()), LOOPBACK_TIMEOUT_MSEC);
		peer.WaitAccepted();

		if (error != nullptr || peer.IsConnected() == false)
		{
			return nullptr;
		}

		_client = client;
		return client;
	}
};

class SocketRecvUdpTest : public SocketTestBase
{
protected:
	void SetUp() override
	{
		InitPool("test-recv-udp", ov::SocketType::Udp);
	}

	// Returns a blocking `ov::Socket` UDP client connected to the loopback port.
	std::shared_ptr<ov::Socket> ConnectClient(uint16_t peer_port)
	{
		auto client = _pool->AllocSocket(ov::SocketFamily::Inet);
		if (client == nullptr)
		{
			return nullptr;
		}

		client->MakeBlocking();
		timeval tv = {LOOPBACK_TIMEOUT_MSEC / 1000, (LOOPBACK_TIMEOUT_MSEC % 1000) * 1000};
		client->SetRecvTimeout(tv);

		if (client->Connect(LoopbackAddress(peer_port), LOOPBACK_TIMEOUT_MSEC) != nullptr)
		{
			return nullptr;
		}

		_client = client;
		return client;
	}
};

// ===========================================================================
// TCP
// ===========================================================================

// A successful read returns the data intact. TCP is a stream, so the payload may
// span several reads; accumulate via `RecvExactly` rather than assuming one call.
TEST_F(SocketRecvTcpTest, RawBufferReceivesData)
{
	PosixTcpPeer peer;
	ASSERT_TRUE(peer.Listen());
	auto client = ConnectClient(peer);
	ASSERT_NE(client, nullptr);

	const char payload[] = "OvenMediaEngine";
	ASSERT_EQ(peer.Send(payload, sizeof(payload)), static_cast<ssize_t>(sizeof(payload)));

	char buffer[64] = {0};
	ASSERT_TRUE(RecvExactly(client, buffer, sizeof(payload)));
	EXPECT_EQ(::memcmp(buffer, payload, sizeof(payload)), 0);
	EXPECT_EQ(client->GetState(), ov::SocketState::Connected);
}

// A single read reports the bytes actually available, never the buffer capacity.
TEST_F(SocketRecvTcpTest, RawBufferReportsActualLengthNotCapacity)
{
	PosixTcpPeer peer;
	ASSERT_TRUE(peer.Listen());
	auto client = ConnectClient(peer);
	ASSERT_NE(client, nullptr);

	const char payload[] = {'a', 'b', 'c'};
	ASSERT_EQ(peer.Send(payload, sizeof(payload)), 3);

	char buffer[4096];
	auto result = client->Recv(buffer, sizeof(buffer));

	ASSERT_TRUE(result.has_value());
	// May be a short read (1..3) on a stream, but never the 4096 buffer capacity.
	EXPECT_GT(result.value(), 0u);
	EXPECT_LE(result.value(), 3u);
}

// The `Data` overload sets the data length to the number of bytes received. TCP
// may split the payload across reads, so accumulate until the whole thing arrives.
TEST_F(SocketRecvTcpTest, DataOverloadSetsLength)
{
	PosixTcpPeer peer;
	ASSERT_TRUE(peer.Listen());
	auto client = ConnectClient(peer);
	ASSERT_NE(client, nullptr);

	const char payload[] = "hello-data";
	ASSERT_EQ(peer.Send(payload, sizeof(payload)), static_cast<ssize_t>(sizeof(payload)));

	std::string received;
	while (received.size() < sizeof(payload))
	{
		auto data = std::make_shared<ov::Data>();
		ASSERT_TRUE(data->Reserve(2048));

		auto error = client->Recv(data);
		ASSERT_EQ(error, nullptr);
		received.append(static_cast<const char *>(data->GetData()), data->GetLength());
	}

	EXPECT_EQ(received.size(), sizeof(payload));
	EXPECT_EQ(::memcmp(received.data(), payload, sizeof(payload)), 0);
}

// An orderly peer shutdown (`recv() == 0`) must be classified as a disconnect,
// NOT as a retriable `0`-byte read. This is the core of the EOF handling change.
TEST_F(SocketRecvTcpTest, PeerShutdownReportsDisconnect)
{
	PosixTcpPeer peer;
	ASSERT_TRUE(peer.Listen());
	auto client = ConnectClient(peer);
	ASSERT_NE(client, nullptr);

	peer.CloseConnection();

	char buffer[64];
	auto result = client->Recv(buffer, sizeof(buffer));

	ASSERT_FALSE(result.has_value());
	ASSERT_NE(result.error(), nullptr);
	EXPECT_EQ(client->GetState(), ov::SocketState::Disconnected);
}

// The `Data` overload surfaces the same disconnect as a non-null error.
TEST_F(SocketRecvTcpTest, DataOverloadReportsDisconnectOnPeerShutdown)
{
	PosixTcpPeer peer;
	ASSERT_TRUE(peer.Listen());
	auto client = ConnectClient(peer);
	ASSERT_NE(client, nullptr);

	peer.CloseConnection();

	auto data = std::make_shared<ov::Data>();
	ASSERT_TRUE(data->Reserve(64));

	auto error = client->Recv(data);

	ASSERT_NE(error, nullptr);
	EXPECT_EQ(client->GetState(), ov::SocketState::Disconnected);
}

// `Recv()` on a socket we closed ourselves fails instead of touching the `fd`.
TEST_F(SocketRecvTcpTest, RecvOnClosedSocketFails)
{
	PosixTcpPeer peer;
	ASSERT_TRUE(peer.Listen());
	auto client = ConnectClient(peer);
	ASSERT_NE(client, nullptr);

	client->Close();

	char buffer[64];
	auto result = client->Recv(buffer, sizeof(buffer));

	ASSERT_FALSE(result.has_value());
	ASSERT_NE(result.error(), nullptr);
}

// A non-blocking read (`non_block=true`) on a blocking socket with no data pending
// means "no data right now" -> a successful `0`-byte read (retry later), NOT an
// error and NOT a disconnect. This is the contract OVT/RTSPC rely on when they
// call `ReceivePacket(true)` on a blocking socket, and it matches the SRT path.
TEST_F(SocketRecvTcpTest, NonBlockParamOnBlockingSocketWithoutDataReportsRetry)
{
	PosixTcpPeer peer;
	ASSERT_TRUE(peer.Listen());
	auto client = ConnectClient(peer);
	ASSERT_NE(client, nullptr);

	char buffer[64];
	auto result = client->Recv(buffer, sizeof(buffer), /*non_block=*/true);

	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result.value(), 0u);
	EXPECT_EQ(client->GetState(), ov::SocketState::Connected);
}

// ===========================================================================
// UDP
// ===========================================================================

// A normal datagram is delivered with its exact length and payload.
TEST_F(SocketRecvUdpTest, ReceivesDatagram)
{
	PosixUdpPeer peer;
	ASSERT_TRUE(peer.Open());

	auto client = ConnectClient(peer.Port());
	ASSERT_NE(client, nullptr);

	uint16_t local_port = LocalPortOf(client);
	ASSERT_NE(local_port, 0u);

	const char payload[] = "udp-payload";
	ASSERT_EQ(peer.SendTo(local_port, payload, sizeof(payload)), static_cast<ssize_t>(sizeof(payload)));

	char buffer[64] = {0};
	auto result		= client->Recv(buffer, sizeof(buffer));

	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result.value(), sizeof(payload));
	EXPECT_STREQ(buffer, payload);
}

// A `0`-length UDP datagram is a valid read (datagram sockets have no EOF), so it
// must be reported as success with `0` bytes and must NOT close the socket.
TEST_F(SocketRecvUdpTest, ZeroLengthDatagramIsSuccessNotDisconnect)
{
	PosixUdpPeer peer;
	ASSERT_TRUE(peer.Open());

	auto client = ConnectClient(peer.Port());
	ASSERT_NE(client, nullptr);

	uint16_t local_port = LocalPortOf(client);
	ASSERT_NE(local_port, 0u);

	// Send an empty datagram.
	ASSERT_EQ(peer.SendTo(local_port, "", 0), 0);

	char buffer[64];
	auto result = client->Recv(buffer, sizeof(buffer));

	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result.value(), 0u);
	EXPECT_NE(client->GetState(), ov::SocketState::Disconnected);
	EXPECT_NE(client->GetState(), ov::SocketState::Error);
}

// After an empty datagram the socket stays usable: a subsequent normal datagram
// is still received. Proves the `0`-length read did not tear the socket down.
TEST_F(SocketRecvUdpTest, StaysUsableAfterZeroLengthDatagram)
{
	PosixUdpPeer peer;
	ASSERT_TRUE(peer.Open());

	auto client = ConnectClient(peer.Port());
	ASSERT_NE(client, nullptr);

	uint16_t local_port = LocalPortOf(client);
	ASSERT_NE(local_port, 0u);

	char buffer[64] = {0};

	ASSERT_EQ(peer.SendTo(local_port, "", 0), 0);
	auto empty_result = client->Recv(buffer, sizeof(buffer));
	ASSERT_TRUE(empty_result.has_value());
	EXPECT_EQ(empty_result.value(), 0u);

	const char payload[] = "after-empty";
	ASSERT_EQ(peer.SendTo(local_port, payload, sizeof(payload)), static_cast<ssize_t>(sizeof(payload)));
	auto data_result = client->Recv(buffer, sizeof(buffer));
	ASSERT_TRUE(data_result.has_value());
	EXPECT_EQ(data_result.value(), sizeof(payload));
	EXPECT_STREQ(buffer, payload);
}

// ===========================================================================
// SRT
//
// Pins the `SRT_EASYNCRCV` -> retry (`0`) contract. `srt_recvmsg2()` ignores a
// per-call `non_block` flag, so async recv (`SRTO_RCVSYN=false`) is what surfaces
// `SRT_EASYNCRCV`; the client connects in blocking mode first (deterministic
// handshake) and is then switched to async.
// ===========================================================================
class SocketRecvSrtTest : public SocketTestBase
{
protected:
	void SetUp() override
	{
		ASSERT_NE(::srt_startup(), -1);
		InitPool("test-recv-srt", ov::SocketType::Srt);
	}

	void TearDown() override
	{
		SocketTestBase::TearDown();
		::srt_cleanup();
	}

	// Returns an OME SRT client connected (blocking handshake) to `peer`, then
	// switched to async recv so a no-data read yields `SRT_EASYNCRCV`.
	std::shared_ptr<ov::Socket> ConnectClient(SrtPeer &peer)
	{
		auto client = _pool->AllocSocket(ov::SocketFamily::Inet);
		if (client == nullptr)
		{
			return nullptr;
		}

		client->MakeBlocking();

		peer.AcceptAsync();
		auto error = client->Connect(LoopbackAddress(peer.Port()), LOOPBACK_TIMEOUT_MSEC);
		peer.WaitAccepted();

		if (error != nullptr || peer.IsConnected() == false)
		{
			return nullptr;
		}

		// Switch to async recv: no data -> `SRT_EASYNCRCV`, broken link -> `SRT_ECONNLOST`.
		client->SetSockOpt<bool>(SRTO_RCVSYN, false);

		_client = client;
		return client;
	}
};

// On an async SRT socket with no data, `SRT_EASYNCRCV` is a retry-later success
// (`0` bytes), not an error and not a disconnect.
TEST_F(SocketRecvSrtTest, NoDataReportsRetry)
{
	SrtPeer peer;
	ASSERT_TRUE(peer.Listen());
	auto client = ConnectClient(peer);
	ASSERT_NE(client, nullptr);

	char buffer[1500];
	auto result = client->Recv(buffer, sizeof(buffer));

	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result.value(), 0u);
	EXPECT_EQ(client->GetState(), ov::SocketState::Connected);
}

// ===========================================================================
// Concurrency / stress
//
// These exercise `Recv()` under heavy multithreading to surface data races,
// deadlocks, and use-after-free. Run them under ThreadSanitizer for race
// detection (configure with `OME_SANITIZE_THREAD=ON`); even without TSan they
// catch crashes, deadlocks (via `Watchdog`), and incorrect classification.
// ===========================================================================
class SocketConcurrencyTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		_pool = ov::SocketPool::Create("test-recv-conc", ov::SocketType::Tcp, false);
		ASSERT_NE(_pool, nullptr);
		ASSERT_TRUE(_pool->Initialize(4));
	}

	void TearDown() override
	{
		if (_pool != nullptr)
		{
			_pool->Uninitialize();
		}
	}

	// A blocking `ov::Socket` TCP client connected to `peer`, with a short recv
	// timeout so a stuck read fails fast instead of hanging the stress loop.
	std::shared_ptr<ov::Socket> Connect(PosixTcpPeer &peer)
	{
		auto client = _pool->AllocSocket(ov::SocketFamily::Inet);
		if (client == nullptr)
		{
			return nullptr;
		}

		client->MakeBlocking();
		timeval tv = {0, 200 * 1000};  // 200 ms
		client->SetRecvTimeout(tv);

		peer.AcceptAsync();
		auto error = client->Connect(LoopbackAddress(peer.Port()), LOOPBACK_TIMEOUT_MSEC);
		peer.WaitAccepted();

		if (error != nullptr || peer.IsConnected() == false)
		{
			return nullptr;
		}
		return client;
	}

	std::shared_ptr<ov::SocketPool> _pool;
};

// Many independent sockets each receive on their own thread at the same time.
// Stresses the pool/worker machinery and per-socket `Recv`; every read must
// deliver its exact payload with no crash.
TEST_F(SocketConcurrencyTest, ManyIndependentSocketsReceiveConcurrently)
{
	Watchdog watchdog(std::chrono::seconds(30), "ManyIndependentSocketsReceiveConcurrently");

	constexpr int SOCKET_COUNT = 48;
	const char payload[]	   = "concurrent-payload";

	std::vector<std::unique_ptr<PosixTcpPeer>> peers;
	std::vector<std::shared_ptr<ov::Socket>> clients;
	for (int i = 0; i < SOCKET_COUNT; i++)
	{
		auto peer = std::make_unique<PosixTcpPeer>();
		ASSERT_TRUE(peer->Listen());
		auto client = Connect(*peer);
		ASSERT_NE(client, nullptr);
		peers.push_back(std::move(peer));
		clients.push_back(client);
	}

	std::atomic<int> success{0};
	std::vector<std::thread> threads;
	threads.reserve(SOCKET_COUNT);
	for (int i = 0; i < SOCKET_COUNT; i++)
	{
		threads.emplace_back([&, i]() {
			peers[i]->Send(payload, sizeof(payload));

			char buffer[64] = {0};
			if (RecvExactly(clients[i], buffer, sizeof(payload)) &&
				::memcmp(buffer, payload, sizeof(payload)) == 0)
			{
				success.fetch_add(1);
			}
		});
	}
	for (auto &thread : threads)
	{
		thread.join();
	}

	EXPECT_EQ(success.load(), SOCKET_COUNT);

	for (auto &client : clients)
	{
		client->Close();
	}
}

// Each socket's peer disconnects concurrently with an in-flight `Recv()`. Every
// `Recv()` must report failure (EOF disconnect or, if it wins the race, a timeout)
// and never a spurious success; no crash under the storm of simultaneous closes.
TEST_F(SocketConcurrencyTest, RecvRacesPeerDisconnectStorm)
{
	Watchdog watchdog(std::chrono::seconds(30), "RecvRacesPeerDisconnectStorm");

	constexpr int SOCKET_COUNT = 48;

	std::vector<std::unique_ptr<PosixTcpPeer>> peers;
	std::vector<std::shared_ptr<ov::Socket>> clients;
	for (int i = 0; i < SOCKET_COUNT; i++)
	{
		auto peer = std::make_unique<PosixTcpPeer>();
		ASSERT_TRUE(peer->Listen());
		auto client = Connect(*peer);
		ASSERT_NE(client, nullptr);
		peers.push_back(std::move(peer));
		clients.push_back(client);
	}

	std::atomic<int> failed_as_expected{0};
	std::vector<std::thread> threads;
	threads.reserve(SOCKET_COUNT);
	for (int i = 0; i < SOCKET_COUNT; i++)
	{
		threads.emplace_back([&, i]() {
			std::thread peer_closer([&, i]() { peers[i]->CloseConnection(); });

			char buffer[64];
			auto result = clients[i]->Recv(buffer, sizeof(buffer));

			peer_closer.join();

			if (result.has_value() == false)
			{
				failed_as_expected.fetch_add(1);
			}
		});
	}
	for (auto &thread : threads)
	{
		thread.join();
	}

	EXPECT_EQ(failed_as_expected.load(), SOCKET_COUNT);

	for (auto &client : clients)
	{
		if (client->GetState() == ov::SocketState::Connected)
		{
			client->Close();
		}
	}
}
