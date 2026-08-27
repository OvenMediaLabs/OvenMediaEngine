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
	// the test can drive SRT message delivery and peer shutdown directly. One
	// listener can accept several connections (`AcceptAsync(count)`); the
	// index-less accessors operate on the first accepted connection.
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

		bool Listen(int backlog = 1)
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
			if (::srt_listen(_listener, backlog) == SRT_ERROR)
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

		// Accepts `count` connections in a background thread so the OME blocking
		// `Connect()` and this accept can proceed concurrently. The listener is
		// non-blocking and each accept is polled up to a timeout, so a failed
		// client connect cannot hang `WaitAccepted()`.
		void AcceptAsync(int count = 1)
		{
			_accept_thread = std::thread([this, count]() {
				int waited = 0;
				while ((ConnectionCount() < count) && (waited < LOOPBACK_TIMEOUT_MSEC))
				{
					SRTSOCKET accepted = ::srt_accept(_listener, nullptr, nullptr);
					if (accepted != SRT_INVALID_SOCK)
					{
						std::lock_guard<std::mutex> lock(_accepted_mutex);
						_accepted_list.push_back(accepted);
						waited = 0;
						continue;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
					waited += 5;
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
		SRTSOCKET ListenerHandle() const
		{
			return _listener;
		}
		bool IsConnected() const
		{
			return ConnectionCount() > 0;
		}
		int ConnectionCount() const
		{
			std::lock_guard<std::mutex> lock(_accepted_mutex);
			return static_cast<int>(_accepted_list.size());
		}

		int Send(const void *data, size_t length)
		{
			return Send(0, data, length);
		}
		int Send(int index, const void *data, size_t length)
		{
			SRTSOCKET s = Accepted(index);
			if (s == SRT_INVALID_SOCK)
			{
				return SRT_ERROR;
			}
			return ::srt_sendmsg2(s, reinterpret_cast<const char *>(data), static_cast<int>(length), nullptr);
		}

		// Closes every accepted connection (after the accept thread has finished).
		void CloseConnection()
		{
			if (_accept_thread.joinable())
			{
				_accept_thread.join();
			}
			std::vector<SRTSOCKET> list;
			{
				std::lock_guard<std::mutex> lock(_accepted_mutex);
				list.swap(_accepted_list);
			}
			for (auto s : list)
			{
				if (s != SRT_INVALID_SOCK)
				{
					::srt_close(s);
				}
			}
		}

		// Closes one accepted connection; safe to call from several threads with
		// distinct indexes. The slot is invalidated first so a later
		// `CloseConnection()` does not close it twice.
		void CloseConnection(int index)
		{
			SRTSOCKET s = SRT_INVALID_SOCK;
			{
				std::lock_guard<std::mutex> lock(_accepted_mutex);
				if ((index >= 0) && (index < static_cast<int>(_accepted_list.size())))
				{
					s					  = _accepted_list[index];
					_accepted_list[index] = SRT_INVALID_SOCK;
				}
			}
			if (s != SRT_INVALID_SOCK)
			{
				::srt_close(s);
			}
		}

	private:
		SRTSOCKET Accepted(int index) const
		{
			std::lock_guard<std::mutex> lock(_accepted_mutex);
			if ((index >= 0) && (index < static_cast<int>(_accepted_list.size())))
			{
				return _accepted_list[index];
			}
			return SRT_INVALID_SOCK;
		}

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
		mutable std::mutex _accepted_mutex;
		std::vector<SRTSOCKET> _accepted_list;
		uint16_t _port = 0;
		std::thread _accept_thread;
	};

	// Relays UDP datagrams between one OME SRT client and an `SrtPeer` so the test can
	// cut the link without any SRT shutdown handshake (`StopForwarding()`), which is
	// the only way to drive libsrt into `SRTS_BROKEN` on loopback.
	class UdpRelay
	{
	public:
		~UdpRelay()
		{
			Stop();
		}

		bool Start(uint16_t peer_port)
		{
			_front = ::socket(AF_INET, SOCK_DGRAM, 0);
			_back  = ::socket(AF_INET, SOCK_DGRAM, 0);
			if ((_front < 0) || (_back < 0))
			{
				return false;
			}

			// Short receive timeouts so the relay threads notice `Stop()` promptly.
			timeval tv = {0, 50 * 1000};
			::setsockopt(_front, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
			::setsockopt(_back, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

			sockaddr_in sa{};
			sa.sin_family	   = AF_INET;
			sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			sa.sin_port		   = 0;	 // ephemeral
			if (::bind(_front, reinterpret_cast<sockaddr *>(&sa), sizeof(sa)) != 0)
			{
				return false;
			}
			socklen_t len = sizeof(sa);
			if (::getsockname(_front, reinterpret_cast<sockaddr *>(&sa), &len) != 0)
			{
				return false;
			}
			_port = ntohs(sa.sin_port);

			sa.sin_port = htons(peer_port);
			if (::connect(_back, reinterpret_cast<sockaddr *>(&sa), sizeof(sa)) != 0)
			{
				return false;
			}

			_running = true;
			_front_thread = std::thread([this]() { FrontLoop(); });
			_back_thread  = std::thread([this]() { BackLoop(); });
			return true;
		}

		uint16_t Port() const
		{
			return _port;
		}

		// Silently drops every datagram in both directions from now on.
		void StopForwarding()
		{
			_forwarding = false;
		}

		void Stop()
		{
			_running = false;
			if (_front_thread.joinable())
			{
				_front_thread.join();
			}
			if (_back_thread.joinable())
			{
				_back_thread.join();
			}
			if (_front >= 0)
			{
				::close(_front);
				_front = -1;
			}
			if (_back >= 0)
			{
				::close(_back);
				_back = -1;
			}
		}

	private:
		// Client -> peer. Remembers the client's address so replies can be routed back.
		void FrontLoop()
		{
			char buffer[2048];
			while (_running)
			{
				sockaddr_in from{};
				socklen_t from_len = sizeof(from);
				ssize_t n = ::recvfrom(_front, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr *>(&from), &from_len);
				if (n < 0)
				{
					continue;
				}
				{
					std::lock_guard<std::mutex> lock(_client_mutex);
					_client	   = from;
					_has_client = true;
				}
				if (_forwarding)
				{
					::send(_back, buffer, n, MSG_NOSIGNAL);
				}
			}
		}

		// Peer -> client.
		void BackLoop()
		{
			char buffer[2048];
			while (_running)
			{
				ssize_t n = ::recv(_back, buffer, sizeof(buffer), 0);
				if (n < 0)
				{
					continue;
				}
				sockaddr_in to{};
				{
					std::lock_guard<std::mutex> lock(_client_mutex);
					if (_has_client == false)
					{
						continue;
					}
					to = _client;
				}
				if (_forwarding)
				{
					::sendto(_front, buffer, n, MSG_NOSIGNAL, reinterpret_cast<sockaddr *>(&to), sizeof(to));
				}
			}
		}

		int _front	   = -1;
		int _back	   = -1;
		uint16_t _port = 0;
		std::atomic<bool> _running{false};
		std::atomic<bool> _forwarding{true};
		std::mutex _client_mutex;
		sockaddr_in _client{};
		bool _has_client = false;
		std::thread _front_thread;
		std::thread _back_thread;
	};

	// Counts the async socket events of a non-blocking OME socket and drains every
	// readable message, so a test can observe the worker-driven close path
	// (`EPOLLHUP` -> `OnClosed()`) and the bytes delivered before it.
	class SrtAsyncEvents : public ov::SocketAsyncInterface
	{
	public:
		void Attach(const std::shared_ptr<ov::Socket> &socket)
		{
			_socket = socket;
		}

		void OnConnected(const std::shared_ptr<const ov::SocketError> &error) override
		{
			if (error == nullptr)
			{
				_connected.fetch_add(1);
			}
			else
			{
				_connect_failed.fetch_add(1);
			}
		}

		void OnReadable() override
		{
			auto socket = _socket.lock();
			if (socket == nullptr)
			{
				return;
			}
			char buffer[1500];
			while (true)
			{
				auto result = socket->Recv(buffer, sizeof(buffer));
				if ((result.has_value() == false) || (result.value() == 0))
				{
					break;
				}
				_received_bytes.fetch_add(result.value());
			}
		}

		void OnClosed() override
		{
			_closed.fetch_add(1);
		}

		int Connected() const
		{
			return _connected.load();
		}
		int ConnectFailed() const
		{
			return _connect_failed.load();
		}
		size_t ReceivedBytes() const
		{
			return _received_bytes.load();
		}
		int Closed() const
		{
			return _closed.load();
		}

	private:
		std::weak_ptr<ov::Socket> _socket;
		std::atomic<int> _connected{0};
		std::atomic<int> _connect_failed{0};
		std::atomic<size_t> _received_bytes{0};
		std::atomic<int> _closed{0};
	};

	// Polls `predicate` every millisecond until it holds or `timeout` elapses.
	template <typename Tpredicate>
	bool WaitUntil(Tpredicate predicate, std::chrono::milliseconds timeout = std::chrono::milliseconds(LOOPBACK_TIMEOUT_MSEC))
	{
		auto deadline = std::chrono::steady_clock::now() + timeout;
		while (predicate() == false)
		{
			if (std::chrono::steady_clock::now() >= deadline)
			{
				return false;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return true;
	}

	// An asynchronous `srt_connect()` returns before the handshake completes; this
	// waits for libsrt to report the socket connected. Only `SRTS_CONNECTED` ends
	// the wait early: while `postConnect()` runs, `srt_getsockstate()` briefly
	// reports `SRTS_BROKEN` (neither connecting nor connected yet), and under
	// global-lock contention that window lasts tens of milliseconds.
	bool WaitSrtConnected(const std::shared_ptr<ov::Socket> &socket)
	{
		return WaitUntil([&]() { return ::srt_getsockstate(socket->GetNativeHandle()) == SRTS_CONNECTED; });
	}

	// Waits until libsrt reports the socket readable (a message has reached its
	// TSBPD play time), without consuming it. Uses a private SRT epoll because a
	// blocking-mode OME socket is not registered in any worker epoll.
	bool WaitSrtReadable(const std::shared_ptr<ov::Socket> &socket, int timeout_msec = LOOPBACK_TIMEOUT_MSEC)
	{
		int eid = ::srt_epoll_create();
		if (eid < 0)
		{
			return false;
		}
		int events = SRT_EPOLL_IN;
		bool ready = false;
		if (::srt_epoll_add_usock(eid, socket->GetNativeHandle(), &events) != SRT_ERROR)
		{
			SRT_EPOLL_EVENT out{};
			ready = (::srt_epoll_uwait(eid, &out, 1, timeout_msec) > 0) && OV_CHECK_FLAG(out.events, SRT_EPOLL_IN);
			::srt_epoll_remove_usock(eid, socket->GetNativeHandle());
		}
		::srt_epoll_release(eid);
		return ready;
	}

	// Receives one SRT message on an async (`SRTO_RCVSYN=false`) blocking-mode
	// socket: `SRT_EASYNCRCV` surfaces as a `0`-byte success, so retry until a
	// message arrives, the socket fails, or `timeout` elapses (returns `false`).
	bool RecvSrtMessage(const std::shared_ptr<ov::Socket> &socket, void *out, size_t capacity, size_t *received,
						std::chrono::milliseconds timeout = std::chrono::milliseconds(LOOPBACK_TIMEOUT_MSEC))
	{
		auto deadline = std::chrono::steady_clock::now() + timeout;
		while (true)
		{
			auto result = socket->Recv(out, capacity);
			if (result.has_value() == false)
			{
				return false;
			}
			if (result.value() > 0)
			{
				*received = result.value();
				return true;
			}
			if (std::chrono::steady_clock::now() >= deadline)
			{
				return false;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	// Keeps calling `Recv()` until it reports failure (the async no-data retries in
	// between are expected). Returns `false` if it is still succeeding at `timeout`.
	bool RecvUntilFailure(const std::shared_ptr<ov::Socket> &socket,
						  std::chrono::milliseconds timeout = std::chrono::milliseconds(LOOPBACK_TIMEOUT_MSEC))
	{
		char buffer[1500];
		size_t received = 0;
		while (RecvSrtMessage(socket, buffer, sizeof(buffer), &received, timeout))
		{
			// Late data before the shutdown is processed; keep draining.
		}
		return socket->Recv(buffer, sizeof(buffer)).has_value() == false;
	}

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

	// Closes every still-connected socket in `sockets` on scope exit, so an early
	// `ASSERT` failure does not trip the `~Socket` "not closed" assertion.
	class CloseOnExit
	{
	public:
		explicit CloseOnExit(std::vector<std::shared_ptr<ov::Socket>> &sockets)
			: _sockets(sockets)
		{
		}
		~CloseOnExit()
		{
			for (auto &socket : _sockets)
			{
				if (socket->GetState() == ov::SocketState::Connected)
				{
					socket->Close();
				}
			}
		}

	private:
		std::vector<std::shared_ptr<ov::Socket>> &_sockets;
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
// `SRT_EASYNCRCV`. The option is set before `Connect()` on purpose: a synchronous
// `srt_connect()` completes the handshake on the caller thread, and libsrt's
// receive worker only registers the socket on its next loop iteration, so a data
// packet that arrives in that window is stored for the (finished) connect and
// never delivered. The asynchronous handshake registers the socket on the worker
// itself, which makes the first message deterministic.
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

	// Returns a blocking-mode OME SRT client connected to `peer` with async recv,
	// so a no-data read yields `SRT_EASYNCRCV` instead of blocking.
	std::shared_ptr<ov::Socket> ConnectClient(SrtPeer &peer)
	{
		return ConnectClientVia(peer, peer.Port());
	}

	// Same as `ConnectClient()`, but connects to `port` (a `UdpRelay` in front of `peer`).
	std::shared_ptr<ov::Socket> ConnectClientVia(SrtPeer &peer, uint16_t port)
	{
		auto client = _pool->AllocSocket(ov::SocketFamily::Inet);
		if (client == nullptr)
		{
			return nullptr;
		}

		client->MakeBlocking();
		// Async recv (and thereby an async handshake, see the fixture comment):
		// no data -> `SRT_EASYNCRCV`, broken link -> `SRT_ECONNLOST`.
		client->SetSockOpt<bool>(SRTO_RCVSYN, false);

		peer.AcceptAsync();
		auto error = client->Connect(LoopbackAddress(port), LOOPBACK_TIMEOUT_MSEC);
		peer.WaitAccepted();

		if (error != nullptr || peer.IsConnected() == false || WaitSrtConnected(client) == false)
		{
			client->Close();
			return nullptr;
		}

		_client = client;
		return client;
	}

	// Returns a non-blocking OME SRT client (driven by the pool worker, events
	// delivered to `events`) connected to `port`.
	std::shared_ptr<ov::Socket> ConnectAsyncClientVia(SrtPeer &peer, uint16_t port, const std::shared_ptr<SrtAsyncEvents> &events)
	{
		auto client = _pool->AllocSocket(ov::SocketFamily::Inet);
		if (client == nullptr)
		{
			return nullptr;
		}

		events->Attach(client);
		if (client->MakeNonBlocking(events) == false)
		{
			client->Close();
			return nullptr;
		}

		peer.AcceptAsync();
		auto error = client->Connect(LoopbackAddress(port), LOOPBACK_TIMEOUT_MSEC);
		peer.WaitAccepted();

		if (error != nullptr || peer.IsConnected() == false || WaitSrtConnected(client) == false)
		{
			client->Close();
			return nullptr;
		}

		_client = client;
		return client;
	}
};

// `srt_startup()` / `srt_cleanup()` must be reference counted: a library user such
// as FFmpeg's libsrt protocol pairs them per connection, and an unbalanced count
// tears the whole library down (every listener included) on the first close.
// libsrt 1.5.4 returned early from `srt_startup()` while the GC was already
// running, so OME's own startup was cancelled by the first FFmpeg SRT close.
TEST_F(SocketRecvSrtTest, StartupCleanupIsRefCounted)
{
	SrtPeer peer;
	ASSERT_TRUE(peer.Listen());

	// A nested user: startup once more and clean up once, like one FFmpeg open/close.
	ASSERT_NE(::srt_startup(), -1);
	ASSERT_EQ(::srt_cleanup(), 0);

	// The fixture's own startup must still be in effect: the listener is alive and accepts.
	EXPECT_EQ(::srt_getsockstate(peer.ListenerHandle()), SRTS_LISTENING);
	auto client = ConnectClient(peer);
	ASSERT_NE(client, nullptr);
	EXPECT_EQ(client->GetState(), ov::SocketState::Connected);
}

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

// After the peer delivers data and calls `srt_close()`, the data must be read
// intact, the following reads must converge on a failure, and a failed socket
// must never report a spurious success afterwards.
//
// The failure is `Disconnected` (`SRT_ECONNLOST`) when `Recv()` observes the
// broken socket before libsrt's GC, and `Error` (`SRT_EINVSOCK`) when the GC,
// which every `srt_close()` in the process wakes, has already moved the
// drained socket to `SRTS_CLOSED`. The order is decided by thread scheduling,
// so both are accepted and the outcome is recorded.
//
// Two messages are sent and the second is left unread until after the peer
// closes: the GC leaves a broken socket alone while unread packets remain, which
// keeps the second message readable through the close. Packets still inside
// the TSBPD latency window when the shutdown arrives are dropped by libsrt, so
// the second message is confirmed playable before the peer closes.
TEST_F(SocketRecvSrtTest, GracefulPeerCloseReportsFailure)
{
	Watchdog watchdog(std::chrono::seconds(15), "GracefulPeerCloseReportsFailure");

	SrtPeer peer;
	ASSERT_TRUE(peer.Listen());
	auto client = ConnectClient(peer);
	ASSERT_NE(client, nullptr);

	const char first[]	= "srt-graceful-eof-1";
	const char second[] = "srt-graceful-eof-2";
	ASSERT_EQ(peer.Send(first, sizeof(first)), static_cast<int>(sizeof(first)));
	ASSERT_EQ(peer.Send(second, sizeof(second)), static_cast<int>(sizeof(second)));

	char buffer[1500] = {0};
	size_t received	  = 0;
	ASSERT_TRUE(RecvSrtMessage(client, buffer, sizeof(buffer), &received));
	ASSERT_EQ(received, sizeof(first));
	EXPECT_EQ(::memcmp(buffer, first, sizeof(first)), 0);
	ASSERT_TRUE(WaitSrtReadable(client));

	peer.CloseConnection();

	ASSERT_TRUE(RecvSrtMessage(client, buffer, sizeof(buffer), &received));
	ASSERT_EQ(received, sizeof(second));
	EXPECT_EQ(::memcmp(buffer, second, sizeof(second)), 0);

	EXPECT_TRUE(RecvUntilFailure(client));
	auto state = client->GetState();
	EXPECT_TRUE((state == ov::SocketState::Disconnected) || (state == ov::SocketState::Error))
		<< ov::StringFromSocketState(state);
	RecordProperty("state", ov::StringFromSocketState(state));
	EXPECT_FALSE(client->Recv(buffer, sizeof(buffer)).has_value());
}

// Same peer shutdown observed through the worker (non-blocking socket): the
// terminal SRT state must surface as `EPOLLHUP` -> `Disconnected` + `OnClosed()`,
// not as `EPOLLERR` -> `Error`.
TEST_F(SocketRecvSrtTest, GracefulPeerCloseFiresCloseCallbackAsDisconnected)
{
	Watchdog watchdog(std::chrono::seconds(15), "GracefulPeerCloseFiresCloseCallbackAsDisconnected");

	SrtPeer peer;
	ASSERT_TRUE(peer.Listen());
	auto events = std::make_shared<SrtAsyncEvents>();
	auto client = ConnectAsyncClientVia(peer, peer.Port(), events);
	ASSERT_NE(client, nullptr);
	ASSERT_EQ(events->ConnectFailed(), 0);

	const char payload[] = "srt-async-graceful-eof";
	ASSERT_EQ(peer.Send(payload, sizeof(payload)), static_cast<int>(sizeof(payload)));
	ASSERT_TRUE(WaitUntil([&]() { return events->ReceivedBytes() == sizeof(payload); }));

	peer.CloseConnection();

	ASSERT_TRUE(WaitUntil([&]() { return events->Closed() == 1; }));
	EXPECT_EQ(client->GetState(), ov::SocketState::Disconnected);
}

// libsrt declares a silent peer dead only after 16 expiration timer events at
// least 300 ms apart, so a cut link takes about 5 s to reach `SRTS_BROKEN`
// regardless of `SRTO_PEERIDLETIMEO`; the waits below allow for that.
constexpr int SRT_BROKEN_LINK_WAIT_MSEC = 15000;

// A link that dies without any shutdown handshake (the relay drops every
// datagram) must expire into `SRTS_BROKEN`, which `Recv()` reports as
// `SRT_ECONNLOST` -> `Disconnected`, not `Error`.
TEST_F(SocketRecvSrtTest, BrokenLinkReportsDisconnect)
{
	Watchdog watchdog(std::chrono::seconds(30), "BrokenLinkReportsDisconnect");

	SrtPeer peer;
	ASSERT_TRUE(peer.Listen());
	UdpRelay relay;
	ASSERT_TRUE(relay.Start(peer.Port()));

	auto client = ConnectClientVia(peer, relay.Port());
	ASSERT_NE(client, nullptr);

	// Prove the relayed link carries data before cutting it.
	const char payload[] = "srt-broken-link";
	ASSERT_EQ(peer.Send(payload, sizeof(payload)), static_cast<int>(sizeof(payload)));
	char buffer[1500] = {0};
	size_t received	  = 0;
	ASSERT_TRUE(RecvSrtMessage(client, buffer, sizeof(buffer), &received));
	ASSERT_EQ(received, sizeof(payload));

	relay.StopForwarding();

	EXPECT_TRUE(RecvUntilFailure(client, std::chrono::milliseconds(SRT_BROKEN_LINK_WAIT_MSEC)));
	EXPECT_EQ(client->GetState(), ov::SocketState::Disconnected);
}

// Same dead link observed through the worker: `SRTS_BROKEN` + `SRT_EPOLL_ERR`
// must map to `EPOLLHUP` (`Disconnected` + `OnClosed()`), not `EPOLLERR` (`Error`).
TEST_F(SocketRecvSrtTest, BrokenLinkFiresCloseCallbackAsDisconnected)
{
	Watchdog watchdog(std::chrono::seconds(30), "BrokenLinkFiresCloseCallbackAsDisconnected");

	SrtPeer peer;
	ASSERT_TRUE(peer.Listen());
	UdpRelay relay;
	ASSERT_TRUE(relay.Start(peer.Port()));

	auto events = std::make_shared<SrtAsyncEvents>();
	auto client = ConnectAsyncClientVia(peer, relay.Port(), events);
	ASSERT_NE(client, nullptr);

	const char payload[] = "srt-async-broken-link";
	ASSERT_EQ(peer.Send(payload, sizeof(payload)), static_cast<int>(sizeof(payload)));
	ASSERT_TRUE(WaitUntil([&]() { return events->ReceivedBytes() == sizeof(payload); }));

	relay.StopForwarding();

	ASSERT_TRUE(WaitUntil([&]() { return events->Closed() == 1; }, std::chrono::milliseconds(SRT_BROKEN_LINK_WAIT_MSEC)));
	EXPECT_EQ(client->GetState(), ov::SocketState::Disconnected);
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

// ===========================================================================
// SRT concurrency / stress
//
// SRT ports of the TCP stress tests above, plus a close-vs-epoll storm. OME
// relies on libsrt's own thread safety for `srt_close()` on one thread racing
// `srt_epoll_uwait()` on a pool worker, so the pool here has several workers.
// libsrt itself is not TSan-instrumented; these catch hangs (`Watchdog`),
// crashes, and misclassified disconnects.
// ===========================================================================
class SrtConcurrencyTest : public ::testing::Test
{
protected:
	static constexpr int WORKER_COUNT = 8;

	void SetUp() override
	{
		ASSERT_NE(::srt_startup(), -1);
		_pool = ov::SocketPool::Create("test-srt-conc", ov::SocketType::Srt, false);
		ASSERT_NE(_pool, nullptr);
		ASSERT_TRUE(_pool->Initialize(WORKER_COUNT));
	}

	void TearDown() override
	{
		if (_pool != nullptr)
		{
			_pool->Uninitialize();
		}
		::srt_cleanup();
	}

	// A blocking-mode OME SRT client with async recv (and async handshake, see
	// `SocketRecvSrtTest`) connected to `peer`, whose accept thread must already
	// be running.
	std::shared_ptr<ov::Socket> ConnectBlocking(SrtPeer &peer)
	{
		auto client = _pool->AllocSocket(ov::SocketFamily::Inet);
		if (client == nullptr)
		{
			return nullptr;
		}

		client->MakeBlocking();
		client->SetSockOpt<bool>(SRTO_RCVSYN, false);
		if (Connected(client, client->Connect(LoopbackAddress(peer.Port()), LOOPBACK_TIMEOUT_MSEC)) == false)
		{
			client->Close();
			return nullptr;
		}
		return client;
	}

	// A non-blocking OME SRT client (registered in a worker's SRT epoll) connected to `peer`.
	std::shared_ptr<ov::Socket> ConnectAsync(SrtPeer &peer, const std::shared_ptr<SrtAsyncEvents> &events)
	{
		auto client = _pool->AllocSocket(ov::SocketFamily::Inet);
		if (client == nullptr)
		{
			return nullptr;
		}

		events->Attach(client);
		if (client->MakeNonBlocking(events) == false)
		{
			client->Close();
			return nullptr;
		}
		if (Connected(client, client->Connect(LoopbackAddress(peer.Port()), LOOPBACK_TIMEOUT_MSEC)) == false)
		{
			client->Close();
			return nullptr;
		}
		return client;
	}

	// Records why a connect attempt failed (`LastFailure()`), so a storm assertion
	// can say whether the connect itself was refused or the handshake never completed.
	bool Connected(const std::shared_ptr<ov::Socket> &client, const std::shared_ptr<const ov::SocketError> &error)
	{
		if (error != nullptr)
		{
			_last_failure = ov::String::FormatString("connect failed: %s", error->What()).CStr();
			return false;
		}
		if (WaitSrtConnected(client) == false)
		{
			_last_failure = ov::String::FormatString("handshake did not complete, srt state %d, reject reason: %s",
													 ::srt_getsockstate(client->GetNativeHandle()),
													 ::srt_rejectreason_str(::srt_getrejectreason(client->GetNativeHandle())))
								.CStr();
			return false;
		}
		return true;
	}

	const std::string &LastFailure() const
	{
		return _last_failure;
	}

	std::shared_ptr<ov::SocketPool> _pool;
	std::string _last_failure;
};

// Many independent SRT sockets each receive on their own thread at the same
// time. Connections are established serially (an SRT handshake is heavy), the
// receives run concurrently; every socket must get exactly its message.
TEST_F(SrtConcurrencyTest, ManyIndependentSocketsReceiveConcurrently)
{
	Watchdog watchdog(std::chrono::seconds(30), "SrtManyIndependentSocketsReceiveConcurrently");

	constexpr int SOCKET_COUNT = 48;
	const char payload[]	   = "srt-concurrent-payload";

	SrtPeer peer;
	ASSERT_TRUE(peer.Listen(SOCKET_COUNT));
	peer.AcceptAsync(SOCKET_COUNT);

	std::vector<std::shared_ptr<ov::Socket>> clients;
	CloseOnExit close_on_exit(clients);
	for (int i = 0; i < SOCKET_COUNT; i++)
	{
		auto client = ConnectBlocking(peer);
		ASSERT_NE(client, nullptr) << "socket " << i << ": " << LastFailure();
		clients.push_back(client);
	}
	peer.WaitAccepted();
	ASSERT_EQ(peer.ConnectionCount(), SOCKET_COUNT);

	std::atomic<int> success{0};
	std::vector<std::thread> threads;
	threads.reserve(SOCKET_COUNT);
	for (int i = 0; i < SOCKET_COUNT; i++)
	{
		threads.emplace_back([&, i]() {
			peer.Send(i, payload, sizeof(payload));

			char buffer[1500] = {0};
			size_t received = 0;
			if (RecvSrtMessage(clients[i], buffer, sizeof(buffer), &received) &&
				(received == sizeof(payload)) &&
				(::memcmp(buffer, payload, sizeof(payload)) == 0))
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
}

// Each peer connection is closed concurrently with an in-flight `Recv()` on the
// OME side. Every `Recv()` must fail, a failed socket must never report a
// spurious success afterwards, and the storm of simultaneous `srt_close()`
// calls must not hang or crash.
//
// The failure surfaces as `Disconnected` (`SRT_ECONNLOST`) only if `Recv()` runs
// before libsrt's GC thread moves the broken socket to `SRTS_CLOSED`; every
// `srt_close()` in the process wakes that GC, and afterwards `srt_recvmsg2()`
// fails with `SRT_EINVSOCK`, which `Recv()` classifies as `Error`. Both
// outcomes are therefore accepted and the split is recorded.
TEST_F(SrtConcurrencyTest, RecvRacesPeerDisconnectStorm)
{
	Watchdog watchdog(std::chrono::seconds(30), "SrtRecvRacesPeerDisconnectStorm");

	constexpr int SOCKET_COUNT = 48;

	SrtPeer peer;
	ASSERT_TRUE(peer.Listen(SOCKET_COUNT));
	peer.AcceptAsync(SOCKET_COUNT);

	std::vector<std::shared_ptr<ov::Socket>> clients;
	CloseOnExit close_on_exit(clients);
	for (int i = 0; i < SOCKET_COUNT; i++)
	{
		auto client = ConnectBlocking(peer);
		ASSERT_NE(client, nullptr) << "socket " << i << ": " << LastFailure();
		clients.push_back(client);
	}
	peer.WaitAccepted();
	ASSERT_EQ(peer.ConnectionCount(), SOCKET_COUNT);

	std::atomic<int> failed{0};
	std::atomic<int> disconnected{0};
	std::atomic<int> errored{0};
	std::atomic<int> spurious_success{0};
	std::vector<std::thread> threads;
	threads.reserve(SOCKET_COUNT);
	for (int i = 0; i < SOCKET_COUNT; i++)
	{
		threads.emplace_back([&, i]() {
			std::thread peer_closer([&, i]() { peer.CloseConnection(i); });

			if (RecvUntilFailure(clients[i]))
			{
				failed.fetch_add(1);
			}

			peer_closer.join();

			switch (clients[i]->GetState())
			{
				case ov::SocketState::Disconnected:
					disconnected.fetch_add(1);
					break;
				case ov::SocketState::Error:
					errored.fetch_add(1);
					break;
				default:
					break;
			}

			char buffer[1500];
			if (clients[i]->Recv(buffer, sizeof(buffer)).has_value())
			{
				spurious_success.fetch_add(1);
			}
		});
	}
	for (auto &thread : threads)
	{
		thread.join();
	}

	EXPECT_EQ(failed.load(), SOCKET_COUNT);
	EXPECT_EQ(disconnected.load() + errored.load(), SOCKET_COUNT);
	EXPECT_EQ(spurious_success.load(), 0);
	RecordProperty("disconnected", disconnected.load());
	RecordProperty("errored", errored.load());
}

// Non-blocking sockets spread over several workers are closed from the OME side
// (`Close()` enqueued to the owning worker, which then calls `srt_close()`) and
// from the peer side at the same time, round after round, while the other
// workers sit in `srt_epoll_uwait()`. The test only requires that every socket
// leaves `Connected` and that no round hangs or crashes.
TEST_F(SrtConcurrencyTest, CloseRacesEpollStorm)
{
	Watchdog watchdog(std::chrono::seconds(120), "SrtCloseRacesEpollStorm");

	constexpr int ROUNDS		= 25;
	constexpr int SOCKET_COUNT	= 8;
	constexpr int TOTAL_CLOSES = ROUNDS * SOCKET_COUNT;

	SrtPeer peer;
	ASSERT_TRUE(peer.Listen(SOCKET_COUNT * 2));

	int closed_from_ome = 0;
	for (int round = 0; round < ROUNDS; round++)
	{
		peer.AcceptAsync(SOCKET_COUNT);

		std::vector<std::shared_ptr<SrtAsyncEvents>> events;
		std::vector<std::shared_ptr<ov::Socket>> clients;
		CloseOnExit close_on_exit(clients);
		for (int i = 0; i < SOCKET_COUNT; i++)
		{
			auto e		= std::make_shared<SrtAsyncEvents>();
			auto client = ConnectAsync(peer, e);
			ASSERT_NE(client, nullptr) << "round " << round << " socket " << i << ": " << LastFailure();
			events.push_back(e);
			clients.push_back(client);
		}
		peer.WaitAccepted();
		ASSERT_EQ(peer.ConnectionCount(), SOCKET_COUNT) << "round " << round;

		// Alternate who closes first per socket so both orders race within a round.
		std::vector<std::thread> threads;
		for (int i = 0; i < SOCKET_COUNT; i++)
		{
			threads.emplace_back([&, i]() {
				if ((i + round) % 2 == 0)
				{
					clients[i]->Close();
					peer.CloseConnection(i);
				}
				else
				{
					peer.CloseConnection(i);
					clients[i]->Close();
				}
			});
		}
		for (auto &thread : threads)
		{
			thread.join();
		}

		for (int i = 0; i < SOCKET_COUNT; i++)
		{
			ASSERT_TRUE(WaitUntil([&]() { return clients[i]->GetState() != ov::SocketState::Connected; }))
				<< "round " << round << " socket " << i << " still connected";
			closed_from_ome++;
		}

		// Forget this round's peer sockets (all already closed) before the next accept batch.
		peer.CloseConnection();
	}

	EXPECT_EQ(closed_from_ome, TOTAL_CLOSES);
}
