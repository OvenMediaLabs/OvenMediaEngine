//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "socket.h"

#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>

#include "epoll_wrapper.h"
#include "socket_pool/socket_pool.h"
#include "socket_private.h"
#include "socket_utilities.h"

#define OV_LOG_PREFIX_FORMAT "[#%d] [%p] "
#define OV_LOG_PREFIX_VALUE (GetNativeHandle() == InvalidSocket) ? 0 : GetNativeHandle(), this

// Debugging purpose
#include "socket_profiler.h"
#include "stats_counter.h"

#ifdef OME_LATENCY_PROBE
#include <linux/net_tstamp.h>
#include <sched.h>
#include <sys/prctl.h>

#include <base/ovlibrary/latency_probe.h>

// Latency probe (OME_LATENCY_PROBE only): measure "kernel received the request bytes"
// (SO_TIMESTAMPING software RX timestamp) vs "we actually read them" (recvmsg return). A large
// gap means the request sat in the kernel socket buffer while our epoll worker was late to read
// it (epoll/scheduling lateness before our session-level read). Logged only when the gap is big.
static void RecvLateLog(int handle, long long late_ms, long bytes)
{
	char comm[24] = {0};
	::prctl(PR_GET_NAME, comm, 0, 0, 0);
	ov::LatencyProbeLog("RECV_LATE", "comm=%s psr=%d fd=%d late_ms=%lld bytes=%ld",
						comm, ::sched_getcpu(), handle, late_ms, bytes);
}

// Latency probe (OME_LATENCY_PROBE only): log when a socket Send cannot fully flush inline
// (kernel send buffer full -> EAGAIN/partial), i.e. the peer is not draining.
// comm identifies the pool worker (SPLLHLS/SPRTMP/...).
static void SendStallLog(int handle, long sent_now, long left)
{
	char comm[24] = {0};
	::prctl(PR_GET_NAME, comm, 0, 0, 0);
	ov::LatencyProbeLog("SEND_STALL", "comm=%s fd=%d sent_now=%ld left=%ld",
						comm, handle, sent_now, left);
}
#endif	// OME_LATENCY_PROBE

namespace ov
{
	static socket_id_t IssueSocketId()
	{
		static std::atomic<uint64_t> last_id{0};

		return socket_id_t(++last_id);
	}

	Socket::Socket(PrivateToken token, const std::shared_ptr<SocketPoolWorker> &worker)
		: _worker(worker),
		  _id(IssueSocketId())
	{
		OV_ASSERT(worker != nullptr, "Worker must be not nullptr");
	}

	// Creates a socket using remote information.
	Socket::Socket(PrivateToken token, const std::shared_ptr<SocketPoolWorker> &worker,
				   SocketWrapper socket, const SocketAddress &remote_address)
		: Socket(token, worker)
	{
		_socket = socket;

		if (_socket.IsValid())
		{
			SetNativeHandleOpened();
		}

		// Remote information is present, considered already connected
		SetState(SocketState::Connected);

		_remote_address = std::make_shared<SocketAddress>(remote_address);
	}

	Socket::~Socket()
	{
		const auto io_ref = _io_ref.fetch_or(IO_REF_CLOSING, std::memory_order_acq_rel);

		// Verify that the socket is closed normally
		OV_ASSERT((io_ref & IO_REF_CLOSING) != 0, "Socket is not closed. Current state: %s", StringFromSocketState(GetState()));

		if ((io_ref & IO_REF_CLOSING) == 0)
		{
			// Only the owner reference is left, since a syscall in flight is made through a `shared_ptr` of this socket
			OV_ASSERT2(io_ref == 1);

			CloseNativeHandle(GetType(), GetNativeHandle());
		}
		else
		{
			OV_ASSERT2((io_ref & ~IO_REF_CLOSING) == 0);
		}

		CHECK_STATE2(== SocketState::Closed, >= SocketState::Disconnected, );
	}

	void Socket::SetNativeHandleOpened()
	{
		_io_ref.store(1, std::memory_order_release);
	}

	bool Socket::BeginIo() const
	{
		auto io_ref = _io_ref.load(std::memory_order_relaxed);

		do
		{
			if ((io_ref & IO_REF_CLOSING) != 0)
			{
				return false;
			}
		} while (_io_ref.compare_exchange_weak(io_ref, io_ref + 1, std::memory_order_acquire, std::memory_order_relaxed) == false);

		return true;
	}

	bool Socket::PinNativeHandle() const
	{
		auto io_ref = _io_ref.load(std::memory_order_relaxed);

		do
		{
			if ((io_ref & ~IO_REF_CLOSING) == 0)
			{
				// Released, or never assigned
				return false;
			}
		} while (_io_ref.compare_exchange_weak(io_ref, io_ref + 1, std::memory_order_acquire, std::memory_order_relaxed) == false);

		return true;
	}

	void Socket::EndIo() const
	{
		if (_io_ref.fetch_sub(1, std::memory_order_acq_rel) == (IO_REF_CLOSING | 1))
		{
			ReleaseNativeHandle();
		}
	}

	void Socket::ReleaseNativeHandle() const
	{
		// For SRT, `CloseInternal()` has already called `srt_close()`: it is what wakes a blocked SRT call,
		// and libsrt does not reuse the ID soon
		if (GetType() != SocketType::Srt)
		{
			CloseNativeHandle(GetType(), GetNativeHandle());
		}
	}

	void Socket::CloseNativeHandle(SocketType type, int native_handle)
	{
		switch (type)
		{
			case SocketType::Tcp:
			case SocketType::Udp:
				::close(native_handle);
				break;

			case SocketType::Srt:
				::srt_close(native_handle);
				break;

			default:
				break;
		}
	}

	bool Socket::Create(const SocketType type, const SocketFamily family)
	{
		CHECK_STATE(== SocketState::Closed, false);

		// `_socket` keeps its number for the life of the object, even after the release, so a socket is created only once
		if (_socket.GetNativeHandle() != InvalidSocket)
		{
			logae("Socket is already created: %d", GetNativeHandle());
			return false;
		}

		logat("Trying to create new socket (type: %d)...", ToUnderlyingType(type));

		switch (type)
		{
			case SocketType::Tcp:
			case SocketType::Udp:
				_socket.SetSocket(type, ::socket(GetSocketProtocolFamily(family), (type == SocketType::Tcp) ? SOCK_STREAM : SOCK_DGRAM, 0));
				break;

			case SocketType::Srt:
				_socket.SetSocket(type, ::srt_create_socket());
				break;

			default:
				break;
		}

		do
		{
			if (_socket.IsValid() == false)
			{
				logae("An error occurred while create socket");
				break;
			}

			SetNativeHandleOpened();

			logat("Socket descriptor is created (%s/%s)",
				  StringFromSocketFamily(family),
				  StringFromSocketType(type));

			_has_close_command		= false;
			_end_of_stream			= false;

			_connection_event_fired = false;

			_family					= family;

			SetState(SocketState::Created);

			switch (type)
			{
				case SocketType::Unknown:
					break;

				case SocketType::Srt:
					SetSockOpt(SRTO_IPV6ONLY, 1);
					break;

				case SocketType::Udp:
					(family == SocketFamily::Inet)
						? SetSockOpt(IPPROTO_IP, IP_PKTINFO, 1)
						: SetSockOpt(IPPROTO_IPV6, IPV6_RECVPKTINFO, 1);

					[[fallthrough]];

				case SocketType::Tcp:
					if (family == SocketFamily::Inet6)
					{
						// In some environments, listening to IPv6 will also listen to IPv4
						// This setting lets this socket listen to IPv6 only
						SetSockOpt(IPPROTO_IPV6, IPV6_V6ONLY, 1);
					}
					break;
			}

			return true;
		} while (false);

		// No handle was assigned, so there is nothing to close
		return false;
	}

	bool Socket::SetBlockingInternal(BlockingMode mode)
	{
		IoGuard io_guard(*this);

		if (io_guard.IsAcquired() == false)
		{
			// In case an error occurs during the connection process, the socket may be in a closed state.
			return false;
		}

		auto socket = GetSharedPtr();

		switch (GetType())
		{
			case SocketType::Tcp:
			case SocketType::Udp: {
				int result = ::fcntl(GetNativeHandle(), F_GETFL, 0);

				if (result == -1)
				{
					logae("Could not obtain flags: %s", Error::CreateErrorFromErrno()->What());
					return false;
				}

				int flags = (mode == BlockingMode::Blocking) ? (result & ~O_NONBLOCK) : (result | O_NONBLOCK);

				result	  = ::fcntl(GetNativeHandle(), F_SETFL, flags);

				if (result == -1)
				{
					logae("Could not set flags: %s", Error::CreateErrorFromErrno()->What());
					return false;
				}

				break;
			}

			case SocketType::Srt:
				if (SetSockOpt(SRTO_RCVSYN, mode == BlockingMode::Blocking) == false || SetSockOpt(SRTO_SNDSYN, mode == BlockingMode::Blocking) == false)
				{
					logae("Could not set flags to SRT socket %d: %s", GetNativeHandle(), SrtError::CreateErrorFromSrt()->What());
					return false;
				}

				break;

			default:
				OV_ASSERT(false, "Invalid socket type: %d", ToUnderlyingType(GetType()));
				return false;
		}

		return true;
	}

	bool Socket::AppendCommand(DispatchCommand command, bool dispatch_immediately)
	{
		SOCKET_PROFILER_INIT();
		LockGuard lock_guard(_dispatch_queue_lock);
		SOCKET_PROFILER_AFTER_LOCK();

		SOCKET_PROFILER_POST_HANDLER([&](int64_t lock_elapsed, int64_t total_elapsed) {
			if ((lock_elapsed > 100) || (_dispatch_queue.size() > 10))
			{
				logtw("[SockProfiler] AppendCommand() - %s, Queue: %zu, Lock: %dms, Total: %dms", ToString().CStr(), _dispatch_queue.size(), lock_elapsed, total_elapsed);
			}
		});

		_dispatch_queue.push_back(std::move(command));

		if (dispatch_immediately)
		{
			switch (DispatchEvents())
			{
				case DispatchResult::Dispatched:
					return true;

				case DispatchResult::PartialDispatched:
					_worker->EnqueueToDispatchLater(GetSharedPtr());
					return true;

				case DispatchResult::Error:
					break;
			}
		}

		return true;
	}

