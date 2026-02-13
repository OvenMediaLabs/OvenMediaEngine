//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "./port.h"

namespace cfg
{
	namespace cmn
	{
		struct SingularPort : public Port<int>
		{
		public:
			SingularPort() = default;
			explicit SingularPort(const char *port)
				: Port(port)
			{
				SetPort(port);
			}

		protected:
			MAY_THROWS(cfg::ConfigError)
			void SetPort(const ov::String &str)
			{
				_port		 = str;
				_socket_type = ov::SocketType::Unknown;
				_port_value	 = ov::Converter::ToInt32(_port.Trim());

				if (IsValidPort(_port_value))
				{
					auto tokens	 = _port.Split("/");

					// Default: TCP
					_socket_type = (tokens.size() != 2) ? ov::SocketType::Tcp : GetSocketType(tokens[1]);

					if (_socket_type == ov::SocketType::Unknown)
					{
						throw CreateConfigError("Unknown socket type: %s", str.CStr());
					}

					return;
				}

				throw CreateConfigError("Invalid port: %s", str.CStr());
			}

			MAY_THROWS(cfg::ConfigError)
			void FromString(const ov::String &str) override
			{
				SetPort(str);
			}
		};
	}  // namespace cmn
}  // namespace cfg
