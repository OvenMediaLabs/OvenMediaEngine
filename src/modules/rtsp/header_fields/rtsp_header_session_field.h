//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once
#include <base/ovlibrary/ovlibrary.h>
#include "rtsp_header_field.h"

class RtspHeaderSessionField : public RtspHeaderField
{
public:
	RtspHeaderSessionField()
		: RtspHeaderField(RtspHeaderFieldType::Session)
	{

	}

	RtspHeaderSessionField(ov::String session_id)
		: RtspHeaderField(RtspHeaderFieldType::Session, session_id)
	{
		_session_id = session_id;
	}

	RtspHeaderSessionField(ov::String session_id, uint32_t timeout_delta_seconds)
		: RtspHeaderField(RtspHeaderFieldType::Session, ov::String::FormatString("%s;%u", session_id.CStr(), timeout_delta_seconds))
	{
		_session_id = session_id;
		_timeout_delta_seconds = timeout_delta_seconds;
	}

	// https://tools.ietf.org/html/rfc2326#page-57
	// Session  = "Session" ":" session-id [ ";" "timeout" "=" delta-seconds ]

	bool Parse(const ov::String &message) override
	{
		if(RtspHeaderField::Parse(message) == false)
		{
			return false;
		}

		auto items = GetValue().Split(";");

		// Has 'timeout'
		if(items.size() == 2)
		{
			_session_id = items[0];

			auto timeout_tokens = items[1].Split("=");
			if(timeout_tokens.size() == 2 && timeout_tokens[0] == "timeout")
			{
				_timeout_delta_seconds = std::atoi(timeout_tokens[1].CStr());
			}
		}
		else
		{
			_session_id = GetValue();
		}

		return true;
	}

	ov::String Serialize() const override
	{
		return ov::String::FormatString("Session: %s;timeout=%u", _session_id.CStr(), _timeout_delta_seconds);
	}

	ov::String GetSessionId()
	{
		return _session_id;
	}

	uint32_t GetTimeoutDeltaSeconds()
	{
		return _timeout_delta_seconds;
	}

private:
	ov::String		_session_id;
	uint32_t		_timeout_delta_seconds = 0;
};