	bool Socket::AddToWorker(bool need_to_wait_first_epoll_event)
	{
		{
			LockGuard lock_guard(_worker_mutex);

			if (_added_to_worker)
			{
				// `DeleteFromWorker()` can be called multiple times due to timing,
				// but `AddToWorker()` must not be called redundantly.
				OV_ASSERT2(false);
				return true;
			}

			if (IsOpen() == false)
			{
				// Another thread closed the socket before it is added
				return false;
			}

			if (GetType() == SocketType::Srt)
			{
				// libsrt raises `SRT_EPOLL_OUT` on `srt_epoll_add_usock()` only for a connected socket with free send buffer space,
				// so a listening or connecting SRT socket has no first event to wait for
				need_to_wait_first_epoll_event = false;
			}

			if (need_to_wait_first_epoll_event)
			{
				ResetFirstEpollEventReceived();
			}

			if (_worker->AddToEpoll(GetSharedPtr()) == false)
			{
				return false;
			}

			_added_to_worker = true;
		}

		if (need_to_wait_first_epoll_event)
		{
			// `CloseInternal()` also wakes this up, and then the socket must not be reported as added
			return WaitForFirstEpollEvent() && IsOpen();
		}

		return true;
	}

	void Socket::DeleteFromWorker()
	{
		LockGuard lock_guard(_worker_mutex);

		if (_added_to_worker)
		{
			// Cleared even when the removal fails, so no later call passes the number to epoll after its release.
			// The handle is still open here: a close removes the socket before it drops the owner reference.
			_added_to_worker = false;
			_worker->DeleteFromEpoll(GetSharedPtr());
		}
	}

	bool Socket::MakeBlocking()
	{
		if (GetState() != SocketState::Created)
		{
			logae("Blocking mode can only be changed at the beginning of socket creation");
			OV_ASSERT2(false);
			return false;
		}

		if (_blocking_mode == BlockingMode::Blocking)
		{
			// Socket is already blocking mode
			OV_ASSERT2(std::atomic_load(&_callback) == nullptr);

			return true;
		}

		// Prevent to call the callback while change blocking mode
		auto old_callback = std::atomic_exchange(&_callback, {});

		if (SetBlockingInternal(BlockingMode::Blocking))
		{
			DeleteFromWorker();
			_blocking_mode = BlockingMode::Blocking;
			return true;
		}

		// Rollback
		std::atomic_store(&_callback, old_callback);

		return false;
	}

	bool Socket::MakeNonBlocking(std::shared_ptr<SocketAsyncInterface> callback)
	{
		if (GetState() != SocketState::Created)
		{
			logae("Blocking mode can only be changed at the beginning of socket creation");
			OV_ASSERT2(false);
			return false;
		}

		return MakeNonBlockingInternal(std::move(callback), true);
	}

	bool Socket::MakeNonBlockingInternal(std::shared_ptr<SocketAsyncInterface> callback, bool need_to_wait_first_epoll_event)
	{
		if (_blocking_mode == BlockingMode::NonBlocking)
		{
			// Socket is already non-blocking mode
			std::atomic_store(&_callback, std::move(callback));

			return true;
		}

		auto old_callback = std::atomic_exchange(&_callback, callback);

		_blocking_mode	  = BlockingMode::NonBlocking;

#ifdef OME_LATENCY_PROBE
		// Latency probe (OME_LATENCY_PROBE only): enable software RX timestamping on all
		// non-blocking TCP sockets (accepted server-side ClientSockets and outgoing client
		// sockets) so Recv() can measure how long a request sat in the kernel buffer before
		// our worker read it.
		if (GetType() == SocketType::Tcp)
		{
			int ts_flags = SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_SOFTWARE;
			SetSockOpt(SOL_SOCKET, SO_TIMESTAMPING, &ts_flags, sizeof(ts_flags));
		}
#endif	// OME_LATENCY_PROBE

		if (
			SetBlockingInternal(BlockingMode::NonBlocking) &&
			AddToWorker(need_to_wait_first_epoll_event))
		{
			return true;
		}

		// Rollback
		std::atomic_store(&_callback, std::move(old_callback));
		_blocking_mode = BlockingMode::Blocking;

		return false;
	}

	bool Socket::Bind(const SocketAddress &address)
	{
		CHECK_STATE(== SocketState::Created, false);

		IoGuard io_guard(*this);

		if (io_guard.IsAcquired() == false)
		{
			logae("Could not bind socket (Invalid socket)");
			return false;
		}

		logat("Binding to %s...", address.ToString().CStr());

		switch (GetType())
		{
			case SocketType::Udp:
			case SocketType::Tcp: {
				int result = ::bind(GetNativeHandle(), address, address.GetSockAddrInLength());

				if (result == 0)
				{
					// Succeeded
					_local_address = std::make_shared<SocketAddress>(address);
					_local_address->SetTransport(GetType() == SocketType::Tcp
						? SocketAddress::Transport::Tcp
						: SocketAddress::Transport::Udp);
				}
				else
				{
					// Failed
					logae("Could not bind to %s (%d, %s)",
						  address.ToString().CStr(),
						  result,
						  Error::CreateErrorFromErrno()->What());
					return false;
				}

				break;
			}

			case SocketType::Srt: {
				int result = ::srt_bind(GetNativeHandle(), address, static_cast<int>(address.GetSockAddrInLength()));

				if (result != SRT_ERROR)
				{
					// Succeeded
					_local_address = std::make_shared<SocketAddress>(address);
					_local_address->SetTransport(SocketAddress::Transport::Srt);
				}
				else
				{
					// Failed
					logae("Could not bind to %s for SRT (%s)", address.ToString().CStr(), srt_getlasterror_str());
					return false;
				}

				break;
			}

			default:
				OV_ASSERT(false, "Invalid socket type: %d", ToUnderlyingType(GetType()));
				return false;
		}

		SetState(SocketState::Bound);
		logat("Bound successfully");

		return true;
	}

	bool Socket::Listen(int backlog)
	{
		CHECK_STATE(== SocketState::Bound, false);

		IoGuard io_guard(*this);

		if (io_guard.IsAcquired() == false)
		{
			return false;
		}

		switch (GetType())
		{
			case SocketType::Tcp: {
				int result = ::listen(GetNativeHandle(), backlog);
				if (result == 0)
				{
					// Succeeded
					SetState(SocketState::Listening);
					return true;
				}

				logae("Could not listen: %s", Error::CreateErrorFromErrno()->What());
				break;
			}

			case SocketType::Srt: {
				int result = ::srt_listen(GetNativeHandle(), backlog);
				if (result != SRT_ERROR)
				{
					SetState(SocketState::Listening);
					return true;
				}

				logae("Could not listen: %s", srt_getlasterror_str());
				break;
			}

			default:
				OV_ASSERT(false, "Invalid socket type: %d", ToUnderlyingType(GetType()));
				break;
		}

		return false;
	}

	SocketWrapper Socket::Accept(SocketAddress *client)
	{
		CHECK_STATE(<= SocketState::Listening, SocketWrapper());

		IoGuard io_guard(*this);

		if (io_guard.IsAcquired() == false)
		{
			return SocketWrapper();
		}

		switch (GetType())
		{
			case SocketType::Tcp: {
				while (true)
				{
					sockaddr_storage client_addr{};
					socklen_t client_length = sizeof(client_addr);
					auto client_socket		= ::accept(GetNativeHandle(), reinterpret_cast<sockaddr *>(&client_addr), &client_length);

					if (client_socket == InvalidSocket)
					{
						if (IsOpen() == false)
						{
							// Woken by a close on another thread
							return SocketWrapper();
						}

						auto error = ov::Error::CreateErrorFromErrno();
						auto code  = error->GetCode();
						code	   = (code == EWOULDBLOCK) ? EAGAIN : code;

						switch (code)
						{
							case EINTR:
								logaw("Accept was interrupted by a signal, continue to accept other clients");
								continue;

							case EAGAIN:
								logad("No more clients to accept");
								break;

							case ENFILE:
								[[fallthrough]];
							case EMFILE:
								logac("The process or the system has reached the limit of open file descriptors, cannot accept new clients until some sockets are closed");
								_has_pending_events = true;
								_worker->EnqueueToDispatchLater(GetSharedPtr());
								break;

							case ENOMEM:
								[[fallthrough]];
							case ENOBUFS:
								logac("Not enough memory to accept new clients, cannot accept new clients until some sockets are closed and memory is freed");
								_has_pending_events = true;
								_worker->EnqueueToDispatchLater(GetSharedPtr());
								break;

							case ECONNABORTED:
								logaw("A connection has been aborted while waiting to be accepted");
								continue;

							default:
								logae("Failed to accept a client socket: %s", error->What());
								break;
						}
					}
					else
					{
						*client = SocketAddress("", client_addr);
					}

					return SocketWrapper(GetType(), client_socket);
				}
			}

			case SocketType::Srt: {
				sockaddr_storage client_addr{};
				int client_length  = sizeof(client_addr);
				auto client_socket = ::srt_accept(GetNativeHandle(), reinterpret_cast<sockaddr *>(&client_addr), &client_length);

				if (client_socket == SRT_INVALID_SOCK)
				{
					if (IsOpen() == false)
					{
						// Failed by a close on another thread
						return SocketWrapper();
					}

					auto srt_error = ov::SrtError::CreateErrorFromSrt();

					switch (srt_error->GetCode())
					{
						case SRT_ESCLOSED:
							// This can happen when the client socket is closed while accepting,
							// so we can just ignore this error and continue to accept other clients.
							logad("Client socket is closed while accepting");
							_has_pending_events = true;
							_worker->EnqueueToDispatchLater(GetSharedPtr());
							break;

						case SRT_EASYNCRCV:
							logad("No more clients to accept");
							break;

						default:
							// Unknown error occurred while accepting
							logae("Failed to accept a client socket: %s", srt_error->GetMessage().CStr());
							_has_pending_events = true;
							_worker->EnqueueToDispatchLater(GetSharedPtr());
							break;
					}
				}
				else
				{
					*client = SocketAddress("", client_addr);
				}

				return SocketWrapper(GetType(), client_socket);
			}

			default:
				OV_ASSERT(false, "Invalid socket type: %d", ToUnderlyingType(GetType()));
				break;
		}

		return SocketWrapper();
	}

