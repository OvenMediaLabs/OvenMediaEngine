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

	bool DatagramSocket::Prepare(
		int port,
		SetAdditionalOptionsCallback callback,
		DatagramCallback datagram_callback)
	{
		return Prepare(SocketAddress::CreateAndGetFirst(nullptr, port), callback, std::move(datagram_callback));
	}

	bool DatagramSocket::Prepare(
		const SocketAddress &address,
		SetAdditionalOptionsCallback callback,
		DatagramCallback datagram_callback)
	{
		CHECK_STATE(== SocketState::Created, false);

		if (
			(
				MakeNonBlocking(GetSharedPtrAs<ov::SocketAsyncInterface>()) &&
				SetSocketOptions(callback) &&
				Bind(address)))
		{
			_datagram_callback = std::move(datagram_callback);

			return true;
		}

		Close();

		return false;
	}

	bool DatagramSocket::CloseInternal(SocketState close_reason)
	{
		_callback = nullptr;

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