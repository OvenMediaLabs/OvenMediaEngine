//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#include "socket_utilities.h"

#include <base/ovlibrary/ovlibrary.h>
#include <sys/ioctl.h>

#include "socket.h"

namespace ov
{
	String GetStat(Socket socket)
	{
		ov::String stat;

		int recv_buffer_size = 0;
		int send_buffer_size = 0;
		unsigned long unread_size = 0;
		unsigned long unsent_size = 0;
		auto native_handle = socket.GetSocket().GetNativeHandle();

		switch (socket.GetType())
		{
			case SocketType::Unknown:
				stat = "(Invalid)";
				break;

			case SocketType::Udp:

				if (::ioctl(native_handle, FIONREAD, &unread_size) != -1)
				{
					stat.AppendFormat("Unread: %lu, ", unread_size);
				}
				else
				{
					stat.Append("Unread: -, ", unread_size);
				}

				if (::ioctl(native_handle, SIOCOUTQ, &unread_size) != -1)
				{
					stat.AppendFormat("Unsent: %lu, ", unread_size);
				}
				else
				{
					stat.Append("Unsent: -, ", unread_size);
				}

				// Get Recv buffer size
				if (socket.GetSockOpt(SO_RCVBUF, &recv_buffer_size))
				{
					stat.AppendFormat("Recv buffer: %d, ", recv_buffer_size);
				}
				else
				{
					stat.Append("Recv buffer: -, ");
				}

				// Get Send buffer size
				if (socket.GetSockOpt(SO_SNDBUF, &send_buffer_size))
				{
					stat.AppendFormat("Send buffer: %d", send_buffer_size);
				}
				else
				{
					stat.Append("Send buffer: -");
				}

				break;

			case SocketType::Tcp:

				if (::ioctl(native_handle, SIOCINQ, &unread_size) != -1)
				{
					stat.AppendFormat("Unread: %lu, ", unread_size);
				}
				else
				{
					stat.Append("Unread: -, ", unread_size);
				}

				if (::ioctl(native_handle, SIOCOUTQ, &unsent_size) != -1)
				{
					stat.AppendFormat("Unsent: %lu, ", unsent_size);
				}
				else
				{
					stat.Append("Unsent: -, ", unsent_size);
				}

				// Get Recv buffer size
				if (socket.GetSockOpt(SO_RCVBUF, &recv_buffer_size))
				{
					stat.AppendFormat("Recv buffer: %d, ", recv_buffer_size);
				}
				else
				{
					stat.Append("Recv buffer: -, ");
				}

				// Get Send buffer size
				if (socket.GetSockOpt(SO_SNDBUF, &send_buffer_size))
				{
					stat.AppendFormat("Send buffer: %d", send_buffer_size);
				}
				else
				{
					stat.Append("Send buffer: -");
				}

				break;

			case SocketType::Srt:
				stat = "(Not supported)";
				break;
		}

		return stat;
	}
}  // namespace ov