	std::shared_ptr<const SocketError> Socket::DoConnectionCallback(const std::shared_ptr<const SocketError> &error)
	{
		if (_blocking_mode == BlockingMode::Blocking)
		{
			if (error == nullptr)
			{
				// A close sets its state under this lock, so a connect that finishes during the close cannot overwrite it
				LockGuard lock(_dispatch_queue_lock);

				if (IsOpen() == false)
				{
					return SocketError::CreateError("Socket is closed");
				}

				SetState(SocketState::Connected);
			}
			else
			{
				CloseWithState(SocketState::Error);
			}

			return error;
		}

		OnConnectedEvent(error);

		return nullptr;
	}

	std::shared_ptr<const SocketError> Socket::Connect(const SocketAddress &endpoint, int timeout_msec)
	{
		CHECK_STATE(== SocketState::Created, DoConnectionCallback(SocketError::CreateError(EINVAL, "Invalid state: %d", ToUnderlyingType(_state.load()))));

		if (endpoint.IsValid() == false)
		{
			return DoConnectionCallback(SocketError::CreateError("Invalid address: %s", endpoint.ToString().CStr()));
		}

		std::shared_ptr<const SocketError> socket_error;

		// To set connection timeout
		bool use_timeout = (timeout_msec > 0 && timeout_msec < Infinite);

		switch (GetType())
		{
			case SocketType::Tcp:
			case SocketType::Udp: {
				if (_blocking_mode == BlockingMode::Blocking)
				{
					if (use_timeout)
					{
						if (
							(SetBlockingInternal(BlockingMode::NonBlocking) == false) ||
							(AddToWorker(true) == false))
						{
							return DoConnectionCallback(SocketError::CreateError("Socket cannot be changed to nonblocking mode: %s", ToString().CStr()));
						}
					}
				}

				bool is_closed = false;

				{
					// A close sets its state under this lock, so this cannot overwrite it
					LockGuard lock(_dispatch_queue_lock);

					is_closed = (IsOpen() == false);

					if (is_closed == false)
					{
						SetState(SocketState::Connecting);
					}
				}

				if (is_closed)
				{
					return DoConnectionCallback(SocketError::CreateError("Socket is closed"));
				}

				logat("Trying to connect to %s...", endpoint.ToString().CStr());
				int result = -1;
				std::shared_ptr<Error> error;

				{
					IoGuard io_guard(*this);

					if (io_guard.IsAcquired() == false)
					{
						return DoConnectionCallback(SocketError::CreateError("Socket is closed"));
					}

					result = ::connect(GetNativeHandle(), endpoint, endpoint.GetSockAddrInLength());

					if (result < 0)
					{
						error = Error::CreateErrorFromErrno();
					}
				}

				if (IsOpen() == false)
				{
					// A shutdown may not abort a blocking connect (some kernels refuse it with `EBUSY`),
					// so a close during it keeps the handle open until the connect returns and is reported here
					return DoConnectionCallback(SocketError::CreateError("Socket is closed"));
				}

				if (result == 0)
				{
					if ((_blocking_mode == BlockingMode::Blocking) && use_timeout)
					{
						// Restore blocking state
						DeleteFromWorker();

						if (SetBlockingInternal(BlockingMode::Blocking) == false)
						{
							return DoConnectionCallback(SocketError::CreateError("Socket cannot be changed to blocking mode: %s", ToString().CStr()));
						}
					}

					return DoConnectionCallback(nullptr);
				}

				if (result < 0)
				{
					// Check timeout
					if (error->GetCode() == EINPROGRESS)
					{
						if (_blocking_mode == BlockingMode::NonBlocking)
						{
							// Don't wait for the connection if this socket is non-blocking mode
							if (use_timeout)
							{
								_worker->EnqueueToCheckConnectionTimeOut(GetSharedPtr(), timeout_msec);
							}
							return nullptr;
						}

						// Blocking mode
						socket_error = WaitForConnectResult(timeout_msec);

						// Restore blocking state
						DeleteFromWorker();

						if (SetBlockingInternal(BlockingMode::Blocking) == false)
						{
							socket_error = (socket_error == nullptr) ? SocketError::CreateError("Socket cannot be changed to blocking mode: %s", ToString().CStr()) : socket_error;
						}
					}
					else
					{
						socket_error = SocketError::CreateError(error);
					}
				}

				break;
			}

			case SocketType::Srt:
				if (SetSockOpt(SRTO_CONNTIMEO, timeout_msec) &&
					(::srt_connect(GetNativeHandle(), endpoint, endpoint.GetSockAddrInLength()) != SRT_ERROR))
				{
					socket_error = nullptr;
				}
				else if (IsOpen() == false)
				{
					socket_error = SocketError::CreateError("Socket is closed");
				}
				else
				{
					socket_error = SocketError::CreateError(SrtError::CreateErrorFromSrt());
				}

				break;

			default:
				socket_error = SocketError::CreateError("Not implemented");
				break;
		}

		return DoConnectionCallback(socket_error);
	}

	bool Socket::SetRecvTimeout(const timeval &tv)
	{
		//CHECK_STATE(== SocketState::Connected, false);

		switch (GetType())
		{
			case SocketType::Tcp:
			case SocketType::Udp:
				return SetSockOpt(SO_RCVTIMEO, tv);

			case SocketType::Srt:
			default:
				OV_ASSERT(false, "Not implemented");
				return false;
		}
	}

	std::shared_ptr<SocketAddress> Socket::GetLocalAddress() const
	{
		return _local_address;
	}

	std::shared_ptr<SocketAddress> Socket::GetRemoteAddress() const
	{
		return _remote_address;
	}

	String Socket::GetRemoteAddressAsUrl() const
	{
		return String::FormatString("%s://%s", StringFromSocketType(GetType()), GetRemoteAddress() != nullptr ? GetRemoteAddress()->ToString(false).CStr() : "unknown");
	}

	bool Socket::SetSockOpt(int proto, int option, const void *value, socklen_t value_length)
	{
		CHECK_STATE(!= SocketState::Closed, false);

		IoGuard io_guard(*this);

		if (io_guard.IsAcquired() == false)
		{
			return false;
		}

		int result = ::setsockopt(GetNativeHandle(), proto, option, value, value_length);

		if (result != 0)
		{
			const auto error = Error::CreateErrorFromErrno();
			logaw("Could not set option: %d (proto: %d) (result: %d, %s)", option, proto, result, error->What());
			return false;
		}

		return true;
	}

	bool Socket::SetSockOpt(int option, const void *value, socklen_t value_length)
	{
		return SetSockOpt(SOL_SOCKET, option, value, value_length);
	}

	bool Socket::GetSockOpt(int proto, int option, void *value, socklen_t value_length) const
	{
		CHECK_STATE(!= SocketState::Closed, false);

		IoGuard io_guard(*this);

		if (io_guard.IsAcquired() == false)
		{
			return false;
		}

		int result = ::getsockopt(GetNativeHandle(), proto, option, value, &value_length);

		if (result != 0)
		{
			logaw("Could not get option: %d (result: %d)", option, result);
			return false;
		}

		return true;
	}

	bool Socket::SetSockOpt(SRT_SOCKOPT option, const void *value, int value_length)
	{
		CHECK_STATE(!= SocketState::Closed, false);

		IoGuard io_guard(*this);

		if (io_guard.IsAcquired() == false)
		{
			return false;
		}

		int result = ::srt_setsockopt(GetNativeHandle(), 0, option, value, value_length);

		if (result == SRT_ERROR)
		{
			auto error = SrtError::CreateErrorFromSrt();
			logaw("Could not set option: %d (result: %s)", option, error->What());
			return false;
		}

		return true;
	}

	bool Socket::IsClosable() const
	{
		return CheckFlag(GetState(), SOCKET_STATE_CLOSABLE);
	}

	SocketState Socket::GetState() const
	{
		return _state.load();
	}

	void Socket::SetState(SocketState state)
	{
		auto old_state = _state.load();
		_state = state;

		logat("Socket state is changed: %s => %s",
			  StringFromSocketState(old_state),
			  StringFromSocketState(state));
	}

	SocketType Socket::GetType() const
	{
		return _socket.GetType();
	}

	// Only Available if socket is SRT
	String Socket::GetStreamId() const
	{
		return _stream_id;
	}

	bool Socket::OnConnectedEvent(const std::shared_ptr<const SocketError> &error)
	{
		if (error == nullptr)
		{
			std::shared_ptr<SocketAsyncInterface> callback;

			{
				// A close sets its state and takes the callback under this lock.
				// So a connect that completes during the close is not reported, and one reported here gets its close callback.
				LockGuard lock(_dispatch_queue_lock);

				if (IsOpen() == false)
				{
					return true;
				}

				SetState(SocketState::Connected);

				callback = std::atomic_load(&_callback);

				if (callback != nullptr)
				{
					_connection_event_fired = true;
				}
			}

			if (callback != nullptr)
			{
				callback->OnConnected(error);
			}
		}
		else
		{
			auto callback = std::atomic_exchange(&_callback, {});

			CloseWithState(SocketState::Error);

			if (callback != nullptr)
			{
				_connection_event_fired = true;
				callback->OnConnected(error);
			}
		}

		return true;
	}

