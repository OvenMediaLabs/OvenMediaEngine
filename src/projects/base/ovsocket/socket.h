//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <netinet/in.h>
#include <netinet/tcp.h>

#include "socket_address.h"
#include "socket_address_pair.h"
#include "socket_wrapper.h"

#if !IS_MACOS
#	include <sys/epoll.h>
#	include <linux/sockios.h>
#endif	// !IS_MACOS

#include <sys/socket.h>

#include <functional>
#include <map>
#include <memory>
#include <utility>

// Failure to send data for the specified time period will be considered an error.
// For example, it can occur when EAGAIN continues to occur for a period of time, or when the peer's TCP window is full and no longer receives data.
#define OV_SOCKET_EXPIRE_TIMEOUT (10 * 1000)

namespace ov
{
	// Forward declaration
	class Socket;
	class SocketPoolWorker;

	class SocketAsyncInterface
	{
	public:
		// Called when
		//   1) A new client connected to ServerSocket
		//   2) Socket is connected to a server
		//   3) An error occurred while connecting to a server
		virtual void OnConnected(const std::shared_ptr<const SocketError> &error) = 0;
		// Data is readable (by epoll)
		virtual void OnReadable() = 0;
		// Socket is closed
		virtual void OnClosed() = 0;
	};

	class Socket : public EnableSharedFromThis<Socket>, public SocketPoolEventInterface
	{
	public:
		// Called when the server socket is created
		using SetAdditionalOptionsCallback = std::function<std::shared_ptr<ov::Error>(const std::shared_ptr<ov::Socket> &socket)>;

	protected:
		friend class SocketPoolWorker;

		OV_SOCKET_DECLARE_PRIVATE_TOKEN();

		enum class DispatchResult
		{
			// Command is dispatched
			Dispatched,
			// Command is dispatched, and some data is remained
			PartialDispatched,
			// An error occurred
			Error
		};

	public:
		// SocketPoolWorker can only be created within SocketPool
		Socket(PrivateToken token, const std::shared_ptr<SocketPoolWorker> &worker);
		Socket(PrivateToken token, const std::shared_ptr<SocketPoolWorker> &worker,
			   SocketWrapper socket, const SocketAddress &remote_address);

		// Disable copy & move operator
		Socket(const Socket &socket) = delete;
		Socket(Socket &&socket) = delete;

		~Socket() override;

		std::shared_ptr<SocketPoolWorker> GetSocketPoolWorker()
		{
			return _worker;
		}

		std::shared_ptr<const SocketPoolWorker> GetSocketPoolWorker() const
		{
			return _worker;
		}

		BlockingMode GetBlockingMode() const
		{
			return _blocking_mode;
		}

		bool MakeBlocking();
		bool MakeNonBlocking(std::shared_ptr<SocketAsyncInterface> callback);

		bool Bind(const SocketAddress &address);
		bool Listen(int backlog = SOMAXCONN);
		SocketWrapper Accept(SocketAddress *client);

		// NOTE: If Socket is used in nonblocking mode, this method always returns nullptr,
		//       and if an error occurs, OnConnected() callback passed as an argument in MakeNonBlocking() is called.
		// NOTE: The callback to connection timeout is handled by SocketPoolWorker,
		//       which can cause up to 100ms difference from timeout_msec due to the time taken by EpollWait()
		std::shared_ptr<const SocketError> Connect(const SocketAddress &endpoint, int timeout_msec = (10 * 1000));

		bool SetRecvTimeout(const timeval &tv);

		std::shared_ptr<SocketAddress> GetLocalAddress() const;
		std::shared_ptr<SocketAddress> GetRemoteAddress() const;
		String GetRemoteAddressAsUrl() const;

		// for system socket
		template <class T>
		bool SetSockOpt(int proto, int option, const T &value)
		{
			return SetSockOpt(proto, option, &value, (socklen_t)sizeof(T));
		}

		template <class T>
		bool SetSockOpt(int option, const T &value)
		{
			return SetSockOpt<T>(SOL_SOCKET, option, value);
		}

		bool SetSockOpt(int proto, int option, const void *value, socklen_t value_length);
		bool SetSockOpt(int option, const void *value, socklen_t value_length);

