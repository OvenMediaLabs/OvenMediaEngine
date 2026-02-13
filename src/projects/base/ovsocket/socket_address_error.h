//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../ovlibrary/error.h"

#define SOCKET_ADDRESS_ERROR_DOMAIN "SocketAddress"

namespace ov
{
	class SocketAddressError : public Error
	{
	public:
		SocketAddressError(const char *message)
			: Error(SOCKET_ADDRESS_ERROR_DOMAIN, message)
		{
		}

		template <typename... Args>
		SocketAddressError(const char *format, Args... args)
			: Error(SOCKET_ADDRESS_ERROR_DOMAIN, String::FormatString(format, args...))
		{
		}

		SocketAddressError(const std::shared_ptr<Error> cause, const char *message)
			: Error(SOCKET_ADDRESS_ERROR_DOMAIN, cause->GetCode())
		{
			SetMessage(cause, message);
		}

		template <typename... Args>
		SocketAddressError(const std::shared_ptr<Error> cause, const char *format, Args... args)
			: Error(SOCKET_ADDRESS_ERROR_DOMAIN, cause->GetCode())
		{
			SetMessage(cause, String::FormatString(format, args...));
		}

	protected:
		void SetMessage(const std::shared_ptr<Error> cause, ov::String message)
		{
			if (message.IsEmpty())
			{
				message = cause->What();
			}
			else
			{
				message.AppendFormat(" (Caused by %s)", cause->What());
			}

			Error::SetMessage(std::move(message));
		}
	};
}  // namespace ov