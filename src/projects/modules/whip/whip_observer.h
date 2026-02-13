//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <modules/http/server/http_server.h>
#include <modules/sdp/session_description.h>

class RtcIceCandidate;

class WhipObserver : public ov::EnableSharedFromThis<WhipObserver>
{
public:
	struct Answer
	{
		Answer(const ov::String &session_id, const ov::String &etag, const std::shared_ptr<SessionDescription> &sdp, const ov::String &vhost_name, const ov::String &app_name, http::StatusCode status_code, const ov::String &error_message="")
			: _session_id(session_id),
			_entity_tag(etag),
			_sdp(sdp),
			_vhost_name(vhost_name),
			_app_name(app_name),
			_status_code(status_code),
			_error_message(error_message)
		{
		}

		Answer(const std::shared_ptr<SessionDescription> &sdp, http::StatusCode status_code, const ov::String &error_message = "")
			:_sdp(sdp),
			_status_code(status_code),
			_error_message(error_message)
		{
		}

		Answer(http::StatusCode status_code, const ov::String &error_message)
			: _status_code(status_code),
			_error_message(error_message)
		{
		}

		Answer(http::StatusCode status_code, const ov::String &vhost_name, const ov::String &app_name)
			: _vhost_name(vhost_name),
			_app_name(app_name),
			_status_code(status_code)
		{
		}

		ov::String _session_id;
		ov::String _entity_tag;
		std::shared_ptr<SessionDescription> _sdp = nullptr;
		ov::String _vhost_name;
		ov::String _app_name;
		http::StatusCode _status_code = http::StatusCode::InternalServerError; // 201 Created if success
		ov::String _error_message;
	};

	virtual Answer OnSdpOffer(const std::shared_ptr<const http::svr::HttpRequest> &request,
								const std::shared_ptr<const SessionDescription> &offer_sdp) = 0;

	virtual Answer OnTrickleCandidate(const std::shared_ptr<const http::svr::HttpRequest> &request,
									const ov::String &session_id,
									const ov::String &if_match,
									const std::shared_ptr<const SessionDescription> &patch){ return {http::StatusCode::NoContent, ""}; }

	virtual Answer OnSessionDelete(const std::shared_ptr<const http::svr::HttpRequest> &request,
			const ov::String &session_key) = 0;
};