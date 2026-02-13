#include <utility>

//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <base/ovsocket/ovsocket.h>

#include <variant>

#include "../../protocol/web_socket/web_socket_frame.h"
#include "../http_response.h"
#include "web_socket_datastructure.h"

namespace http
{
	namespace svr
	{
		namespace ws
		{
			class WebSocketResponse : public HttpResponse
			{
			public:
				// Only can be created by upgrade
				WebSocketResponse(const std::shared_ptr<HttpResponse> &http_respose);
				virtual ~WebSocketResponse();

				ssize_t Send(const std::shared_ptr<const ov::Data> &data, prot::ws::FrameOpcode opcode);
				ssize_t Send(const ov::String &string);
				ssize_t Send(const Json::Value &value);

			protected:
				using HttpResponse::Send;
			};
		}  // namespace ws
	}	   // namespace svr
}  // namespace http
