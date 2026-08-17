//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/converter.h>
#include <base/ovsocket/socket.h>

#include "../../engine/item.h"

namespace cfg
{
	namespace cmn
	{
		template <typename Tport = int>
		struct Port : public Text
		{
		protected:
			ov::String _port;

			Tport _port_value{};
			ov::SocketType _socket_type = ov::SocketType::Unknown;

		public:
			Port() = default;
			explicit Port(const char *port)
				: _port(port)
			{
			}

			CFG_DECLARE_VIRTUAL_CONST_REF_GETTER_OF(GetPort, _port_value)
			CFG_DECLARE_VIRTUAL_CONST_REF_GETTER_OF(GetPortString, _port)
			CFG_DECLARE_VIRTUAL_CONST_REF_GETTER_OF(GetSocketType, _socket_type)

			ov::String ToString() const override
			{
				return _port;
			}

		protected:
			static ov::SocketType GetSocketType(const ov::String &type)
			{
				ov::String uppercase_type = type.Trim().UpperCaseString();

				if (uppercase_type == "TCP")
				{
					return ov::SocketType::Tcp;
				}
				else if (uppercase_type == "UDP")
				{
					return ov::SocketType::Udp;
				}
				else if (uppercase_type == "SRT")
				{
					return ov::SocketType::Srt;
				}

				return ov::SocketType::Unknown;
			}

			static bool IsValidPort(int port)
			{
				return (port > 0) && (port <= 65535);
			}
		};
	}  // namespace cmn
}  // namespace cfg