	Socket::DispatchResult Socket::DispatchEventInternal(DispatchCommand &command)
	{
		SOCKET_PROFILER_INIT();
		SOCKET_PROFILER_POST_HANDLER([&](int64_t lock_elapsed, int64_t total_elapsed) {
			if (total_elapsed > 100)
			{
				logtw("[SockProfiler] DispatchEventInternal() - %s, Total: %dms", ToString().CStr(), total_elapsed);
			}
		});

		ssize_t sent_bytes = 0;
		auto &data		   = command.data;

		logap("Dispatching event: %s", command.ToString().CStr());

		switch (command.type)
		{
			case DispatchCommand::Type::Connected:
				if (OnConnectedEvent(nullptr))
				{
					return DispatchResult::Dispatched;
				}

				return DispatchResult::Error;

			case DispatchCommand::Type::Send:
				sent_bytes = SendInternal(data);
				break;

			case DispatchCommand::Type::SendTo:
				sent_bytes = SendToInternal(command.address, data);
				break;

			case DispatchCommand::Type::SendFromTo:
				sent_bytes = SendFromToInternal(command.address_pair, data);
				break;

			case DispatchCommand::Type::HalfClose:
				return HalfClose();

			case DispatchCommand::Type::WaitForHalfClose:
				return WaitForHalfClose();

			case DispatchCommand::Type::Close: {
				logat("Trying to close the socket...");

				CloseInternal(command.new_state);

				SetState(command.new_state);
				return DispatchResult::Dispatched;
			}
		}

		if (sent_bytes == static_cast<ssize_t>(command.data->GetLength()))
		{
			return DispatchResult::Dispatched;
		}

		if (sent_bytes == -1)
		{
			// On a UDP socket a send error concerns one datagram: drop it and go on to the next command
			return (GetType() == SocketType::Udp) ? DispatchResult::Dispatched : DispatchResult::Error;
		}

		if (sent_bytes > 0)
		{
			// Since some data has been sent, the time needs to be updated.
			command.UpdateTime();
			data = data->Subdata(sent_bytes);

			logat("Part of the data has been sent: %ld bytes, left: %ld bytes (%s)", sent_bytes, data->GetLength(), command.ToString().CStr());
		}
		else
		{
			// logat("Could not send data: %ld bytes (%s)", data->GetLength(), command.ToString().CStr());
		}

#ifdef OME_LATENCY_PROBE
		if (command.type == DispatchCommand::Type::Send)
		{
			SendStallLog(GetNativeHandle(), static_cast<long>(sent_bytes), static_cast<long>(data->GetLength()));
		}
#endif	// OME_LATENCY_PROBE

		return DispatchResult::PartialDispatched;
	}

	Socket::DispatchResult Socket::DispatchEventsInternal()
	{
		SOCKET_PROFILER_INIT();

		DispatchResult result = DispatchResult::Dispatched;

		{
			SOCKET_PROFILER_AFTER_LOCK();

			LockGuard lock_guard(_dispatch_queue_lock);

			[[maybe_unused]] auto count = _dispatch_queue.size();
			SOCKET_PROFILER_POST_HANDLER([&](int64_t lock_elapsed, int64_t total_elapsed) {
				if ((lock_elapsed > 100) || (count > 10) || (_dispatch_queue.size() > 10))
				{
					logtw("[SockProfiler] DispatchEventsInternal() - %s, Before Queue: %zu, After Queue: %zu, Lock: %dms, Total: %dms",
						  ToString().CStr(), count, _dispatch_queue.size(), lock_elapsed, total_elapsed);
				}
			});

			if (_dispatch_queue.empty() == false)
			{
				logap("Dispatching events (count: %zu)...", _dispatch_queue.size());

				while (_dispatch_queue.empty() == false)
				{
					auto front = std::move(_dispatch_queue.front());
					_dispatch_queue.pop_front();

					bool is_close_command = front.IsCloseCommand();

					if ((IsOpen() == false) && (is_close_command == false))
					{
						// If the socket is closed during dispatching, the rest of the data will not be sent.
						logat("Some commands have not been dispatched: %zu commands", _dispatch_queue.size());
#if DEBUG
						for (auto &queue : _dispatch_queue)
						{
							logat("  - Command: %s", queue.ToString().CStr());
						}
#endif	// DEBUG

						_dispatch_queue.clear();

						result = DispatchResult::Dispatched;
						break;
					}

					result = DispatchEventInternal(front);

					if (result == DispatchResult::Dispatched)
					{
						// Dispatches the next item
						continue;
					}
					else if (result == DispatchResult::PartialDispatched)
					{
						// The data is not fully processed and will not be removed from queue

						_dispatch_queue.emplace_front(std::move(front));

						// Close-related commands will be processed when we receive the event from epoll later
					}
					else
					{
						// An error occurred
						if (is_close_command)
						{
							// Ignore errors that occurred during close
							result = DispatchResult::Dispatched;
							continue;
						}
					}

					break;
				}
			}
		}

		return result;
	}

	Socket::DispatchResult Socket::DispatchEvents()
	{
		switch (_blocking_mode)
		{
			case BlockingMode::Blocking:
#if DEBUG
			{
				LockGuard lock_guard(_dispatch_queue_lock);
				[[maybe_unused]] auto count = _dispatch_queue.size();
				OV_ASSERT2(count == 0);
			}
#endif	// DEBUG
				return DispatchResult::Dispatched;

			case BlockingMode::NonBlocking: {
				// Due to the connection callback point, the DispatchEventsInternal() specifically performs mutex.lock inside.
				// LockGuard lock_guard(_dispatch_queue_lock);
				auto result = DispatchEventsInternal();

				CallCloseCallbackIfNeeded();

				return result;
			}
		}

		return DispatchResult::Error;
	}

	PostProcessMethod Socket::OnDataWritableEvent()
	{
		switch (DispatchEvents())
		{
			case DispatchResult::Dispatched:
				return PostProcessMethod::Nothing;

			case DispatchResult::PartialDispatched:
				return PostProcessMethod::GarbageCollection;

			case DispatchResult::Error:
				break;
		}

		return PostProcessMethod::Error;
	}

	void Socket::OnDataAvailableEvent()
	{
		logat("Socket is ready to read");

		_has_pending_events = false;

		auto callback = std::atomic_load(&_callback);
		if (callback != nullptr)
		{
			callback->OnReadable();
		}
	}

	bool Socket::IsSendable() const
	{
		switch (GetType())
		{
			case SocketType::Unknown:
				return false;

			case SocketType::Tcp:
				[[fallthrough]];
			case SocketType::Srt:
				CHECK_STATE(== SocketState::Connected, false);
				break;

			case SocketType::Udp:
				CHECK_STATE2(== SocketState::Created, == SocketState::Bound, false);
				break;
		}

		return IsOpen() && (HasCloseCommand() == false);
	}

	bool Socket::HasConnectCompleted() const
	{
		IoGuard io_guard(*this);

		if (io_guard.IsAcquired() == false)
		{
			return false;
		}

		pollfd poll_fd{GetNativeHandle(), POLLOUT, 0};

		return (::poll(&poll_fd, 1, 0) > 0) && ((poll_fd.revents & (POLLOUT | POLLERR | POLLHUP)) != 0);
	}

	void Socket::WakeConnectWaiter()
	{
		_connect_event.SetEvent();
	}

	std::shared_ptr<const SocketError> Socket::WaitForConnectResult(int timeout_msec)
	{
		std::shared_ptr<const SocketError> socket_error;

		// Woken once the worker sees the connect complete, or by a close
		_connect_event.Wait(timeout_msec);

		{
			// A close also takes this lock, so the socket stays open while this thread decides the result
			LockGuard lock(_dispatch_queue_lock);

			if (IsOpen() == false)
			{
				socket_error = SocketError::CreateError("Socket is closed");
			}
			else
			{
				// `SO_ERROR` is read after the completion check, so a connect failing in between is reported as a timeout
				const bool completed = HasConnectCompleted();
				int so_error		 = 0;

				if (GetSockOpt(SO_ERROR, &so_error) == false)
				{
					socket_error = SocketError::CreateError("Unknown error occurred: could not read SO_ERROR");
				}
				else if (completed == false)
				{
					socket_error = SocketError::CreateError(so_error, "Connection timed out (%d ms elapsed, SO_ERROR: %d)", timeout_msec, so_error);
				}
				else if (so_error != 0)
				{
					socket_error = SocketError::CreateError(so_error, "Socket error occurred: %s", ::strerror(so_error));
				}
			}
		}

		if (socket_error != nullptr)
		{
			logat("%s", socket_error->What());
		}

		return socket_error;
	}

	ssize_t Socket::HandleSendError(const size_t total_sent)
	{
		const auto error = Error::CreateErrorFromErrno();

		switch (error->GetCode())
		{
			case EAGAIN:
				// Socket buffer is full - retry later
				STATS_COUNTER_INCREASE_RETRY();
				return total_sent;

			case EPIPE:
				// Broken pipe - maybe peer is disconnected
				[[fallthrough]];
			case ECONNRESET:
				// Connection reset - maybe peer is disconnected
				break;

			default:
				logaw("Could not send data: %s", error->What());
				break;
		}

		STATS_COUNTER_INCREASE_ERROR();
		return -1L;
	}

	ssize_t Socket::SendData(const std::shared_ptr<const Data> &data)
	{
		auto data_to_send		= data->GetDataAs<uint8_t>();
		size_t remaining_bytes	= data->GetLength();
		size_t total_sent_bytes = 0L;

		logap("Trying to send data %zu bytes...", remaining_bytes);

		IoGuard io_guard(*this);

		if (io_guard.IsAcquired() == false)
		{
			return -1L;
		}

		while ((remaining_bytes > 0L) && (_force_stop == false))
		{
			const auto sent = ::send(GetNativeHandle(), data_to_send, remaining_bytes, MSG_NOSIGNAL | MSG_DONTWAIT);

			if (sent < 0L)
			{
				return HandleSendError(total_sent_bytes);
			}

			OV_ASSERT2(static_cast<ssize_t>(remaining_bytes) >= sent);

			STATS_COUNTER_INCREASE_PPS();

			data_to_send += sent;
			remaining_bytes -= sent;
			total_sent_bytes += sent;

			UpdateLastSentTime();
		}

		logap("%zu bytes sent", total_sent_bytes);
		return total_sent_bytes;
	}

