//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <memory>

#include "./string.h"

// Just a hint of what exception is thrown
#define MAY_THROWS(...)

namespace ov
{
	class Error : public std::exception
	{
	public:
		Error(const Error &error) = default;

		// This indicates no error
		Error() = default;
		Error(ov::String domain, int code);
		Error(ov::String domain, ov::String message);
		Error(ov::String domain, const char *format, ...);
		Error(ov::String domain, int code, ov::String message);
		Error(ov::String domain, int code, const char *format, ...);

		static std::shared_ptr<Error> CreateError(ov::String domain, int code, const char *format, ...);
		static std::shared_ptr<Error> CreateError(ov::String domain, const char *format, ...);

		static std::shared_ptr<Error> CreateErrorFromErrno();

		virtual ~Error() = default;

		const String &GetDomain() const
		{
			return _domain;
		}

		virtual int GetCode() const;
		virtual String GetMessage() const;

		virtual const char *What() const noexcept
		{
			return _error_string.CStr();
		}

	protected:
		Error(ov::String domain);
		Error(ov::String domain, int code, bool code_set, ov::String message);

		void SetCodeAndMessage(int code, ov::String message);
		void SetMessage(ov::String message);

		void UpdateErrorString();

		const char *what() const noexcept override
		{
			return _error_string.CStr();
		}

	protected:
		String _domain;

		int _code = 0;
		bool _code_set = false;

		String _message;

		String _error_string;
	};
}  // namespace ov
