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

namespace ov
{
	// Used to wait for connection
	class ConnectHelper : public SocketAsyncInterface
	{
	public:
		void OnConnected(const std::shared_ptr<const SocketError> &error) override
		{
			std::atomic_store_explicit(&_error, error, std::memory_order_release);
			_epoll_event.SetEvent();
		}

		void OnReadable() override
		{
		}

		void OnClosed() override
		{
		}

		bool WaitForConnect(int timeout)
		{
			return _epoll_event.Wait(timeout);
		}

		std::shared_ptr<const SocketError> GetError() const
		{
			return std::atomic_load_explicit(&_error, std::memory_order_acquire);
		}

	protected:
		Event _epoll_event{true};
		std::shared_ptr<const SocketError> _error;
	};

	Socket::Socket(PrivateToken token, const std::shared_ptr<SocketPoolWorker> &worker)
		: _worker(worker)
	{
		OV_ASSERT(worker != nullptr, "Worker must be not nullptr");
	}

	// Creates a socket using remote information.
	Socket::Socket(PrivateToken token, const std::shared_ptr<SocketPoolWorker> &worker,
				   SocketWrapper socket, const SocketAddress &remote_address)
		: Socket(token, worker)
	{
		_socket = socket;

		// Remote information is present, considered already connected
		SetState(SocketState::Connected);

		_remote_address = std::make_shared<SocketAddress>(remote_address);
	}

	Socket::~Socket()
	{
		// Verify that the socket is closed normally
		OV_ASSERT(_socket.IsValid() == false, "Socket is not closed. Current state: %s", StringFromSocketState(GetState()));
		CHECK_STATE2(== SocketState::Closed, >= SocketState::Disconnected, );
	}

	bool Socket::Create(const SocketType type, const SocketFamily family)
	{
		CHECK_STATE(== SocketState::Closed, false);

		if (_socket.IsValid())
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

			logat("Socket descriptor is created (%s/%s)",
				  StringFromSocketFamily(family),
				  StringFromSocketType(type));

			_has_close_command = false;
			_end_of_stream = false;

			_connection_event_fired = false;

			_family = family;

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

		// An error occurred - reset all variables
		{
			SocketState state;
			{
				std::lock_guard lock_guard(_state_mutex);
				state = _state;
			}

			std::lock_guard lock_guard(_dispatch_queue_lock);
			if (CloseInternal(state))
			{
				SetState(SocketState::Closed);
			}
		}

		return false;
	}

	bool Socket::SetBlockingInternal(BlockingMode mode)
	{
		if (_socket.IsValid() == false)
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

				result = ::fcntl(GetNativeHandle(), F_SETFL, flags);

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
		std::lock_guard lock_guard(_dispatch_queue_lock);
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
			std::lock_guard lock_guard(_worker_mutex);

			if (_added_to_worker)
			{
				// `DeleteFromWorker()` can be called multiple times due to timing,
				// but `AddToWorker()` must not be called redundantly.
				OV_ASSERT2(false);
				return true;
			}

			if (GetType() == SocketType::Srt)
			{
				// SRT doesn't generates any epoll events after ::srt_epoll_add_usock()
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
			return WaitForFirstEpollEvent();
		}

		return true;
	}

	bool Socket::DeleteFromWorker()
	{
		std::lock_guard lock_guard(_worker_mutex);

		if (_added_to_worker)
		{
			if (_worker->DeleteFromEpoll(GetSharedPtr()))
			{
				_added_to_worker = false;
				return true;
			}
		}

		return true;
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
			OV_ASSERT2(_callback == nullptr);

			return true;
		}

		// Prevent to call the callback while change blocking mode
		auto old_callback = std::move(_callback);

		if (
			SetBlockingInternal(BlockingMode::Blocking) &&
			DeleteFromWorker())
		{
			_blocking_mode = BlockingMode::Blocking;
			return true;
		}

		// Rollback
		_callback = std::move(old_callback);

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
			_callback = std::move(callback);

