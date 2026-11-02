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
		//
		// `send_buffer_size` / `recv_buffer_size` are applied as `SO_SNDBUF` / `SO_RCVBUF` (in bytes).
		// `0` keeps the kernel default (`net.core.wmem_default` / `net.core.rmem_default`).
		bool Prepare(int port,
					 SetAdditionalOptionsCallback callback,
					 DatagramCallback datagram_callback,
					 int send_buffer_size = 0,
					 int recv_buffer_size = 0);
		// Bind to the address specified by address
		bool Prepare(const SocketAddress &address,
					 SetAdditionalOptionsCallback callback,
					 DatagramCallback datagram_callback,
					 int send_buffer_size = 0,
					 int recv_buffer_size = 0);

		// Grows `SO_SNDBUF` / `SO_RCVBUF` to what the kernel grants for the given sizes (bytes). `0` leaves
		// the respective buffer alone, and a buffer is never shrunk, so the largest request wins regardless
		// of the order in which the requests arrive. Can be called after `Prepare()`.
		bool SetBufferSizes(int send_buffer_size, int recv_buffer_size);

		// The `SO_SNDBUF` / `SO_RCVBUF` value the kernel reports for this socket (`0` if unavailable)
		int GetSendBufferSize() const;
		int GetRecvBufferSize() const;

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
		bool SetBufferSize(int option, int requested_size);

		//--------------------------------------------------------------------
		// Overriding of Socket
		//--------------------------------------------------------------------
		bool CloseInternal(SocketState close_reason) override OV_REQUIRES(_dispatch_queue_lock);

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