		template <class T>
		bool GetSockOpt(int option, T *value) const
		{
			return GetSockOpt(SOL_SOCKET, option, value, (socklen_t)sizeof(T));
		}

		bool GetSockOpt(int proto, int option, void *value, socklen_t value_length) const;

		// for SRT socket
		template <class T>
		bool SetSockOpt(SRT_SOCKOPT option, const T &value)
		{
			return SetSockOpt(option, &value, static_cast<int>(sizeof(T)));
		}

		bool SetSockOpt(SRT_SOCKOPT option, const void *value, int value_length);

		bool IsClosable() const;

		SocketState GetState() const;

		void SetState(SocketState state);

		SocketWrapper GetSocket() const
		{
			return _socket;
		}

		int GetNativeHandle() const
		{
			return _socket.GetNativeHandle();
		}

		SocketType GetType() const;
		std::shared_ptr<SocketAsyncInterface> GetAsyncInterface()
		{
			return _callback;
		}

		// only available for SRT socket
		String GetStreamId() const;

		bool Send(const std::shared_ptr<const Data> &data);
		bool Send(const void *data, size_t length);

		bool SendTo(const SocketAddress &address, const std::shared_ptr<const Data> &data);
		bool SendTo(const SocketAddress &address, const void *data, size_t length);

		bool SendFromTo(const SocketAddressPair &address_pair, const std::shared_ptr<const Data> &data);
		bool SendFromTo(const SocketAddressPair &address_pair, const void *data, size_t length);

		// When Recv is called in non-blocking mode,
		//
		// 1. return != nullptr: An error occurred (Include disconnecting the client)
		// 2. return == nullptr:
		// 2-1. data->Length() == 0: Retry later (EAGAIN)
		// 2-2. data->Length() > 0: Data is remained, so must call Recv() again to empty socket buffer
		//                          (If not, epoll event will not occur later)
		//
		// If MakeNonBlocking() is called, non_block is ignored
		std::shared_ptr<const SocketError> Recv(std::shared_ptr<Data> &data, const bool non_block = false);
		std::shared_ptr<const SocketError> Recv(void *data, size_t length, size_t *received_length, const bool non_block = false);

		// If MakeNonBlocking() is called, non_block is ignored
		std::shared_ptr<const SocketError> RecvFrom(std::shared_ptr<Data> &data, SocketAddressPair *address_pair, const bool non_block = false);

		std::chrono::system_clock::time_point GetLastRecvTime() const;
		std::chrono::system_clock::time_point GetLastSentTime() const;

		// Dispatches as many command as possible
		DispatchResult DispatchEvents();

		bool Flush();

		bool CloseIfNeeded();
		bool CloseWithState(SocketState new_state);
		bool Close();

		bool CloseImmediately();
		bool CloseImmediatelyWithState(SocketState new_state);

		bool HasCommand() const
		{
			return _dispatch_queue.size() > 0;
		}

		bool HasExpiredCommand() const
		{
			std::lock_guard lock_guard(_dispatch_queue_lock);

			if (HasCommand())
			{
				return _dispatch_queue.front().IsExpired(OV_SOCKET_EXPIRE_TIMEOUT);
			}

			return false;
		}

		bool IsEndOfStream() const
		{
			return _end_of_stream;
		}

		void SetEndOfStream()
		{
			_end_of_stream = true;
		}

		bool IsClosing() const
		{
			return _has_close_command;
		}

		virtual String ToString() const;

	protected:
		struct DispatchCommand
		{
			static constexpr int CLOSE_TYPE_MASK = 0x40;

			enum class Type : uint8_t
			{
				// Fired when a client is connected to server (ServerSocket)
				// Need to call connection callback
				Connected = 0x00,

				// Need to send data using send()
				Send = 0x01,
				// Need to send data using sendto()
				SendTo = 0x02,
				// Need to send data using sendmsg()
				SendFromTo = 0x03,

				// Need to call shutdown(SHUT_WR) (TCP only)
				HalfClose = CLOSE_TYPE_MASK | 0x01,
				// Wait for ACK/FIN
				WaitForHalfClose = CLOSE_TYPE_MASK | 0x02,
				// Need to close the socket
				Close = CLOSE_TYPE_MASK | 0x03
			};

