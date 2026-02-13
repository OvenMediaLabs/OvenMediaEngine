//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "web_socket_response.h"

#include <unistd.h>
#include <algorithm>

#include "./web_socket_private.h"
#include "../../protocol/web_socket/web_socket_frame.h"

namespace http
{
	namespace svr
	{
		namespace ws
		{
			WebSocketResponse::WebSocketResponse(const std::shared_ptr<HttpResponse> &http_respose)
				: HttpResponse(http_respose)
			{
			}

			WebSocketResponse::~WebSocketResponse()
			{
			}

			ssize_t WebSocketResponse::Send(const std::shared_ptr<const ov::Data> &data, prot::ws::FrameOpcode opcode)
			{
				// RFC6455 - 5.2.  Base Framing Protocol
				//
				//
				prot::ws::FrameHeader header{
					.opcode = static_cast<uint8_t>(opcode),
					.reserved = 0x00,
					.fin = true,
					.payload_length = 0,
					.mask = false};

				size_t length = (data == nullptr) ? 0LL : data->GetLength();

				if (length < 0x7D)
				{
					// frame-payload-length    = ( %x00-7D )
					//                         / ( %x7E frame-payload-length-16 )
					//                         / ( %x7F frame-payload-length-63 )
					//                         ; 7, 7+16, or 7+64 bits in length,
					//                         ; respectively
					header.payload_length = static_cast<uint8_t>(length);
				}
				else if (length < 0xFFFF)
				{
					// frame-payload-length-16 = %x0000-FFFF ; 16 bits in length
					header.payload_length = 126;
				}
				else
				{
					// frame-payload-length-63 = %x0000000000000000-7FFFFFFFFFFFFFFF
					//                         ; 64 bits in length
					header.payload_length = 127;
				}

				auto response_data = std::make_shared<ov::Data>(65535);

				response_data->Append(&header, sizeof(header));

				if (header.payload_length == 126)
				{
					auto payload_length = ov::HostToNetwork16(static_cast<uint16_t>(length));
					response_data->Append(&payload_length, sizeof(payload_length));
				}
				else if (header.payload_length == 127)
				{
					auto payload_length = ov::HostToNetwork64(static_cast<uint64_t>(length));
					response_data->Append(&payload_length, sizeof(payload_length));
				}

				if (length > 0LL)
				{
					logtt("Trying to send data\n%s", data->Dump(32).CStr());
					response_data->Append(data);
				}

				return HttpResponse::Send(response_data) ? length : -1LL;
			}

			ssize_t WebSocketResponse::Send(const ov::String &string)
			{
				return Send(string.ToData(false), prot::ws::FrameOpcode::Text);
			}

			ssize_t WebSocketResponse::Send(const Json::Value &value)
			{
				return Send(ov::Json::Stringify(value));
			}
		}  // namespace ws
	}	   // namespace svr
}  // namespace http
