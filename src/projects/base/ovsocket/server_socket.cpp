//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "server_socket.h"

#include <netinet/tcp.h>

#include "client_socket.h"
#include "socket_pool/socket_pool.h"
#include "socket_pool/socket_pool_worker.h"
#include "socket_private.h"

#undef OV_LOG_TAG
#define OV_LOG_TAG "Socket.Server"

#define OV_LOG_PREFIX_FORMAT "[#%d] [%p] "
#define OV_LOG_PREFIX_VALUE (GetNativeHandle() == InvalidSocket) ? 0 : GetNativeHandle(), this

namespace ov
{
	ServerSocket::ServerSocket(PrivateToken token, const std::shared_ptr<SocketPoolWorker> &worker, const std::shared_ptr<SocketPool> &pool)
		: Socket(token, worker),

		  _pool(pool)
	{
	}

	ServerSocket::~ServerSocket()
	{
	}

	bool ServerSocket::SetSocketOptions(int send_buffer_size, int recv_buffer_size, SetAdditionalOptionsCallback callback)
	{
		// SRT socket is already non-block mode
		bool result = true;

		switch (GetType())
		{
			case SocketType::Tcp: {
				result &= SetSockOpt<int>(SO_REUSEADDR, 1);

				// Disable Nagle's algorithm
				result &= SetSockOpt<int>(IPPROTO_TCP, TCP_NODELAY, 1);

				// Set send buffer size if smaller than argument (0 == default)
				int current_send_buffer_size = 0;
				GetSockOpt(SO_SNDBUF, &current_send_buffer_size);
				if ((send_buffer_size != 0) && (current_send_buffer_size < send_buffer_size))
				{
					// We need to divide by 2, because the return value of SO_SNDBUF always (input value * 2)
					result &= SetSockOpt<int>(SO_SNDBUF, send_buffer_size / 2);

					GetSockOpt(SO_SNDBUF, &current_send_buffer_size);

					logat("TCP Server send buffer size (requested: %d, from socket: %d)", send_buffer_size * 2, current_send_buffer_size);
				}

				// Set recv buffer size if smaller than argument (0 == default)
				int current_recv_buffer_size = 0;
				GetSockOpt(SO_RCVBUF, &current_recv_buffer_size);
				if ((recv_buffer_size != 0) && (current_recv_buffer_size < recv_buffer_size))
				{
					// We need to divide by 2, because the return value of SO_RCVBUF always (input value * 2)
					result &= SetSockOpt<int>(SO_RCVBUF, recv_buffer_size / 2);

					GetSockOpt(SO_RCVBUF, &current_recv_buffer_size);

					logat("TCP Server recv buffer size (requested: %d, from socket: %d)", recv_buffer_size * 2, current_recv_buffer_size);
				}
				break;
			}

			case SocketType::Udp:
				OV_ASSERT2(false);
				break;

			case SocketType::Srt:
				// https://github.com/Haivision/srt/blob/master/docs/API.md

				// OptName	        Since	Binding	    Type	    Units	    Default	    Range
				// SRTO_TRANSTYPE	1.3.0	pre	        enum		            SRTT_LIVE	alt: SRTT_FILE
				//
				// [SET] - Sets the transmission type for the socket, in particular,
				// setting this option sets multiple other parameters to their default values as required for a particular transmission type.
				//     SRTT_LIVE: Set options as for live transmission.
				//                In this mode, you should send by one sending instruction only so many data
				//                that fit in one UDP packet, and limited to the value defined first in SRTO_PAYLOADSIZE option
				//                (1316 is default in this mode). There is no speed control in this mode, only the bandwidth control, if configured, in order to not exceed the bandwidth with the overhead transmission (retransmitted and control packets).
				//     SRTT_FILE: Set options as for non-live transmission.
				//                See SRTO_MESSAGEAPI for further explanations
				result &= SetSockOpt<SRT_TRANSTYPE>(SRTO_TRANSTYPE, SRTT_LIVE);

				// OptName	        Since	Binding	    Type	    Units	    Default	    Range
				// SRTO_RCVSYN		        pre	        bool    	true	    true	    false
				//
				// [GET or SET] - Synchronous (blocking) receive mode
				result &= SetSockOpt<bool>(SRTO_RCVSYN, false);

				// OptName	        Since	Binding	    Type	    Units	    Default	    Range
				// SRTO_SNDSYN		        post	    bool	    true	    true	    false
				//
				// [GET or SET] - Synchronous (blocking) send mode
				result &= SetSockOpt<bool>(SRTO_SNDSYN, false);

				// OptName          Since	Binding	    Type	    Units	    Default	    Range
				// SRTO_REUSEADDR	        pre			true	                true        false
				//
				// When true, allows the SRT socket use the binding address used already by another SRT socket in the same application.
				// Note that SRT socket use an intermediate object to access the underlying UDP sockets called Multiplexer,
				// so multiple SRT socket may share one UDP socket and the packets received to this UDP socket
				// will be correctly dispatched to the SRT socket to which they are currently destined.
				// This has some similarities to SO_REUSEADDR system socket option, although it's only used inside SRT.
				//
				// TODO: This option weirdly only allows the socket used in bind() to use the local address
				// that another socket is already using, but not to disallow another socket in the same application
				// to use the binding address that the current socket is already using.
				// What it actually changes is that when given address in bind() is already used by another socket,
				// this option will make the binding fail instead of making the socket added to the shared group of that
				// socket that already has bound this address - but it will not disallow another socket reuse its address.
				result &= SetSockOpt<bool>(SRTO_REUSEADDR, true);
				break;

			default:
				result = false;
		}

		return result &&
			   // Call the callback function if it is set
			   ((callback == nullptr) || (callback(GetSharedPtrAs<Socket>()) == nullptr));
	}