			static const char *StringFromType(Type type)
			{
				switch (type)
				{
					case Type::Connected:
						return "Connected";

					case Type::Send:
						return "Send";

					case Type::SendTo:
						return "SendTo";

					case Type::SendFromTo:
						return "SendFromTo";

					case Type::HalfClose:
						return "HalfClose";

					case Type::WaitForHalfClose:
						return "WaitForHalfClose";

					case Type::Close:
						return "Close";
				}

				return "Unknown";
			}

			DispatchCommand(const std::shared_ptr<const Data> &data)
				: type(Type::Send),
				  data(data),
				  enqueued_time(std::chrono::system_clock::now())
			{
			}

			DispatchCommand(const SocketAddress &address, const std::shared_ptr<const Data> &data)
				: type(Type::SendTo),
				  address(address),
				  data(data),
				  enqueued_time(std::chrono::system_clock::now())
			{
			}

			DispatchCommand(const SocketAddressPair &address_pair, const std::shared_ptr<const Data> &data)
				: type(Type::SendFromTo),
				  address_pair(address_pair),
				  data(data),
				  enqueued_time(std::chrono::system_clock::now())
			{
			}

			DispatchCommand(Type type)
				: type(type),
				  enqueued_time(std::chrono::system_clock::now())
			{
			}

			DispatchCommand(Type type, SocketState new_state)
				: type(type),
				  new_state(new_state),
				  enqueued_time(std::chrono::system_clock::now())
			{
			}

			// Copy ctor
			DispatchCommand(const DispatchCommand &another_command)
				: type(another_command.type),
				  new_state(another_command.new_state),
				  address(another_command.address),
				  address_pair(another_command.address_pair),
				  data(another_command.data),
				  enqueued_time(another_command.enqueued_time)
			{
			}

			// Move ctor
			DispatchCommand(DispatchCommand &&another_command)
			{
				std::swap(type, another_command.type);
				std::swap(new_state, another_command.new_state);
				std::swap(address, another_command.address);
				std::swap(address_pair, another_command.address_pair);
				std::swap(data, another_command.data);
				std::swap(enqueued_time, another_command.enqueued_time);
			}

			bool IsCloseCommand() const
			{
				return OV_CHECK_FLAG(static_cast<uint8_t>(type), CLOSE_TYPE_MASK);
			}

			void UpdateTime()
			{
				enqueued_time = std::chrono::system_clock::now();
			}

			bool IsExpired(int millisecond_time) const
			{
				auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - enqueued_time);

				return (delta.count() >= millisecond_time);
			}

			String ToString() const
			{
				auto description = String::FormatString(
					"<DispatchCommand: %p, %s, type: %s",
					this,
					Converter::ToISO8601String(enqueued_time).CStr(),
					StringFromType(type));

				if (type == DispatchCommand::Type::SendTo)
				{
					description.AppendFormat(", address: %s", address.ToString(false).CStr());
				}

				if (type == DispatchCommand::Type::SendFromTo)
				{
					description.AppendFormat(", address_pair: %s", address_pair.ToString().CStr());
				}

				if (data != nullptr)
				{
					description.AppendFormat(", data: %zu bytes", data->GetLength());
				}

				description.Append('>');

				return description;
			}

			Type type = Type::Close;
			SocketState new_state = SocketState::Closed;
			SocketAddress address;
			SocketAddressPair address_pair;
			std::shared_ptr<const Data> data;
			std::chrono::time_point<std::chrono::system_clock> enqueued_time;
		};

	protected:
		virtual bool Create(const SocketType type, const SocketFamily family);

		// Internal version of MakeNonBlocking() - It doesn't check state
		bool MakeNonBlockingInternal(std::shared_ptr<SocketAsyncInterface> callback, bool need_to_wait_first_epoll_event);

		bool SetBlockingInternal(BlockingMode mode);

		bool AppendCommand(DispatchCommand command, bool dispatch_immediately);

		//--------------------------------------------------------------------
		// Implementation of SocketPoolEventInterface
		//--------------------------------------------------------------------
		bool OnConnectedEvent(const std::shared_ptr<const SocketError> &error) override;
		PostProcessMethod OnDataWritableEvent() override;
		void OnDataAvailableEvent() override;
		//--------------------------------------------------------------------

