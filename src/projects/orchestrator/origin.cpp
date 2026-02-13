#include "origin.h"

#include "orchestrator_private.h"

namespace ocst
{
	Origin::Origin(const cfg::vhost::orgn::Origin &origin_config)
	{
		_scheme = origin_config.GetPass().GetScheme();
		_location = origin_config.GetLocation();
		_forward_query_params = origin_config.GetPass().IsForwardQueryParamsEnabled();

		bool parsed = false;
		_persistent = origin_config.IsPersistent();
		_failback = origin_config.IsFailback();
		_strict_location = origin_config.IsStrictLocation();
		_ignore_rtcp_sr_timestamp = origin_config.IsRtcpSrTimestampIgnored();
		_relay = origin_config.IsRelay(&parsed);
		if (parsed == false && _scheme.UpperCaseString() == "OVT")
		{
			_relay = true;
		}

		for (auto &url : origin_config.GetPass().GetUrlList())
		{
			_url_list.push_back(url);
		}

		this->_origin_config = origin_config;

		_is_valid = true;
	}

	bool Origin::MakeUrlListForRequestedLocation(const ov::String &requested_location, std::vector<ov::String> &url_list) const
	{
		// If the location has the prefix that configured in <Origins>, extract the remaining part
		// For example, if the settings is:
		//      <Origin>
		//      	<Location>/app/stream</Location>
		//      	<Pass>
		//              <Scheme>ovt</Scheme>
		//              <Urls>
		//      		    <Url>origin.airensoft.com:9000/another_app/and_stream</Url>
		//              </Urls>
		//      	</Pass>
		//      </Origin>
		// And when the location is "/app/stream_o",
		//
		// <Location>: /app/stream
		// location:   /app/stream_o
		//                        ~~ <= remaining part
		auto remaining_part = requested_location.Substring(_location.GetLength());

		logtt("Found: Location: %s, Requested Location: %s, remaining_part: %s", _location.CStr(), requested_location.CStr(), remaining_part.CStr());
		for (auto url : _url_list)
		{
			// Append the remaining_part to the URL

			// For example,
			//    url:     ovt://origin.airensoft.com:9000/another_app/and_stream
			//    new_url: ovt://origin.airensoft.com:9000/another_app/and_stream_o
			//                                                                   ~~ <= remaining part

			// Prepend "<scheme>://"
			url.Prepend("://");
			url.Prepend(_scheme);

			// Exclude query string from url
			auto index = url.IndexOf('?');
			auto url_part = url.Substring(0, index);
			auto another_part = url.Substring(index + 1);

			// Append remaining_part
			url_part.Append(remaining_part);

			if(index >= 0)
			{
				url_part.Append('?');
				url_part.Append(another_part);
			}

			url_list.push_back(url_part);
		}

		return true;
	}
} // namespace ocst