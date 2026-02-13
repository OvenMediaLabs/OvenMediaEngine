//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <arpa/inet.h>
#include <base/ovlibrary/ovlibrary.h>
#include <srt/srt.h>
#include <sys/epoll.h>

#include <functional>

#include "epoll_wrapper.h"
#include "socket_error.h"
#include "srt_error.h"

#define OV_SOCKET_ADD_FLAG_IF(list, x, flag) \
	if (OV_CHECK_FLAG(x, flag))              \
	{                                        \
		list.emplace_back(#flag);            \
	}

#define OV_SOCKET_DECLARE_PRIVATE_TOKEN()   \
	struct PrivateToken                     \
	{                                       \
		explicit PrivateToken(const void *) \
		{                                   \
		}                                   \
	}

namespace ov
{
	// socket type
	typedef int socket_t;

	// A socket value when not initialized
	const int InvalidSocket = -1;
	static_assert(SRT_INVALID_SOCK == InvalidSocket, "SRT_INVALID_SOCK must be equal to InvalidSocket");

	constexpr const int EpollMaxEvents = 1024;
	constexpr const int MaxSrtPacketSize = 1316;

	const ssize_t TcpBufferSize = 4096;
	const ssize_t UdpBufferSize = 4096;

	enum class SocketConnectionState : int8_t
	{
		/// Socket is connected
		Connected,
		/// Disconnected by server
		Disconnect,
		/// Disconnected from client
		Disconnected,
		/// An error occurred
		Error
	};

	enum class BlockingMode : char
	{
		Blocking,
		NonBlocking
	};

	enum class SocketFamily : sa_family_t
	{
		Unknown = AF_UNSPEC,

		Inet = AF_INET,
		Inet6 = AF_INET6,
	};

	enum class SocketType : char
	{
		Unknown,
		Udp,
		Tcp,
		Srt
	};

	constexpr const int SOCKET_STATE_CLOSABLE = 0x01000000;

	enum class SocketState : int
	{
		// Socket was closed
		Closed = 0,
		// Socket is created
		Created = 1 | SOCKET_STATE_CLOSABLE,
		// Bound on some port
		Bound = 2 | SOCKET_STATE_CLOSABLE,
		// Listening
		Listening = 3 | SOCKET_STATE_CLOSABLE,
		// Connecting
		Connecting = 4 | SOCKET_STATE_CLOSABLE,
		// Connection established
		Connected = 5 | SOCKET_STATE_CLOSABLE,
		// The connection with Peer has been lost (However, we can read data from the kernel socket buffer if available)
		Disconnected = 6,
		// An error occurred
		Error = 7,
	};

	static const char *StringFromBlockingMode(BlockingMode mode)
	{
		switch (mode)
		{
			case BlockingMode::Blocking:
				return "Blocking";

			case BlockingMode::NonBlocking:
				return "Nonblocking";
		}

		return "Unknown";
	}

	constexpr static const char *StringFromSocketState(SocketState state)
	{
		switch (state)
		{
			case SocketState::Closed:
				return "Closed";

			case SocketState::Created:
				return "Created";

			case SocketState::Bound:
				return "Bound";

			case SocketState::Listening:
				return "Listening";

			case SocketState::Connecting:
				return "Connecting";

			case SocketState::Connected:
				return "Connected";

			case SocketState::Disconnected:
				return "Disconnected";

			case SocketState::Error:
				return "Error";

			default:
				return "Unknown";
		}
	}

	static const char *StringFromSocketType(SocketType type)
	{
		switch (type)
		{
			case SocketType::Udp:
				return "UDP";

			case SocketType::Tcp:
				return "TCP";

			case SocketType::Srt:
				return "SRT";

			case SocketType::Unknown:
			default:
				return "Unknown";
		}
	}

	static const char *StringFromSocketFamily(SocketFamily family)
	{
		switch (family)
		{
			case SocketFamily::Inet:
				return "Inet";

			case SocketFamily::Inet6:
				return "Inet6";

			case SocketFamily::Unknown:
			default:
				return "Unknown";
		}
	}

	class SocketAddressPair;

	// For SocketPoolWorker callback
	enum class PostProcessMethod
	{
		// Nothing to do
		Nothing,

		// Do garbage collection
		GarbageCollection,

		// An error occurred - close the socket immediately
		Error
	};

	class SocketPoolEventInterface
	{
	public:
		virtual bool OnConnectedEvent(const std::shared_ptr<const SocketError> &error) = 0;
		virtual PostProcessMethod OnDataWritableEvent() = 0;
		virtual void OnDataAvailableEvent() = 0;
	};

	// For TCP sockets
	class ServerSocket;
	class ClientSocket;

	typedef std::function<void(const std::shared_ptr<ClientSocket> &client, SocketConnectionState state, const std::shared_ptr<Error> &error)> ClientConnectionCallback;
	typedef std::function<void(const std::shared_ptr<ClientSocket> &client, const std::shared_ptr<Data> &data)> ClientDataCallback;

	// For UDP sockets
	class DatagramSocket;

	typedef std::function<void(const std::shared_ptr<DatagramSocket> &client, const SocketAddressPair &address_pair, const std::shared_ptr<Data> &data)> DatagramCallback;

	static String StringFromEpollEvent(const epoll_event &event)
	{
		std::vector<String> flags;

		OV_SOCKET_ADD_FLAG_IF(flags, event.events, EPOLLIN);
		OV_SOCKET_ADD_FLAG_IF(flags, event.events, EPOLLPRI);
		OV_SOCKET_ADD_FLAG_IF(flags, event.events, EPOLLOUT);
		OV_SOCKET_ADD_FLAG_IF(flags, event.events, EPOLLRDNORM);
		OV_SOCKET_ADD_FLAG_IF(flags, event.events, EPOLLRDBAND);
		OV_SOCKET_ADD_FLAG_IF(flags, event.events, EPOLLWRNORM);
		OV_SOCKET_ADD_FLAG_IF(flags, event.events, EPOLLWRBAND);
		OV_SOCKET_ADD_FLAG_IF(flags, event.events, EPOLLMSG);
		OV_SOCKET_ADD_FLAG_IF(flags, event.events, EPOLLERR);
		OV_SOCKET_ADD_FLAG_IF(flags, event.events, EPOLLHUP);
		OV_SOCKET_ADD_FLAG_IF(flags, event.events, EPOLLRDHUP);
		OV_SOCKET_ADD_FLAG_IF(flags, event.events, EPOLLWAKEUP);
		OV_SOCKET_ADD_FLAG_IF(flags, event.events, EPOLLONESHOT);
		OV_SOCKET_ADD_FLAG_IF(flags, event.events, EPOLLET);

		return String::Join(flags, " | ");
	}

	static String StringFromEpollEvent(const epoll_event *event)
	{
		return StringFromEpollEvent(*event);
	}
}  // namespace ov
