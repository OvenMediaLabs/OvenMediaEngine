//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Gilhoon Choi
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/common_types.h>
#include <base/ovlibrary/ovlibrary.h>
#include <base/ovsocket/socket_address.h>

#include "../queue_metrics.h"
#include "../stream_metrics.h"
#include "message.h"

namespace mon::alrt
{
	class Notification
	{
	public:
		enum class StatusCode : uint8_t
		{
			OK,
			// From User's server
			INVALID_DATA_FORMAT,
			// From http level error
			INVALID_STATUS_CODE,  // If HTTP status codes is NOT 200 ok
			// Internal, etc errors
			INTERNAL_ERROR,
		};

		static std::shared_ptr<Notification> Query(const std::shared_ptr<ov::Url> &notification_server_url,
												   uint32_t timeout_msec,
												   const ov::String secret_key,
												   const ov::String message_body);

		StatusCode GetStatusCode() const;
		ov::String GetErrorReason() const;
		uint64_t GetElapsedTime() const;

	private:
		void Run();
		ov::String GetMessageBody();
		void SetStatus(StatusCode code, ov::String reason);

		uint64_t _elapsed_msec							  = 0;

		// Request
		std::shared_ptr<ov::Url> _notification_server_url = nullptr;
		uint64_t _timeout_msec							  = 0;
		ov::String _secret_key;
		ov::String _message_body;

		// Response
		StatusCode _status_code = StatusCode::OK;
		ov::String _error_reason;
	};
}  // namespace mon::alrt
