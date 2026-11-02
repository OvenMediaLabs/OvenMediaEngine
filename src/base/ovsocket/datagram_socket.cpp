//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#include "datagram_socket.h"

#include "client_socket.h"
#include "socket_private.h"

#undef OV_LOG_TAG
#define OV_LOG_TAG "Socket.Datagram"

#define OV_LOG_PREFIX_FORMAT "[%p] "
#define OV_LOG_PREFIX_VALUE this

namespace ov
{
	bool DatagramSocket::SetSocketOptions(SetAdditionalOptionsCallback callback)
	{
		switch (GetType())
		{
			case SocketType::Tcp:
				[[fallthrough]];
			case SocketType::Srt:
				[[fallthrough]];
			default:
				// DatagramSocket should not be created with TCP or SRT
				OV_ASSERT2(false);
				return false;

			case SocketType::Udp:
				break;
		}

		auto result = SetSockOpt<int>(SO_REUSEADDR, 1);

		return result &&
			   // Call the callback function if it is set
			   ((callback == nullptr) || (callback(GetSharedPtrAs<Socket>()) == nullptr));
	}

	// Applies `SO_SNDBUF` or `SO_RCVBUF` (`option`) and reads back what the kernel actually granted.
	//
	// Linux doubles the requested value (to account for bookkeeping overhead) and caps it at
	// `net.core.wmem_max` / `net.core.rmem_max`, so the value read back is `2 * min(requested, *mem_max)`.
	// A read-back value below the request therefore means the sysctl cap got in the way: the socket was
	// granted less than half of what was asked for. The caller (`PhysicalPort`) reports that once per port
	// instead of once per `SO_REUSEPORT` socket, so this only logs at debug level.
	//
	// A buffer size is a tuning hint, so a failure here is logged but does not fail `Prepare()`.
	bool DatagramSocket::SetBufferSize(int option, int requested_size)
	{
		const char *option_name = (option == SO_SNDBUF) ? "SO_SNDBUF" : "SO_RCVBUF";

		if (requested_size <= 0)
		{
			// 0 == keep the kernel default
			return true;
		}

		int current_size = 0;

		if (GetSockOpt(option, &current_size) && (current_size >= requested_size))
		{
			logad("%s is already %d bytes (requested: %d bytes)", option_name, current_size, requested_size);
			return true;
		}

		if (SetSockOpt<int>(option, requested_size) == false)
		{
			logaw("Could not set %s to %d bytes (current: %d bytes)", option_name, requested_size, current_size);
			return false;
		}

		int effective_size = 0;

		if (GetSockOpt(option, &effective_size) == false)
		{
			logaw("%s was set to %d bytes, but the effective size could not be read back", option_name, requested_size);
			return false;
		}

		logad("%s set to %d bytes (requested: %d bytes, previous: %d bytes)", option_name, effective_size, requested_size, current_size);

		return true;
	}

	bool DatagramSocket::SetBufferSizes(int send_buffer_size, int recv_buffer_size)
	{
		// Apply both even if one of them fails - they are independent
		bool send_result = SetBufferSize(SO_SNDBUF, send_buffer_size);
		bool recv_result = SetBufferSize(SO_RCVBUF, recv_buffer_size);

		return send_result && recv_result;
	}

	int DatagramSocket::GetSendBufferSize() const
	{
		int size = 0;
		return GetSockOpt(SO_SNDBUF, &size) ? size : 0;
	}

	int DatagramSocket::GetRecvBufferSize() const
	{
		int size = 0;
		return GetSockOpt(SO_RCVBUF, &size) ? size : 0;
	}

	bool DatagramSocket::Prepare(
		int port,
		SetAdditionalOptionsCallback callback,
		DatagramCallback datagram_callback,
		int send_buffer_size,
		int recv_buffer_size)
	{
		return Prepare(SocketAddress::CreateAndGetFirst(nullptr, port), callback, std::move(datagram_callback), send_buffer_size, recv_buffer_size);
	}

	bool DatagramSocket::Prepare(
		const SocketAddress &address,
		SetAdditionalOptionsCallback callback,
		DatagramCallback datagram_callback,
		int send_buffer_size,
		int recv_buffer_size)
	{
		CHECK_STATE(== SocketState::Created, false);

		if (
			(
				MakeNonBlocking(GetSharedPtrAs<ov::SocketAsyncInterface>()) &&
				SetSocketOptions(callback) &&
				Bind(address)))
		{
			// Buffer sizes are tuning hints: a failure is logged inside `SetBufferSizes()` but must not
			// take the port down, so the result is intentionally not checked here.
			SetBufferSizes(send_buffer_size, recv_buffer_size);

			_datagram_callback = std::move(datagram_callback);

			return true;
		}

		Close();

		return false;
	}

	bool DatagramSocket::CloseInternal(SocketState close_reason)
	{
		std::atomic_store(&_callback, {});

		if (Socket::CloseInternal(close_reason))
		{
			SetState(SocketState::Closed);
			return true;
		}

		return false;
	}

	void DatagramSocket::OnReadable()
	{
		logtp("Trying to read UDP packets...");

		auto data = std::make_shared<ov::Data>(UdpBufferSize);

		SocketAddressPair address_pair;

		while (true)
		{
			auto error = RecvFrom(data, &address_pair);

			if (error == nullptr)
			{
				if (data->GetLength() == 0L)
				{
					// Try later
					break;
				}
				else
				{
					if (_datagram_callback != nullptr)
					{
						_datagram_callback(GetSharedPtrAs<DatagramSocket>(), address_pair, data->Clone());
					}
				}
			}
			else
			{
				// An error occurred
				break;
			}
		}
	}

	String DatagramSocket::ToString() const
	{
		return Socket::ToString("DatagramSocket");
	}
}  // namespace ov