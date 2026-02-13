//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/host.h>

namespace ocst
{
	class Origin
	{
	public:
		Origin() = default; // not valid
		Origin(const cfg::vhost::orgn::Origin &origin_config);

		ov::String GetScheme() const { return _scheme; }
		ov::String GetLocation() const { return _location; }
		bool IsForwardQueryParamsEnabled() const { return _forward_query_params; }
		std::vector<ov::String> GetUrlList() const { return _url_list; }
		cfg::vhost::orgn::Origin GetOriginConfig() const { return _origin_config; }

		bool MakeUrlListForRequestedLocation(const ov::String &requested_location, std::vector<ov::String> &url_list) const;

		bool IsPersistent() const { return _persistent; }
		bool IsFailback() const { return _failback; }
		bool IsStrictLocation() const { return _strict_location; }
		bool IsIgnoreRtcpSrTimestamp() const { return _ignore_rtcp_sr_timestamp; }
		bool IsRelay() const { return _relay; }
		bool IsValid() const { return _is_valid; }

	private:
		bool _is_valid = false;
		info::application_id_t _app_id = 0U;

		ov::String _scheme;

		// Origin/Location
		ov::String _location;

		// Forwarding query params
		bool _forward_query_params = true;

		// Generated URL list from <Origin>.<Pass>.<URL>
		std::vector<ov::String> _url_list;

		// Original configuration
		cfg::vhost::orgn::Origin _origin_config;

		bool _persistent = false;
		bool _failback = false;
		bool _strict_location = false;
		bool _ignore_rtcp_sr_timestamp = false;
		bool _relay = true;
	};
}