//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "socket.h"
#include "socket_datastructure.h"

namespace ov
{
	class DatagramSocket : public Socket, public SocketAsyncInterface
	{
	public:
		DatagramSocket(PrivateToken token, const std::shared_ptr<SocketPoolWorker> &worker)
			: Socket(token, worker)
		{
		}

		~DatagramSocket() override = default;

		// Bind to a specific port
		bool Prepare(int port,
					 SetAdditionalOptionsCallback callback,
					 DatagramCallback datagram_callback);
		// Bind to the address specified by address
		bool Prepare(const SocketAddress &address,
					 SetAdditionalOptionsCallback callback,
					 DatagramCallback datagram_callback);

		using Socket::Close;
		using Socket::Connect;
		using Socket::GetState;
		using Socket::Recv;
		using Socket::RecvFrom;
		using Socket::Send;
		using Socket::SendTo;

		String ToString() const override;

	protected:
		bool SetSocketOptions(SetAdditionalOptionsCallback callback);

		//--------------------------------------------------------------------
		// Overriding of Socket
		//--------------------------------------------------------------------
		bool CloseInternal(SocketState close_reason) override;

		//--------------------------------------------------------------------
		// Implementation of SocketAsyncInterface
		//--------------------------------------------------------------------
		void OnConnected(const std::shared_ptr<const SocketError> &error) override
		{
			// datagram socket should not be called this event
			OV_ASSERT2(false);
		}
		void OnReadable() override;
		void OnClosed() override
		{
			// datagram socket should not be called this event
			OV_ASSERT2(false);
		}

		DatagramCallback _datagram_callback = nullptr;
	};
}  // namespace ov