	ssize_t Socket::SendSrtData(
		const std::shared_ptr<const Data> &data)
	{
		auto data_to_send		= data->GetDataAs<char>();
		size_t remaining_bytes	= data->GetLength();
		size_t total_sent_bytes = 0L;

		logap("Trying to send data %zu bytes...", remaining_bytes);

		IoGuard io_guard(*this);

		if (io_guard.IsAcquired() == false)
		{
			return -1L;
		}

		while ((remaining_bytes > 0L) && (_force_stop == false))
		{
			// SRT limits packet size up to 1316
			const auto bytes_to_send = std::min(1316UL, remaining_bytes);

			const auto sent			 = ::srt_sendmsg(GetNativeHandle(), data_to_send, bytes_to_send, -1, 1);

			if (sent == SRT_ERROR)
			{
				const auto error = SrtError::CreateErrorFromSrt();

				if (error->GetCode() == SRT_EASYNCSND)
				{
					// Socket buffer is full - retry later
					STATS_COUNTER_INCREASE_RETRY();
					return total_sent_bytes;
				}

				STATS_COUNTER_INCREASE_ERROR();
				logaw("Could not send data: %d (%s)", sent, SrtError::CreateErrorFromSrt()->What());
				return sent;
			}

			OV_ASSERT2(static_cast<ssize_t>(remaining_bytes) >= sent);

			STATS_COUNTER_INCREASE_PPS();

			data_to_send += sent;
			remaining_bytes -= sent;
			total_sent_bytes += sent;

			UpdateLastSentTime();
		}

		logap("%zu bytes sent", total_sent_bytes);
		return total_sent_bytes;
	}

	ssize_t Socket::SendInternal(const std::shared_ptr<const Data> &data)
	{
		switch (GetType())
		{
			case SocketType::Udp:
			case SocketType::Tcp:
				return SendData(data);

			case SocketType::Srt:
				return SendSrtData(data);

			case SocketType::Unknown:
				logac("Could not send data - unknown socket type");
				OV_ASSERT2(false);
				break;
		}

		return -1L;
	}

	template <typename SendFunc, typename MakeCommand>
	bool Socket::SendInlineOrAppend(const std::shared_ptr<const Data> &data, SendFunc &&send_func, MakeCommand &&make_command)
	{
		{
			LockGuard lock_guard(_dispatch_queue_lock);

			// Only bypass the queue when nothing is waiting ahead of us, so packets are never reordered
			if (_dispatch_queue.empty() && (GetState() != SocketState::Closed))
			{
				const ssize_t sent_bytes = send_func();

				if (sent_bytes != static_cast<ssize_t>(data->GetLength()) && (sent_bytes != -1))
				{
					// Subdata() is a separate copy-on-write Data even at offset 0, so the caller may reuse its buffer
					auto command = make_command(data->Subdata(sent_bytes));

#ifdef OME_LATENCY_PROBE
					if (command.type == DispatchCommand::Type::Send)
					{
						SendStallLog(GetNativeHandle(), static_cast<long>(sent_bytes), static_cast<long>(command.data->GetLength()));
					}
#endif	// OME_LATENCY_PROBE

					_dispatch_queue.push_back(std::move(command));
					_worker->EnqueueToDispatchLater(GetSharedPtr());
				}

				CallCloseCallbackIfNeeded();
				return true;
			}
		}

		return AppendCommand(make_command(data->Clone()), true);
	}

	bool Socket::Send(const std::shared_ptr<const Data> &data)
	{
		if (data == nullptr)
		{
			OV_ASSERT2(data != nullptr);
			return false;
		}

		switch (_blocking_mode)
		{
			case BlockingMode::Blocking:
				return (SendInternal(data) == static_cast<ssize_t>(data->GetLength()));

			case BlockingMode::NonBlocking:
				if (IsSendable())
				{
					return SendInlineOrAppend(
						data,
						[&] { return SendInternal(data); },
						[](std::shared_ptr<const Data> d) { return DispatchCommand(std::move(d)); });
				}
				break;
		}

		return false;
	}

	bool Socket::Send(const void *data, size_t length)
	{
		return Send((data == nullptr) ? nullptr : std::make_shared<Data>(data, length));
	}

	ssize_t Socket::SendToInternal(const SocketAddress &address, const std::shared_ptr<const Data> &data)
	{
		if (GetType() != SocketType::Udp)
		{
			// Does not support SendTo() for TCP/SRT
			logac("Could not send data - Invalid socket type: %s", StringFromSocketType(GetType()));
			OV_ASSERT2(false);
			return -1L;
		}

		auto data_to_send		= data->GetDataAs<uint8_t>();
		size_t remaining_bytes	= data->GetLength();
		size_t total_sent_bytes = 0L;

		logap("Trying to send data %zu bytes to %s...", remaining_bytes, address.ToString(false).CStr());

		IoGuard io_guard(*this);

		if (io_guard.IsAcquired() == false)
		{
			return -1L;
		}

		while ((remaining_bytes > 0L) && (_force_stop == false))
		{
			const ssize_t sent = ::sendto(GetNativeHandle(), data_to_send, remaining_bytes, MSG_NOSIGNAL | MSG_DONTWAIT, address, address.GetSockAddrInLength());

			if (sent < 0L)
			{
				return HandleSendError(total_sent_bytes);
			}

			OV_ASSERT2(static_cast<ssize_t>(remaining_bytes) >= sent);

			STATS_COUNTER_INCREASE_PPS();

			data_to_send += sent;
			remaining_bytes -= sent;
			total_sent_bytes += sent;

			UpdateLastSentTime();
		}

		logap("%zu bytes sent", total_sent_bytes);
		return total_sent_bytes;
	}

	bool Socket::SendTo(const SocketAddress &address, const std::shared_ptr<const Data> &data)
	{
		if (data == nullptr)
		{
			OV_ASSERT2(data != nullptr);
			return false;
		}

		switch (_blocking_mode)
		{
			case BlockingMode::Blocking:
				return (SendToInternal(address, data) == static_cast<ssize_t>(data->GetLength()));

			case BlockingMode::NonBlocking:
				if (IsSendable())
				{
					if (GetType() == SocketType::Udp)
					{
						return SendInlineOrAppend(
							data,
							[&] { return SendToInternal(address, data); },
							[&address](std::shared_ptr<const Data> d) { return DispatchCommand(address, std::move(d)); });
					}
					return Send(data);
				}
				break;
		}

		return false;
	}

	bool Socket::SendTo(const SocketAddress &address, const void *data, size_t length)
	{
		return SendTo(address, (data == nullptr) ? nullptr : std::make_shared<Data>(data, length));
	}

	void SetAddr(in_pktinfo *pktinfo, const SocketAddress &address)
	{
		pktinfo->ipi_spec_dst.s_addr = address.ToIn4Addr()->s_addr;
	}

	void SetAddr(in6_pktinfo *pktinfo, const SocketAddress &address)
	{
		::memcpy(&pktinfo->ipi6_addr, address.ToIn6Addr(), sizeof(in6_addr));
	}

	template <typename Tpktinfo>
	bool SendFromToInternal(
		const int socket_handle,
		const int msg_level, const int msg_type,
		const SocketAddress &local_address, const SocketAddress &remote_address,
		const void *data, const size_t length,
		size_t *total_sent_bytes,
		const std::atomic<bool> &force_stop)
	{
		OV_ASSERT2(total_sent_bytes != nullptr);
		*total_sent_bytes	   = 0L;

		auto data_to_send	   = static_cast<const uint8_t *>(data);
		size_t remaining_bytes = length;

		char control[CMSG_SPACE(sizeof(Tpktinfo))]{};

		iovec iov{};
		// This is intentional conversion
		iov.iov_base = const_cast<uint8_t *>(data_to_send);
		iov.iov_len	 = remaining_bytes;

		Tpktinfo pktinfo{};
		SetAddr(&pktinfo, local_address);

		auto cmsg		 = reinterpret_cast<cmsghdr *>(control);
		cmsg->cmsg_level = msg_level;
		cmsg->cmsg_type	 = msg_type;
		cmsg->cmsg_len	 = CMSG_LEN(sizeof(pktinfo));
		::memcpy(CMSG_DATA(cmsg), &pktinfo, sizeof(pktinfo));

		msghdr msg{};
		// This is intentional conversion
		msg.msg_name	   = const_cast<sockaddr *>(remote_address.ToSockAddr());
		msg.msg_namelen	   = remote_address.GetSockAddrInLength();
		msg.msg_iov		   = &iov;
		msg.msg_iovlen	   = 1;
		msg.msg_control	   = control;
		msg.msg_controllen = sizeof(control);

		while ((remaining_bytes > 0L) && (force_stop == false))
		{
			const auto sent = ::sendmsg(socket_handle, &msg, MSG_NOSIGNAL | MSG_DONTWAIT);

			if (sent < 0L)
			{
				return false;
			}

			OV_ASSERT2(static_cast<ssize_t>(remaining_bytes) >= sent);

			STATS_COUNTER_INCREASE_PPS();

			data_to_send += sent;
			remaining_bytes -= sent;
			*total_sent_bytes += sent;
		}

		logtp("[#%d] %zu bytes sent", socket_handle, *total_sent_bytes);

		return true;
	}

