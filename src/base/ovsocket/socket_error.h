//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

namespace ov
{
	class SocketError : public Error
	{
	public:
		SocketError(const ov::String &message)
			: Error("Socket", message)
		{
		}

		SocketError(int code, const ov::String &message)
			: Error("Socket", code, message)
		{
		}

		static std::shared_ptr<SocketError> CreateError(int code, const char *format, ...)
		{
			ov::String message;
			va_list list;
			va_start(list, format);
			message.VFormat(format, list);
			va_end(list);

			return std::make_shared<SocketError>(code, message);
		}

		static std::shared_ptr<SocketError> CreateError(const char *format, ...)
		{
			ov::String message;
			va_list list;
			va_start(list, format);
			message.VFormat(format, list);
			va_end(list);

			return std::make_shared<SocketError>(message);
		}

		static std::shared_ptr<SocketError> CreateError(const std::shared_ptr<Error> &error)
		{
			if (error == nullptr)
			{
				return nullptr;
			}

			auto http_error = std::dynamic_pointer_cast<SocketError>(error);

			if (http_error != nullptr)
			{
				return http_error;
			}

			return CreateError(error->GetCode(), "%s", error->What());
		}
	};
}  // namespace ov