			return true;
		}

		auto old_callback = std::move(_callback);

		_callback = std::move(callback);
		_blocking_mode = BlockingMode::NonBlocking;

		if (
			SetBlockingInternal(BlockingMode::NonBlocking) &&
			AddToWorker(need_to_wait_first_epoll_event))
		{
			return true;
		}

		// Rollback
		_callback = std::move(old_callback);
		_blocking_mode = BlockingMode::Blocking;

		return false;
	}

	bool Socket::Bind(const SocketAddress &address)
	{
		CHECK_STATE(== SocketState::Created, false);

		if (_socket.IsValid() == false)
		{
			logae("Could not bind socket (Invalid socket)");
			OV_ASSERT2(_socket.IsValid());
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

		switch (GetType())
		{
			case SocketType::Tcp: {
				sockaddr_storage client_addr{};
				socklen_t client_length = sizeof(client_addr);

				socket_t client_socket = ::accept(GetNativeHandle(), reinterpret_cast<sockaddr *>(&client_addr), &client_length);

				if (client_socket != InvalidSocket)
				{
					*client = SocketAddress("", client_addr);
				}

				return SocketWrapper(GetType(), client_socket);
			}

			case SocketType::Srt: {
				sockaddr_storage client_addr{};
				int client_length = sizeof(client_addr);

				SRTSOCKET client_socket = ::srt_accept(GetNativeHandle(), reinterpret_cast<sockaddr *>(&client_addr), &client_length);

				if (client_socket != SRT_INVALID_SOCK)
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
		OV_ASSERT2(_socket.IsValid());

		CHECK_STATE(== SocketState::Created, DoConnectionCallback(SocketError::CreateError(EINVAL, "Invalid state: %d", static_cast<int>(_state))));

		if (endpoint.IsValid() == false)
		{
			return DoConnectionCallback(SocketError::CreateError("Invalid address: %s", endpoint.ToString().CStr()));
		}

		std::shared_ptr<const SocketError> socket_error;
		std::shared_ptr<ConnectHelper> connect_helper;

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

						connect_helper = std::make_shared<ConnectHelper>();
						_callback = connect_helper;
					}
				}

				SetState(SocketState::Connecting);

				logat("Trying to connect to %s...", endpoint.ToString().CStr());
				int result = ::connect(GetNativeHandle(), endpoint, endpoint.GetSockAddrInLength());

				if (result == 0)
				{
					if (_blocking_mode == BlockingMode::Blocking)
					{
						_callback = nullptr;

						if (use_timeout)
						{
							// Restore blocking state
							if (
								(DeleteFromWorker() == false) ||
								(SetBlockingInternal(BlockingMode::Blocking) == false))
							{
								return DoConnectionCallback(SocketError::CreateError("Socket cannot be changed to blocking mode: %s", ToString().CStr()));
							}
						}
					}

					return DoConnectionCallback(nullptr);
				}

				if (result < 0)
				{
					auto error = Error::CreateErrorFromErrno();

					// Check timeout
					if (error->GetCode() == EINPROGRESS)
					{
						if (_blocking_mode == BlockingMode::NonBlocking)
						{
							// Don't wait for the connection if this socket is non-blocking mode
							_worker->EnqueueToCheckConnectionTimeOut(GetSharedPtr(), timeout_msec);
							return nullptr;
						}

						// Blocking mode
						if (connect_helper->WaitForConnect(timeout_msec))
						{
							socket_error = connect_helper->GetError();
						}
						else
						{
							// Timed out
							int so_error;
							GetSockOpt(SO_ERROR, &so_error);

							socket_error = SocketError::CreateError(so_error, "Connection timed out (%d ms elapsed, SO_ERROR: %d)", timeout_msec, so_error);
							logat("%s", socket_error->What());
						}

						// Restore blocking state
						if (
							(DeleteFromWorker() == false) ||
							(SetBlockingInternal(BlockingMode::Blocking) == false))
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
				if (SetSockOpt(SRTO_CONNTIMEO, timeout_msec))
				{
					if (::srt_connect(GetNativeHandle(), endpoint, endpoint.GetSockAddrInLength()) != SRT_ERROR)
					{
						return DoConnectionCallback(nullptr);
					}
				}

				socket_error = SocketError::CreateError(SrtError::CreateErrorFromSrt());

				break;

			default:
				socket_error = SocketError::CreateError("Not implemented");
				break;
		}

		return DoConnectionCallback(socket_error);
	}

	bool Socket::SetRecvTimeout(const timeval &tv)
	{
		OV_ASSERT2(_socket.IsValid());
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
		std::lock_guard lock_guard(_state_mutex);
		return _state;
	}

	void Socket::SetState(SocketState state)
	{
		std::lock_guard lock_guard(_state_mutex);

		logat("Socket state is changed: %s => %s",
			  StringFromSocketState(_state),
			  StringFromSocketState(state));

		_state = state;
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
			SetState(SocketState::Connected);

			if (_callback != nullptr)
			{
				_connection_event_fired = true;
				_callback->OnConnected(error);
			}
		}
		else
		{
			auto callback = std::move(_callback);

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
		auto &data = command.data;

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
			return DispatchResult::Error;
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

		return DispatchResult::PartialDispatched;
	}

	Socket::DispatchResult Socket::DispatchEventsInternal()
	{
		SOCKET_PROFILER_INIT();

		DispatchResult result = DispatchResult::Dispatched;

		{
			SOCKET_PROFILER_AFTER_LOCK();

			std::lock_guard lock_guard(_dispatch_queue_lock);

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
					auto front = _dispatch_queue.front();
					_dispatch_queue.pop_front();

					bool is_close_command = front.IsCloseCommand();

					if ((GetState() == SocketState::Closed) && (is_close_command == false))
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

						_dispatch_queue.emplace_front(front);

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
				std::lock_guard lock_guard(_dispatch_queue_lock);
				[[maybe_unused]] auto count = _dispatch_queue.size();
				OV_ASSERT2(count == 0);
			}
#endif	// DEBUG
				return DispatchResult::Dispatched;

			case BlockingMode::NonBlocking: {
				// Due to the connection callback point, the DispatchEventsInternal() specifically performs mutex.lock inside.
				// std::lock_guard lock_guard(_dispatch_queue_lock);
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

		if (_callback != nullptr)
		{
			_callback->OnReadable();
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

		// Check if the socket is closed
		return (_has_close_command == false);
	}

	ssize_t Socket::HandleSendError(const ssize_t result, const size_t total_sent)
	{
		const auto error = Error::CreateErrorFromErrno();

		switch (error->GetCode())
		{
			case EAGAIN:
				// Socket buffer is full - retry later
				STATS_COUNTER_INCREASE_RETRY();
				return total_sent;

			case EBADF:
				// Socket is closed somewhere in OME
				[[fallthrough]];
			case EPIPE:
				// Broken pipe - maybe peer is disconnected
				[[fallthrough]];
			case ECONNRESET:
				// Connection reset - maybe peer is disconnected
				break;

			default:
				logaw("Could not send data: %zd (%s)", result, error->What());
				break;
		}

		STATS_COUNTER_INCREASE_ERROR();
		return result;
	}

	ssize_t Socket::SendData(const std::shared_ptr<const Data> &data)
	{
		auto data_to_send = data->GetDataAs<uint8_t>();
		size_t remaining_bytes = data->GetLength();
		size_t total_sent_bytes = 0L;

		logap("Trying to send data %zu bytes...", remaining_bytes);

		while ((remaining_bytes > 0L) && (_force_stop == false))
		{
			const auto sent = ::send(GetNativeHandle(), data_to_send, remaining_bytes, MSG_NOSIGNAL | MSG_DONTWAIT);

			if (sent < 0L)
			{
				return HandleSendError(sent, total_sent_bytes);
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
		auto data_to_send = data->GetDataAs<char>();
		size_t remaining_bytes = data->GetLength();
		size_t total_sent_bytes = 0L;

		logap("Trying to send data %zu bytes...", remaining_bytes);

		while ((remaining_bytes > 0L) && (_force_stop == false))
		{
			// SRT limits packet size up to 1316
			const auto bytes_to_send = std::min(1316UL, remaining_bytes);

			const auto sent = ::srt_sendmsg(GetNativeHandle(), data_to_send, bytes_to_send, -1, 1);

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
					return AppendCommand({data->Clone()}, true);
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

		auto data_to_send = data->GetDataAs<uint8_t>();
		size_t remaining_bytes = data->GetLength();
		size_t total_sent_bytes = 0L;

		logap("Trying to send data %zu bytes to %s...", remaining_bytes, address.ToString(false).CStr());

		while ((remaining_bytes > 0L) && (_force_stop == false))
		{
			const ssize_t sent = ::sendto(GetNativeHandle(), data_to_send, remaining_bytes, MSG_NOSIGNAL | MSG_DONTWAIT, address, address.GetSockAddrInLength());

			if (sent < 0L)
			{
				return HandleSendError(sent, total_sent_bytes);
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
					return AppendCommand(
						(GetType() == SocketType::Udp)
							? DispatchCommand(address, data->Clone())
							: DispatchCommand(data->Clone()),
						true);
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
		volatile const bool &force_stop)
	{
		OV_ASSERT2(total_sent_bytes != nullptr);
		*total_sent_bytes = 0L;

		auto data_to_send = static_cast<const uint8_t *>(data);
		size_t remaining_bytes = length;

		char control[CMSG_SPACE(sizeof(Tpktinfo))]{};

		iovec iov{};
		// This is intentional conversion
		iov.iov_base = const_cast<uint8_t *>(data_to_send);
		iov.iov_len = remaining_bytes;

		Tpktinfo pktinfo{};
		SetAddr(&pktinfo, local_address);

		auto cmsg = reinterpret_cast<cmsghdr *>(control);
		cmsg->cmsg_level = msg_level;
		cmsg->cmsg_type = msg_type;
		cmsg->cmsg_len = CMSG_LEN(sizeof(pktinfo));
		::memcpy(CMSG_DATA(cmsg), &pktinfo, sizeof(pktinfo));

		msghdr msg{};
		// This is intentional conversion
		msg.msg_name = const_cast<sockaddr *>(remote_address.ToSockAddr());
		msg.msg_namelen = remote_address.GetSockAddrInLength();
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		msg.msg_control = control;
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

		const auto local_address = address_pair.GetLocalAddress();
		const auto remote_address = address_pair.GetRemoteAddress();

		const auto data_length = data->GetLength();

		logap("Trying to send data %zu bytes to %s from %s...",
			  data_length,
			  remote_address.ToString().CStr(), local_address.ToString().CStr());

		size_t total_sent_bytes = 0;
		bool sent = false;

		switch (_family)
		{
			case SocketFamily::Unknown:
				OV_ASSERT2(false);
				return -1L;

			case SocketFamily::Inet:
				sent = ov::SendFromToInternal<in_pktinfo>(
					GetNativeHandle(),
					IPPROTO_IP, IP_PKTINFO,
					address_pair.GetLocalAddress(), address_pair.GetRemoteAddress(),
					data->GetData(), data_length,
					&total_sent_bytes,
					_force_stop);
				break;

			case SocketFamily::Inet6:
				sent = ov::SendFromToInternal<in6_pktinfo>(
					GetNativeHandle(),
					IPPROTO_IPV6, IPV6_PKTINFO,
					address_pair.GetLocalAddress(), address_pair.GetRemoteAddress(),
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
			return HandleSendError(sent, total_sent_bytes);
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
					return AppendCommand(
						(GetType() == SocketType::Udp)
							? DispatchCommand(address_pair, data->Clone())
							: DispatchCommand(data->Clone()),
						true);
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

		size_t read_bytes;

		data->SetLength(data->GetCapacity());

		auto error = Recv(data->GetWritableData(), data->GetCapacity(), &read_bytes, non_block);

		if (error != nullptr)
		{
			return error;
		}

		data->SetLength(read_bytes);

		return nullptr;
	}

	std::shared_ptr<const SocketError> Socket::Recv(void *data, size_t length, size_t *received_length, const bool non_block)
	{
		if (GetState() == SocketState::Closed)
		{
			return SocketError::CreateError("Socket is closed");
		}

		OV_ASSERT2(data != nullptr);
		OV_ASSERT2(received_length != nullptr);

		logap("Trying to read from the socket...");

		ssize_t read_bytes = -1;
		std::shared_ptr<SocketError> socket_error;

		switch (GetType())
		{
			case SocketType::Udp:
			case SocketType::Tcp:
				read_bytes = ::recv(GetNativeHandle(), data, length,
									((_blocking_mode == BlockingMode::NonBlocking) || non_block) ? MSG_DONTWAIT : 0);

				if (read_bytes <= 0L)
				{
					auto error = Error::CreateErrorFromErrno();

					if (error->GetCode() == EAGAIN)
					{
						if (_blocking_mode == BlockingMode::NonBlocking)
						{
							if (IsEndOfStream() == false)
							{
								// Timed out
								read_bytes = 0L;
								// Actually, it is not an error
								socket_error = nullptr;
							}
							else
							{
								// Socket is closed
								socket_error = SocketError::CreateError(error);
							}
						}
						else
						{
							socket_error = SocketError::CreateError(error->GetCode(), "Receive timed out: %s", error->GetMessage().CStr());
						}
					}
					else
					{
						socket_error = SocketError::CreateError(error);
					}
				}

				break;

			case SocketType::Srt: {
				SRT_MSGCTRL msg_ctrl{};
				read_bytes = ::srt_recvmsg2(GetNativeHandle(), reinterpret_cast<char *>(data), static_cast<int>(length), &msg_ctrl);

				if (read_bytes <= 0L)
				{
					auto error = SrtError::CreateErrorFromSrt();

					if (error->GetCode() == SRT_EASYNCRCV)
					{
						// Timed out
						read_bytes = 0L;
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

			default:
				break;
		}

		if (socket_error != nullptr)
		{
			if (
				((_socket.GetType() != SocketType::Srt) && (read_bytes == 0L)) ||
				((_socket.GetType() == SocketType::Srt) && (socket_error->GetCode() == SRT_ECONNLOST)))
			{
				logtt("Remote is disconnected with error: %s", socket_error->What());

				socket_error = SocketError::CreateError("Remote is disconnected");
				*received_length = 0UL;

				CloseWithState(SocketState::Disconnected);

				return socket_error;
			}
			else if (read_bytes < 0L)
			{
				if (_socket.GetType() != SocketType::Srt)
				{
					SocketState new_state = SocketState::Error;

					switch (socket_error->GetCode())
					{
						// Errors that can occur under normal circumstances do not output
						case EBADF:
							// Socket is closed somewhere in OME
							break;

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
			*received_length = static_cast<size_t>(read_bytes);
			UpdateLastRecvTime();
		}

		return socket_error;
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

				auto sock = ToSockAddrIn4(&local);

				sock->sin_port = HostToNetwork16(local_port);
				sock->sin_addr = pktinfo->ipi_addr;

				return SocketAddress("", local);
			}
		}
		else if (family == SocketFamily::Inet6)
		{
			const auto pktinfo = QueryLocalAddress<in6_pktinfo>(msg, cmsg, IPPROTO_IPV6, IPV6_PKTINFO);

			if (pktinfo != nullptr)
			{
				sockaddr_storage local{};

				local.ss_family = remote.ss_family;

				const auto pktinfo = reinterpret_cast<in6_pktinfo *>(CMSG_DATA(cmsg));

				auto sock = ToSockAddrIn6(&local);

				sock->sin6_port = local_port;
				sock->sin6_addr = pktinfo->ipi6_addr;

				return SocketAddress("", local);
			}

			cmsg = CMSG_NXTHDR(msg, cmsg);
		}

		return SocketAddress();
	}

	std::shared_ptr<const SocketError> Socket::RecvFrom(std::shared_ptr<Data> &data, SocketAddressPair *address_pair, const bool non_block)
	{
		OV_ASSERT2(_socket.IsValid());
		OV_ASSERT2(data != nullptr);
		OV_ASSERT2(data->GetCapacity() > 0);

		std::shared_ptr<SocketError> socket_error;

		switch (GetType())
		{
			case SocketType::Udp:
			case SocketType::Tcp: {
				sockaddr_storage remote{};
				socklen_t remote_length = sizeof(remote);

				logat("Trying to read from the socket...");
				data->SetLength(data->GetCapacity());

				iovec iov{};
				iov.iov_base = data->GetWritableData();
				iov.iov_len = data->GetLength();

				const int control_buf_size = CMSG_SPACE((_family == SocketFamily::Inet) ? sizeof(in_pktinfo) : sizeof(in6_pktinfo));
				char control_buf[control_buf_size];
				::memset(control_buf, 0, sizeof(control_buf));

				msghdr msg{};
				msg.msg_name = &remote;
				msg.msg_namelen = remote_length;
				msg.msg_control = control_buf;
				msg.msg_controllen = sizeof(control_buf);
				msg.msg_iov = &iov;
				msg.msg_iovlen = 1;

				const ssize_t read_bytes = ::recvmsg(
					GetNativeHandle(),
					&msg,
					((_blocking_mode == BlockingMode::NonBlocking) || non_block) ? MSG_DONTWAIT : 0);

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

						address_pair->SetLocalAddress(QueryLocalAddress(_family, port, remote, &msg));
						address_pair->SetRemoteAddress(SocketAddress("", remote));
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

	std::chrono::system_clock::time_point Socket::GetLastRecvTime() const
	{
		return _last_recv_time;
	}

	std::chrono::system_clock::time_point Socket::GetLastSentTime() const
	{
		return _last_sent_time;
	}

	void Socket::UpdateLastRecvTime()
	{
		_last_recv_time = std::chrono::high_resolution_clock::now();
	}

	void Socket::UpdateLastSentTime()
	{
		_last_sent_time = std::chrono::high_resolution_clock::now();
	}

	bool Socket::Flush()
	{
		CHECK_STATE(== SocketState::Connected, false);

		// Dispatch ALL commands
		while (_dispatch_queue.size() > 0)
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

		return IsClosing() || Close();
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
				{
					std::lock_guard lock_guard(_dispatch_queue_lock);

					if (CloseInternal(new_state))
					{
						SetState(new_state);
					}
					else
					{
						result = false;
					}
				}

				return result;
			}

			case BlockingMode::NonBlocking: {
				SOCKET_PROFILER_INIT();
				std::lock_guard lock_guard(_dispatch_queue_lock);
				SOCKET_PROFILER_AFTER_LOCK();

				SOCKET_PROFILER_POST_HANDLER([&](int64_t lock_elapsed, int64_t total_elapsed) {
					if ((lock_elapsed > 100) || (_dispatch_queue.size() > 10))
					{
						logtw("[SockProfiler] Close() - %s, Queue: %zu, Lock: %dms, Total: %dms", ToString().CStr(), _dispatch_queue.size(), lock_elapsed, total_elapsed);
					}
				});

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
			std::lock_guard lock_guard(_dispatch_queue_lock);

			result = CloseInternal(_state);
		}

		CallCloseCallbackIfNeeded();

		return result;
	}

	bool Socket::CloseImmediatelyWithState(SocketState new_state)
	{
		bool result = false;

		{
			std::lock_guard lock_guard(_dispatch_queue_lock);

			result = CloseInternal(new_state);

			if (result)
			{
				SetState(new_state);
			}
		}

		CallCloseCallbackIfNeeded();

		return result;
	}

	Socket::DispatchResult Socket::HalfClose()
	{
		if (GetType() == SocketType::Tcp)
		{
			if (_socket.IsValid())
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

		while (true)
		{
			char buffer[MAX_BUFFER_SIZE];
			int result = ::recv(GetNativeHandle(), buffer, MAX_BUFFER_SIZE, MSG_DONTWAIT);

			if (result < 0L)
			{
				auto error = Error::CreateErrorFromErrno();

				switch (error->GetCode())
				{
					case EBADF:
						// Suppress "Bad file descriptor" message
						break;

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
		CHECK_STATE(!= SocketState::Closed, false);

		_post_callback = std::move(_callback);
		_close_reason = close_reason;

		if (_socket.IsValid())
		{
			DeleteFromWorker();
			
			switch (GetType())
			{
				case SocketType::Tcp:
					::close(GetNativeHandle());
					break;

				case SocketType::Udp:
					::close(GetNativeHandle());
					break;

				case SocketType::Srt:
					::srt_close(GetNativeHandle());
					break;

				default:
					break;
			}

			_socket.SetValid(false);

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

			logat("Socket is closed successfully");

			return true;
		}

		logat("Socket is already closed");
		OV_ASSERT(((_state == SocketState::Closed) ||
				   (_state == SocketState::Disconnected) ||
				   (_state == SocketState::Error)),
				  "Invalid state: %s", StringFromSocketState(_state));

		return false;
	}

	void Socket::CallCloseCallbackIfNeeded()
	{
		auto post_callback = std::move(_post_callback);

		if ((post_callback != nullptr) && _connection_event_fired)
		{
			_worker->EnqueueToCloseCallbackLater(GetSharedPtr(), post_callback);
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