	ssize_t Socket::SendFromToInternal(const SocketAddressPair &address_pair, const std::shared_ptr<const Data> &data)
	{
		if (GetType() != SocketType::Udp)
		{
			// Does not support SendFromTo() for TCP/SRT
			logac("Could not send(from/to) data - Invalid socket type: %s", StringFromSocketType(GetType()));
			OV_ASSERT2(false);
			return -1L;
		}

		const auto &local_address  = address_pair.GetLocalAddress();
		const auto &remote_address = address_pair.GetRemoteAddress();

		const auto data_length	  = data->GetLength();

		logap("Trying to send data %zu bytes to %s from %s...",
			  data_length,
			  remote_address.ToString().CStr(), local_address.ToString().CStr());

		size_t total_sent_bytes = 0;
		bool sent				= false;

		IoGuard io_guard(*this);

		if (io_guard.IsAcquired() == false)
		{
			return -1L;
		}

		switch (_family)
		{
			case SocketFamily::Unknown:
				OV_ASSERT2(false);
				return -1L;

			case SocketFamily::Inet:
				sent = ov::SendFromToInternal<in_pktinfo>(
					GetNativeHandle(),
					IPPROTO_IP, IP_PKTINFO,
					local_address, remote_address,
					data->GetData(), data_length,
					&total_sent_bytes,
					_force_stop);
				break;

			case SocketFamily::Inet6:
				sent = ov::SendFromToInternal<in6_pktinfo>(
					GetNativeHandle(),
					IPPROTO_IPV6, IPV6_PKTINFO,
					local_address, remote_address,
					data->GetData(), data_length,
					&total_sent_bytes,
					_force_stop);
				break;
		}

		if (total_sent_bytes > 0L)
		{
			UpdateLastSentTime();
		}

		if (sent == false)
		{
			return HandleSendError(total_sent_bytes);
		}

		return total_sent_bytes;
	}

	bool Socket::SendFromTo(const SocketAddressPair &address_pair, const std::shared_ptr<const Data> &data)
	{
		if (IsSendable() == false)
		{
			return false;
		}

		if (data == nullptr)
		{
			OV_ASSERT2(data != nullptr);
			return false;
		}

		switch (_blocking_mode)
		{
			case BlockingMode::Blocking:
				return (SendFromToInternal(address_pair, data) == static_cast<ssize_t>(data->GetLength()));

			case BlockingMode::NonBlocking:
				if (IsSendable())
				{
					if (GetType() == SocketType::Udp)
					{
						return SendInlineOrAppend(
							data,
							[&] { return SendFromToInternal(address_pair, data); },
							[&address_pair](std::shared_ptr<const Data> d) { return DispatchCommand(address_pair, std::move(d)); });
					}
					return Send(data);
				}
		}

		return false;
	}

	bool Socket::SendFromTo(const SocketAddressPair &address_pair, const void *data, size_t length)
	{
		return SendFromTo(address_pair, (data == nullptr) ? nullptr : std::make_shared<Data>(data, length));
	}

	std::shared_ptr<const SocketError> Socket::Recv(std::shared_ptr<Data> &data, const bool non_block)
	{
		OV_ASSERT2(data != nullptr);
		OV_ASSERT(data->GetCapacity() > 0, "Must specify a data size in advance using Reserve().");

		size_t capacity = data->GetCapacity();

		// Set the length of the data to the capacity to avoid memory reallocation
		data->SetLength(capacity);

		auto result = Recv(data->GetWritableData(), capacity, non_block);

		if (result.has_value() == false)
		{
			return result.error();
		}

		data->SetLength(result.value());

		return nullptr;
	}

	tl::expected<size_t, std::shared_ptr<const SocketError>> Socket::Recv(void *data, size_t length, const bool non_block)
	{
		if (GetState() == SocketState::Closed)
		{
			return tl::make_unexpected(SocketError::CreateError("Socket is closed"));
		}

		OV_ASSERT2(data != nullptr);

		logap("Trying to read from the socket...");

		ssize_t read_bytes = -1;
		std::shared_ptr<SocketError> socket_error;
		size_t received_length = 0;

		switch (GetType())
		{
			case SocketType::Unknown:
				// Must have a concrete type before `Recv()`; reaching here is a bug.
				OV_ASSERT2(false);
				return tl::make_unexpected(SocketError::CreateError("Cannot receive data from a socket of unknown type"));

			case SocketType::Udp:
			case SocketType::Tcp:
			{
				IoGuard io_guard(*this);

				if (io_guard.IsAcquired() == false)
				{
					return tl::make_unexpected(SocketError::CreateError("Socket is closed"));
				}

				const int recv_flags = ((_blocking_mode == BlockingMode::NonBlocking) || non_block) ? MSG_DONTWAIT : 0;
#ifdef OME_LATENCY_PROBE
				// Latency probe (OME_LATENCY_PROBE only): use recvmsg to pull the SO_TIMESTAMPING
				// software RX timestamp, so the serving layer can compare kernel-arrival vs serve time.
				struct iovec iov;
				iov.iov_base = data;
				iov.iov_len	 = length;
				// Control buffer must be aligned for struct cmsghdr access via the CMSG_* macros.
				alignas(struct cmsghdr) char ts_cbuf[256];
				struct msghdr msg = {};
				msg.msg_iov		   = &iov;
				msg.msg_iovlen	   = 1;
				msg.msg_control	   = ts_cbuf;
				msg.msg_controllen = sizeof(ts_cbuf);

				read_bytes = ::recvmsg(GetNativeHandle(), &msg, recv_flags);

				if (read_bytes > 0L && GetType() == SocketType::Tcp)
				{
					// Clear first so a read without a timestamp cmsg reports 0 (unknown) to the
					// serving layer, instead of reusing the previous read's kernel arrival time.
					_last_rx_kernel_ms.store(0, std::memory_order_relaxed);

					for (struct cmsghdr *cm = CMSG_FIRSTHDR(&msg); cm != nullptr; cm = CMSG_NXTHDR(&msg, cm))
					{
						if (cm->cmsg_level == SOL_SOCKET && cm->cmsg_type == SCM_TIMESTAMPING &&
							cm->cmsg_len >= CMSG_LEN(sizeof(struct timespec)))
						{
							// scm_timestamping layout: struct timespec ts[3]; ts[0]=software RX time
							auto *ts = reinterpret_cast<struct timespec *>(CMSG_DATA(cm));
							int64_t kernel_ms = static_cast<int64_t>(ts[0].tv_sec) * 1000 + ts[0].tv_nsec / 1000000;
							if (kernel_ms > 0)
							{
								// Store for the serving layer to read (per-request K timestamp).
								_last_rx_kernel_ms.store(kernel_ms, std::memory_order_relaxed);

								auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
								int64_t late = now_ms - kernel_ms;
								if (late > 15)
								{
									RecvLateLog(GetNativeHandle(), static_cast<long long>(late), static_cast<long>(read_bytes));
								}
							}
							break;
						}
					}
				}
#else	// OME_LATENCY_PROBE
				read_bytes = ::recv(GetNativeHandle(), data, length, recv_flags);
#endif	// OME_LATENCY_PROBE

				if (read_bytes == 0L)
				{
					if (GetType() == SocketType::Tcp)
					{
						// `0` means orderly shutdown (EOF). `errno` is not set, so classify by the return value.
						socket_error = SocketError::CreateError("Remote is disconnected");
					}
					else
					{
						// `0` is a valid empty datagram (no EOF), so treat it as a successful 0-byte read.
					}
				}
				else if (read_bytes < 0L)
				{
					auto error = Error::CreateErrorFromErrno();
					auto code  = error->GetCode();

					// Some platforms report the would-block condition as `EWOULDBLOCK`;
					// normalize it to `EAGAIN` (mirrors `Accept()`).
					if (code == EWOULDBLOCK)
					{
						code = EAGAIN;
					}

					if (code == EAGAIN)
					{
						if ((_blocking_mode == BlockingMode::NonBlocking) || non_block)
						{
							// Non-blocking read: `EAGAIN` means no data right now -> retry later (0 bytes), not an error.
							// Only a non-blocking socket at end-of-stream is actually closed.
							if ((_blocking_mode == BlockingMode::NonBlocking) && IsEndOfStream())
							{
								// End of stream - route through the disconnect handling below
								// (`read_bytes` == 0), not the `EAGAIN`/timeout path.
								read_bytes	 = 0L;
								socket_error = SocketError::CreateError("Remote is disconnected");
							}
							else
							{
								// No data available now - retry later
								read_bytes	 = 0L;
								socket_error = nullptr;
							}
						}
						else
						{
							// Blocking read timed out (`SO_RCVTIMEO`)
							socket_error = SocketError::CreateError(code, "Receive timed out: %s", error->GetMessage().CStr());
						}
					}
					else
					{
						socket_error = SocketError::CreateError(error);
					}
				}

				break;
			}

			case SocketType::Srt: {
				IoGuard io_guard(*this);

				if (io_guard.IsAcquired() == false)
				{
					return tl::make_unexpected(SocketError::CreateError("Socket is closed"));
				}

				SRT_MSGCTRL msg_ctrl{};
				read_bytes = ::srt_recvmsg2(GetNativeHandle(), reinterpret_cast<char *>(data), static_cast<int>(length), &msg_ctrl);

				if (read_bytes == 0L)
				{
					// `srt_recvmsg2()` returns `0` only on EOF (buffer/stream mode);
					// no-data is `EASYNCRCV` and a broken link is `ECONNLOST`, both negative.
					// The `0` return does not set the SRT last error, so classify by the return value -
					// `srt_getlasterror()` may be stale here.
					socket_error = SocketError::CreateError("Remote is disconnected");
				}
				else if (read_bytes < 0L)
				{
					auto error = SrtError::CreateErrorFromSrt();

					if (error->GetCode() == SRT_EASYNCRCV)
					{
						// Timed out
						read_bytes	 = 0L;
						// Actually, it is not an error
						socket_error = nullptr;
					}
					else
					{
						socket_error = SocketError::CreateError(error->GetCode(), "Receive timed out (SRT): %s", error->GetMessage().CStr());
					}
				}

				break;
			}
		}

		if ((read_bytes <= 0L) && (IsOpen() == false))
		{
			// Woken or failed by a close on another thread
			return tl::make_unexpected(SocketError::CreateError("Socket is closed"));
		}

		if (socket_error != nullptr)
		{
			if ((read_bytes == 0L) ||
				((_socket.GetType() == SocketType::Srt) &&
				 ((socket_error->GetCode() == SRT_ECONNLOST) || (socket_error->GetCode() == SRT_EINVSOCK))))
			{
				logtt("Remote is disconnected with error: %s", socket_error->What());

				socket_error = SocketError::CreateError("Remote is disconnected");

				// NOTE: `SRT_ECONNLOST` is ambiguous - close as plain Disconnected.
				//
				// libsrt reports `SRT_ECONNLOST` for BOTH a real network break
				// AND a graceful peer `srt_close()` - when the peer sends
				// `UMSG_SHUTDOWN`, libsrt's `processCtrlShutdown()` sets the
				// broken flag and calls `completeBrokenConnectionDependencies(SRT_ECONNLOST)`,
				// so the receiver cannot tell the two cases apart at the API level.
				// Close with `Disconnected` (same posture as TCP RST) instead of `Error`
				// so downstream does not treat this as an unambiguous transport failure.
				//
				// `SRT_EINVSOCK` is the same break read late: OME has not closed the socket,
				// so libsrt's garbage collector closed it after the connection broke.
				CloseWithState(SocketState::Disconnected);

				return tl::make_unexpected(socket_error);
			}
			else if (read_bytes < 0L)
			{
				if (_socket.GetType() != SocketType::Srt)
				{
					SocketState new_state = SocketState::Error;

					switch (socket_error->GetCode())
					{
						// Errors that can occur under normal circumstances do not output
						case ECONNRESET:
							// Peer is disconnected
							new_state = SocketState::Disconnected;
							break;

						case ENOTCONN:
							// Transport endpoint is not connected
							break;

						case ETIMEDOUT:
							// Even though the socket is non-blocking and MSG_DONTWAIT is used,
							// the kernel may still return ETIMEDOUT if the connection is deemed broken.
							// This can happen when the TCP session was previously established,
							// but the peer has not responded for a long time (e.g., due to network issues,
							// a dropped connection, or an unresponsive peer behind a firewall or NAT).
							break;

						case EAGAIN:
							// Timed out
							OV_ASSERT2(_blocking_mode == BlockingMode::Blocking);
							break;

						default:
							logae("An error occurred while read data: %s\nStack trace: %s",
								  socket_error->What(),
								  StackTrace::GetStackTrace().CStr());
					}

					CloseWithState(new_state);
				}
				else
				{
					switch (socket_error->GetCode())
					{
						default:
							logae("An error occurred while read data: %s\nStack trace: %s",
								  socket_error->What(),
								  StackTrace::GetStackTrace().CStr());
					}

					CloseWithState(SocketState::Error);
				}
			}
		}
		else
		{
			logap("%zd bytes read", read_bytes);
			received_length = static_cast<size_t>(read_bytes);

			// Only refresh the last-recv time when data actually arrived;
			// a `0`-byte success (retry-later / would-block) must not mask idle timeouts.
			if (read_bytes > 0L)
			{
				UpdateLastRecvTime();
			}
		}

		if (socket_error != nullptr)
		{
			return tl::make_unexpected(socket_error);
		}

		return received_length;
	}