		DispatchResult DispatchEventInternal(DispatchCommand &command);

		bool IsSendable() const;
		ssize_t HandleSendError(const ssize_t result, const size_t total_sent);

		bool DispatchEventsAfterAppendCommand();

		ssize_t SendData(const std::shared_ptr<const Data> &data);
		ssize_t SendSrtData(const std::shared_ptr<const Data> &data);

		ssize_t SendInternal(const std::shared_ptr<const Data> &data);
		ssize_t SendToInternal(const SocketAddress &address, const std::shared_ptr<const Data> &data);
		ssize_t SendFromToInternal(const SocketAddressPair &address_pair, const std::shared_ptr<const Data> &data);

		std::shared_ptr<SocketError> RecvInternal(void *data, size_t length, size_t *received_length);

		virtual String ToString(const char *class_name) const;

		DispatchResult HalfClose();
		DispatchResult WaitForHalfClose();

		// CloseInternal() doesn't call the _callback directly
		// So, we need to call CallCloseCallbackIfNeeded() after calling this api to do connection callback
		virtual bool CloseInternal(SocketState close_reason);

		// Since the resource is usually cleaned inside the OnClosed() callback,
		// callback is performed outside the lock_guard to prevent acquiring the lock.
		void CallCloseCallbackIfNeeded();

	protected:
		std::shared_ptr<const SocketError> DoConnectionCallback(const std::shared_ptr<const SocketError> &error);

		// ClientSocket doesn't need to wait the first epoll event
		bool AddToWorker(bool need_to_wait_first_epoll_event);
		bool DeleteFromWorker();

		// When using epoll ET mode, the first event occurs immediately after EPOLL_CTL_ADD. (except SRT epoll)
		// This API is used for waiting the event, and MUST be called in SocketPoolWorker::ThreadProc() thread
		bool NeedToWaitFirstEpollEvent() const
		{
			if (_socket.GetType() == SocketType::Srt)
			{
				return false;
			}

			return _need_to_wait_first_epoll_event;
		}

		// true == Event is raised
		// false == Timed out
		bool WaitForFirstEpollEvent()
		{
			return _first_epoll_event_received.Wait();
		}

		// This API MUST be called in SocketPoolWorker::ThreadProc() thread
		bool SetFirstEpollEventReceived()
		{
			_need_to_wait_first_epoll_event = false;
			_first_epoll_event_received.SetEvent();

			return true;
		}

		bool ResetFirstEpollEventReceived()
		{
			_need_to_wait_first_epoll_event = true;
			_first_epoll_event_received.Reset();

			return true;
		}

		DispatchResult DispatchEventsInternal();

	protected:
		std::shared_ptr<SocketPoolWorker> _worker;

		SocketWrapper _socket;
		SocketFamily _family;

		mutable std::mutex _state_mutex;
		SocketState _state = SocketState::Closed;

		BlockingMode _blocking_mode = BlockingMode::Blocking;

		std::mutex _worker_mutex;
		bool _added_to_worker = false;

		std::atomic<bool> _need_to_wait_first_epoll_event{true};
		Event _first_epoll_event_received{true};

		bool _end_of_stream = false;

		std::shared_ptr<SocketAddress> _local_address = nullptr;
		std::shared_ptr<SocketAddress> _remote_address = nullptr;

		mutable std::recursive_mutex _dispatch_queue_lock;
		std::deque<DispatchCommand> _dispatch_queue;
		bool _has_close_command = false;

		std::atomic<bool> _connection_event_fired{false};
		std::shared_ptr<SocketAsyncInterface> _callback;

		// A temporary variable used to send callback without mutex lock
		std::shared_ptr<SocketAsyncInterface> _post_callback;
		SocketState _close_reason = SocketState::Closed;

		volatile bool _force_stop = false;

		String _stream_id;	// only available for SRT socket

	private:
		void UpdateLastRecvTime();
		void UpdateLastSentTime();

		std::chrono::system_clock::time_point _last_recv_time = std::chrono::system_clock::now();
		std::chrono::system_clock::time_point _last_sent_time = std::chrono::system_clock::now();
	};
}  // namespace ov