	bool ServerSocket::Prepare(uint16_t port,
							   SetAdditionalOptionsCallback callback,
							   ClientConnectionCallback connection_callback,
							   ClientDataCallback data_callback,
							   int send_buffer_size, int recv_buffer_size,
							   int backlog)
	{
		return Prepare(SocketAddress::CreateAndGetFirst(nullptr, port),
					   callback,
					   connection_callback,
					   data_callback,
					   send_buffer_size, recv_buffer_size,
					   backlog);
	}

	bool ServerSocket::Prepare(const SocketAddress &address,
							   SetAdditionalOptionsCallback callback,
							   ClientConnectionCallback connection_callback,
							   ClientDataCallback data_callback,
							   int send_buffer_size, int recv_buffer_size,
							   int backlog)
	{
		CHECK_STATE(== SocketState::Created, false);

		if ((MakeNonBlocking(GetSharedPtrAs<SocketAsyncInterface>()) &&
			 SetSocketOptions(send_buffer_size, recv_buffer_size, callback) &&
			 Bind(address) &&
			 Listen(backlog)))
		{
			_connection_callback = connection_callback;
			_data_callback = data_callback;

			return true;
		}

		Close();

		return false;
	}

	void ServerSocket::AcceptClients()
	{
		while (true)
		{
			SocketAddress remote_address;
			SocketWrapper client_socket = Socket::Accept(&remote_address);

			if (client_socket.IsValid() == false)
			{
				// There is no client to accept
				return;
			}

			logat("Trying to allocate a socket for client: %s", remote_address.ToString(false).CStr());

			auto client = _pool->AllocSocket<ClientSocket>(remote_address.GetFamily(), GetSharedPtrAs<ServerSocket>(), client_socket, remote_address);

			if (client != nullptr)
			{
				auto key = client.get();

				// An event occurs the moment client socket is added to the epoll,
				// so we must add the client into _client_list before calling Prepare()
				{
					std::lock_guard lock_guard(_client_list_mutex);
					_client_list[key] = client;
				}

				if (client->Prepare())
				{
					logat("Client(%s) is connected", client->ToString().CStr());
				}
				else
				{
					{
						std::lock_guard lock_guard(_client_list_mutex);
						_client_list.erase(key);
					}

					logae("Client(%s) is connected, but could not prepare socket options", client->ToString().CStr());

					client->CloseImmediately();
				}
			}
		}
	}

	bool ServerSocket::OnClientDisconnected(const std::shared_ptr<ClientSocket> &client)
	{
		std::lock_guard lock_guard(_client_list_mutex);

		auto item = _client_list.find(client.get());

		if (item == _client_list.end())
		{
			return false;
		}

		logat("Client(%s) is disconnected", client->ToString().CStr());

		_client_list.erase(item);

		return true;
	}

	void ServerSocket::OnReadable()
	{
		AcceptClients();
	}

	bool ServerSocket::CloseInternal(SocketState close_reason)
	{
		_client_list_mutex.lock();
		auto client_list = std::move(_client_list);
		_client_list_mutex.unlock();

		for (const auto &client : client_list)
		{
			client.second->Close();
		}

		_callback = nullptr;

		if (Socket::CloseInternal(close_reason))
		{
			SetState(SocketState::Closed);
			return true;
		}

		return false;
	}

	String ServerSocket::ToString() const
	{
		return Socket::ToString("ServerSocket");
	}
}  // namespace ov