	template <typename Tpktinfo>
	const Tpktinfo *QueryLocalAddress(msghdr *msg, cmsghdr *cmsg, int msg_level, int msg_type)
	{
		auto current_cmsg = cmsg;

		while (current_cmsg != nullptr)
		{
			if ((current_cmsg->cmsg_level == msg_level) && (current_cmsg->cmsg_type == msg_type))
			{
				return reinterpret_cast<Tpktinfo *>(CMSG_DATA(current_cmsg));
			}

			current_cmsg = CMSG_NXTHDR(msg, current_cmsg);
		}

		return nullptr;
	}

	SocketAddress QueryLocalAddress(
		const SocketFamily family,
		const int local_port,
		const sockaddr_storage &remote,
		msghdr *msg)
	{
		auto cmsg = CMSG_FIRSTHDR(msg);

		if (family == SocketFamily::Inet)
		{
			const auto pktinfo = QueryLocalAddress<in_pktinfo>(msg, cmsg, IPPROTO_IP, IP_PKTINFO);

			if (pktinfo != nullptr)
			{
				sockaddr_storage local{};

				local.ss_family = remote.ss_family;

				auto sock		= ToSockAddrIn4(&local);

				sock->sin_port	= HostToNetwork16(local_port);
				sock->sin_addr	= pktinfo->ipi_addr;

				return SocketAddress("", local);
			}
		}
		else if (family == SocketFamily::Inet6)
		{
			const auto pktinfo = QueryLocalAddress<in6_pktinfo>(msg, cmsg, IPPROTO_IPV6, IPV6_PKTINFO);

			if (pktinfo != nullptr)
			{
				sockaddr_storage local{};

				local.ss_family	   = remote.ss_family;

				const auto pktinfo = reinterpret_cast<in6_pktinfo *>(CMSG_DATA(cmsg));

				auto sock		   = ToSockAddrIn6(&local);

				sock->sin6_port	   = local_port;
				sock->sin6_addr	   = pktinfo->ipi6_addr;

				return SocketAddress("", local);
			}

			cmsg = CMSG_NXTHDR(msg, cmsg);
		}

		return SocketAddress();
	}

	std::shared_ptr<const SocketError> Socket::RecvFrom(std::shared_ptr<Data> &data, SocketAddressPair *address_pair, const bool non_block)
	{
		OV_ASSERT2(data != nullptr);
		OV_ASSERT2(data->GetCapacity() > 0);

		std::shared_ptr<SocketError> socket_error;

		switch (GetType())
		{
			case SocketType::Udp:
			case SocketType::Tcp: {
				IoGuard io_guard(*this);

				if (io_guard.IsAcquired() == false)
				{
					data->SetLength(0L);
					return SocketError::CreateError("Socket is closed");
				}

				sockaddr_storage remote{};
				socklen_t remote_length = sizeof(remote);

				logat("Trying to read from the socket...");
				data->SetLength(data->GetCapacity());

				iovec iov{};
				iov.iov_base			   = data->GetWritableData();
				iov.iov_len				   = data->GetLength();

				char control_buf[CMSG_SPACE(sizeof(in6_pktinfo))];
				::memset(control_buf, 0, sizeof(control_buf));

				msghdr msg{};
				msg.msg_name			 = &remote;
				msg.msg_namelen			 = remote_length;
				msg.msg_control			 = control_buf;
				msg.msg_controllen		 = sizeof(control_buf);
				msg.msg_iov				 = &iov;
				msg.msg_iovlen			 = 1;

				const ssize_t read_bytes = ::recvmsg(
					GetNativeHandle(),
					&msg,
					((_blocking_mode == BlockingMode::NonBlocking) || non_block) ? MSG_DONTWAIT : 0);

				if ((read_bytes <= 0L) && (IsOpen() == false))
				{
					// Woken or failed by a close on another thread, which leaves no source address to parse
					data->SetLength(0L);
					return SocketError::CreateError("Socket is closed");
				}

				if (read_bytes < 0L)
				{
					auto error = Error::CreateErrorFromErrno();

					data->SetLength(0L);

					if (error->GetCode() == EAGAIN)
					{
						// Timed out
						socket_error = nullptr;
					}
					else
					{
						socket_error = SocketError::CreateError(error);
					}
				}
				else
				{
					logat("%zd bytes read", read_bytes);

					data->SetLength(read_bytes);

					if (address_pair != nullptr)
					{
						const auto port = GetLocalAddress()->Port();

						const auto transport = _local_address->GetTransport();

						auto local = QueryLocalAddress(_family, port, remote, &msg);
						local.SetTransport(transport);
						address_pair->SetLocalAddress(local);

						SocketAddress remote_addr("", remote);
						remote_addr.SetTransport(transport);
						address_pair->SetRemoteAddress(remote_addr);
					}

					UpdateLastRecvTime();
				}
				break;
			}

			case SocketType::Srt:
				// Does not support RecvFrom() for SRT
				OV_ASSERT2(false);
				socket_error = SocketError::CreateError("RecvFrom() is not supported operation while using SRT");
				break;

			case SocketType::Unknown:
				OV_ASSERT2(false);
				socket_error = SocketError::CreateError("Unknown socket type");
				break;
		}

		if (socket_error != nullptr)
		{
			logae("An error occurred while read data: %s\nStack trace: %s",
				  socket_error->What(),
				  StackTrace::GetStackTrace().CStr());

			CloseWithState(SocketState::Error);
		}

		return socket_error;
	}

	std::chrono::steady_clock::time_point Socket::GetLastRecvTime() const
	{
		return _last_recv_time.load(std::memory_order_relaxed);
	}

	std::chrono::steady_clock::time_point Socket::GetLastSentTime() const
	{
		return _last_sent_time.load(std::memory_order_relaxed);
	}

	void Socket::UpdateLastRecvTime()
	{
		_last_recv_time.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
	}

	void Socket::UpdateLastSentTime()
	{
		_last_sent_time.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
	}

	bool Socket::Flush()
	{
		CHECK_STATE(== SocketState::Connected, false);

		// Dispatch ALL commands
		while (HasCommand())
		{
			if (DispatchEvents() == DispatchResult::Error)
			{
				return false;
			}
		}

		return true;
	}

	bool Socket::CloseIfNeeded()
	{
		if (_blocking_mode == BlockingMode::Blocking)
		{
			return Close();
		}

		return HasCloseCommand() || Close();
	}

