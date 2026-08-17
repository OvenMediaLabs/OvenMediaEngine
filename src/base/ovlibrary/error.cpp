//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "error.h"

#include <errno.h>

#include "log.h"
#include "memory_utilities.h"

namespace ov
{
	Error::Error(ov::String domain)
		: _domain(std::move(domain))
	{
	}

	Error::Error(ov::String domain, int code, bool code_set, ov::String message)
		: _domain(std::move(domain)),

		  _code(code),
		  _code_set(code_set),

		  _message(std::move(message))
	{
		UpdateErrorString();
	}

	Error::Error(ov::String domain, int code)
		: Error(std::move(domain), code, true, "")
	{
	}

	Error::Error(ov::String domain, ov::String message)
		: Error(domain, 0, false, std::move(message))
	{
	}

	Error::Error(ov::String domain, const char *format, ...)
		: Error(domain)
	{
		String message;
		va_list list;
		va_start(list, format);
		message.VFormat(format, list);
		va_end(list);

		SetMessage(std::move(message));
	}

	Error::Error(ov::String domain, int code, ov::String message)
		: Error(std::move(domain), code, true, std::move(message))
	{
	}

	Error::Error(ov::String domain, int code, const char *format, ...)
		: Error(domain)
	{
		String message;
		va_list list;
		va_start(list, format);
		message.VFormat(format, list);
		va_end(list);

		SetCodeAndMessage(code, std::move(message));
	}

	std::shared_ptr<Error> Error::CreateError(ov::String domain, int code, const char *format, ...)
	{
		String message;
		va_list list;
		va_start(list, format);
		message.VFormat(format, list);
		va_end(list);

		return std::make_shared<Error>(domain, code, std::move(message));
	}

	std::shared_ptr<Error> Error::CreateError(ov::String domain, const char *format, ...)
	{
		String message;
		va_list list;
		va_start(list, format);
		message.VFormat(format, list);
		va_end(list);

		return std::make_shared<Error>(domain, std::move(message));
	}

	std::shared_ptr<Error> Error::CreateErrorFromErrno()
	{
		auto last_errno = errno;
		auto last_err_message = ::strerror(last_errno);

		return ov::Error::CreateError("errno", last_errno, "%s", last_err_message);
	}

	void Error::SetCodeAndMessage(int code, ov::String message)
	{
		_code = code;
		_code_set = true;
		_message = std::move(message);

		UpdateErrorString();
	}

	void Error::SetMessage(ov::String message)
	{
		_code_set = false;
		_message = std::move(message);

		UpdateErrorString();
	}

	void Error::UpdateErrorString()
	{
		_error_string = "";

		if (_domain.IsEmpty() == false)
		{
			_error_string.AppendFormat("[%s] ", _domain.CStr());
		}

		if (_message.IsEmpty() == false)
		{
			_error_string.AppendFormat("%s", _message.CStr());
		}
		else
		{
			_error_string.AppendFormat("(No error message)");
		}

		if (_code_set)
		{
			_error_string.AppendFormat(" (%d)", _code);
		}
	}

	int Error::GetCode() const
	{
		return _code;
	}

	String Error::GetMessage() const
	{
		return _message;
	}
}  // namespace ov