	bool Socket::CloseWithState(SocketState new_state)
	{
		CHECK_STATE(>= SocketState::Closed, false);

		logat("Closing [%s] with state %s...",
			  (GetRemoteAddress() != nullptr)
				  ? GetRemoteAddress()->ToString(false).CStr()
				  : GetStreamId().CStr(),
			  StringFromSocketState(new_state));

		if (GetState() == SocketState::Closed)
		{
			// Suppress error message
			return false;
		}

		switch (_blocking_mode)
		{
			case BlockingMode::Blocking: {
				bool result = true;
				bool closed_at_once = false;

				{
					// A socket that has not connected has nothing to flush, so it is closed here at once.
					// A connect sets its state under this lock, so one in progress reports the close.
					LockGuard lock(_dispatch_queue_lock);
					const auto state = GetState();

					if ((state == SocketState::Created) || (state == SocketState::Connecting))
					{
						closed_at_once = true;
						result		   = CloseAndSetState(new_state);
					}
				}

				if (closed_at_once == false)
				{
					if (GetState() != SocketState::Error)
					{
						if (HalfClose() == DispatchResult::Error)
						{
							result = false;
						}
						if (WaitForHalfClose() == DispatchResult::Error)
						{
							result = false;
						}
					}

					// Close regardless of result
					LockGuard lock(_dispatch_queue_lock);

					result = CloseAndSetState(new_state) && result;
				}

				CallCloseCallbackIfNeeded();

				return result;
			}

			case BlockingMode::NonBlocking: {
				SOCKET_PROFILER_INIT();
				LockGuard lock_guard(_dispatch_queue_lock);
				SOCKET_PROFILER_AFTER_LOCK();

				SOCKET_PROFILER_POST_HANDLER([&](int64_t lock_elapsed, int64_t total_elapsed) {
					if ((lock_elapsed > 100) || (_dispatch_queue.size() > 10))
					{
						logtw("[SockProfiler] Close() - %s, Queue: %zu, Lock: %dms, Total: %dms", ToString().CStr(), _dispatch_queue.size(), lock_elapsed, total_elapsed);
					}
				});

				// A close already ran under this lock and cleared the queue, so nothing is queued:
				// only the state moves to `new_state`, unless it is already `Closed`
				if (IsOpen() == false)
				{
					_has_close_command = true;

					if (GetState() == SocketState::Closed)
					{
						return false;
					}

					SetState(new_state);

					return true;
				}

				if (_has_close_command == false)
				{
					logat("Enqueuing close command (new_state: %s)", StringFromSocketState(new_state));

					_has_close_command = true;

					if ((GetState() != SocketState::Disconnected) && (GetState() != SocketState::Error))
					{
						_dispatch_queue.emplace_back(DispatchCommand::Type::HalfClose);
						_dispatch_queue.emplace_back(DispatchCommand::Type::WaitForHalfClose);
					}

					_dispatch_queue.emplace_back(DispatchCommand::Type::Close, new_state);
				}
				else
				{
					logat("This socket already has close command (Do not need to call Close*() in this case)\n%s", StackTrace::GetStackTrace().CStr());
				}

				_worker->EnqueueToDispatchLater(GetSharedPtr());

				return true;
			}
		}

		return false;
	}

	bool Socket::Close()
	{
		return CloseWithState(SocketState::Closed);
	}

	bool Socket::CloseImmediately()
	{
		bool result = false;

		{
			LockGuard lock_guard(_dispatch_queue_lock);

			result = CloseInternal(_state);

			if (result)
			{
				SetState(SocketState::Closed);
			}
		}

		CallCloseCallbackIfNeeded();

		return result;
	}

	bool Socket::CloseImmediatelyWithState(SocketState new_state)
	{
		bool result = false;

		{
			LockGuard lock_guard(_dispatch_queue_lock);

			result = CloseAndSetState(new_state);
		}

		CallCloseCallbackIfNeeded();

		return result;
	}

	std::shared_ptr<Error> Socket::AddToExternalEpoll(int epoll_fd, epoll_event *event)
	{
		if (GetType() == SocketType::Srt)
		{
			OV_ASSERT(false, "SRT socket cannot be registered in an epoll instance");
			return Error::CreateError("Socket", "SRT socket cannot be registered in an epoll instance");
		}

		IoGuard io_guard(*this);

		if (io_guard.IsAcquired() == false)
		{
			return Error::CreateError("Socket", "Socket is closed");
		}

		if (::epoll_ctl(epoll_fd, EPOLL_CTL_ADD, GetNativeHandle(), event) != 0)
		{
			return Error::CreateErrorFromErrno();
		}

		return nullptr;
	}

	std::shared_ptr<Error> Socket::DeleteFromExternalEpoll(int epoll_fd)
	{
		if (GetType() == SocketType::Srt)
		{
			OV_ASSERT(false, "SRT socket cannot be registered in an epoll instance");
			return Error::CreateError("Socket", "SRT socket cannot be registered in an epoll instance");
		}

		// A closing socket still has to leave the epoll instance
		auto io_guard = IoGuard::Pin(*this);

		if (io_guard.IsAcquired() == false)
		{
			// Released: the thread that dropped the last reference closes the handle, which removes it from the epoll instance
			return nullptr;
		}

		if ((::epoll_ctl(epoll_fd, EPOLL_CTL_DEL, GetNativeHandle(), nullptr) != 0) && (errno != ENOENT))
		{
			return Error::CreateErrorFromErrno();
		}

		return nullptr;
	}

	Socket::DispatchResult Socket::HalfClose()
	{
		if (GetType() == SocketType::Tcp)
		{
			IoGuard io_guard(*this);

			if (io_guard.IsAcquired())
			{
				// Send FIN
				::shutdown(GetNativeHandle(), SHUT_WR);
			}
		}

		return DispatchResult::Dispatched;
	}

	Socket::DispatchResult Socket::WaitForHalfClose()
	{
		if (GetType() != SocketType::Tcp)
		{
			return DispatchResult::Dispatched;
		}

		switch (GetState())
		{
			case SocketState::Created:
				[[fallthrough]];
			case SocketState::Disconnected:
				return DispatchResult::Dispatched;

			default:
				break;
		}

		IoGuard io_guard(*this);

		if (io_guard.IsAcquired() == false)
		{
			return DispatchResult::Dispatched;
		}

		while (true)
		{
			char buffer[MAX_BUFFER_SIZE];
			int result = ::recv(GetNativeHandle(), buffer, MAX_BUFFER_SIZE, MSG_DONTWAIT);

			if (result < 0L)
			{
				auto error = Error::CreateErrorFromErrno();

				switch (error->GetCode())
				{
					case EAGAIN:
						// Peer doesn't send ACK/FIN yet - ignores this
						return DispatchResult::Dispatched;

					case ECONNRESET:
						// Suppress "Connection reset by peer" message
						break;

					case ENOTCONN:
						// Suppress "Transport endpoint is not connected" message
						break;

					default:
						logae("An error occurred while half-closing: %s", error->What());
						break;
				}

				return DispatchResult::Error;
			}
			else if (result == 0)
			{
				logat("Half closed");
				return DispatchResult::Dispatched;
			}
			else
			{
				// Ignores those data
			}
		}
	}

	bool Socket::CloseInternal(SocketState close_reason)
	{
		// Only the first close of an assigned handle goes on, so a later close leaves `_post_callback` alone
		const auto io_ref = _io_ref.fetch_or(IO_REF_CLOSING, std::memory_order_acq_rel);

		if ((io_ref & IO_REF_CLOSING) != 0)
		{
			logat("Socket is already closed");
			return false;
		}

		_close_reason = close_reason;

		// A blocking connect waiting on it sees the socket closed once the close releases `_dispatch_queue_lock`
		WakeConnectWaiter();

		// Only a socket that reported its connection gets a close callback
		auto post_callback			  = std::atomic_exchange(&_callback, {});
		const bool has_close_callback = (post_callback != nullptr) && _connection_event_fired;

		if (has_close_callback)
		{
			std::atomic_store(&_post_callback, std::move(post_callback));
		}

		// `DeleteFromWorker()` takes `_worker_mutex` after `IO_REF_CLOSING` is set above,
		// so `AddToWorker()` either registers the socket before this removes it, or sees the mark and does not register it.
		// It also runs before the shutdown below, which would otherwise raise events for this socket in the worker.
		DeleteFromWorker();

		if (has_close_callback == false)
		{
			_count_slot.Reset();
		}

		switch (GetType())
		{
			case SocketType::Tcp:
			case SocketType::Udp:
				if ((io_ref & ~IO_REF_CLOSING) > 1)
				{
					// Wakes a receive or accept blocked on another thread, so the handle is released once it returns.
					// A blocking TCP connect may not be woken (see `Connect()`).
					::shutdown(GetNativeHandle(), SHUT_RDWR);
				}
				break;

			case SocketType::Srt:
				::srt_close(GetNativeHandle());
				break;

			default:
				break;
		}

		// Once the socket is out of epoll, the worker may never deliver its first epoll event.
		// So a waiting `AddToWorker()` is woken up here.
		_first_epoll_event_received.SetEvent();

		if (_dispatch_queue.size() > 0)
		{
			if (_dispatch_queue.front().IsCloseCommand() == false)
			{
				logaw("Socket was closed even though there was %zu commands in the socket", _dispatch_queue.size());

#if DEBUG
				for (auto &queue : _dispatch_queue)
				{
					logat("  - Command: %s", queue.ToString().CStr());
				}
#endif	// DEBUG
			}
		}

		_dispatch_queue.clear();

		// Drops the owner reference. The handle is released here, or when the last syscall in flight returns.
		EndIo();

		logat("Socket is closed successfully");

		return true;
	}

	bool Socket::CloseAndSetState(SocketState new_state)
	{
		if (CloseInternal(new_state) == false)
		{
			return false;
		}

		SetState(new_state);

		return true;
	}

	void Socket::CallCloseCallbackIfNeeded()
	{
		auto post_callback = std::atomic_exchange(&_post_callback, {});

		// Only the caller that takes `_post_callback` reaches `_count_slot` here, and `CloseInternal()` left it alone
		if (post_callback != nullptr)
		{
			_worker->EnqueueToCloseCallbackLater(GetSharedPtr(), post_callback, std::move(_count_slot));
		}
	}

	String Socket::ToString(const char *class_name) const
	{
		String caller(class_name);
		bool ignore_privacy_protection = true;

		// ClientSocket must follow privacy rule
		if (caller == "ClientSocket")
		{
			ignore_privacy_protection = false;
		}

		String extra(256);

		if (_remote_address != nullptr)
		{
			extra.Append(", ");
			extra.Append(_remote_address->ToString(ignore_privacy_protection));
		}

		if (GetType() == SocketType::Srt)
		{
			extra.Append(", streamid: ");
			extra.Append(_stream_id);
		}

		return String::FormatString(
			"<%s: %p, #%d, %s, %s, %s%s>",
			class_name, this,
			GetNativeHandle(), StringFromSocketState(GetState()),
			StringFromSocketType(GetType()),
			StringFromBlockingMode(_blocking_mode),
			extra.CStr());
	}

	String Socket::ToString() const
	{
		return ToString("Socket");
	}
}  // namespace ov
