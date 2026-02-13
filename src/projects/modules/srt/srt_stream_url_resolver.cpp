//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================

#include "./srt_stream_url_resolver.h"

#include <orchestrator/orchestrator.h>

#include "./srt_private.h"

namespace modules::srt
{
	void StreamUrlResolver::Initialize(const std::vector<cfg::cmn::SrtStream> &stream_list)
	{
		std::unique_lock lock{_stream_map_mutex};
		for (const auto &stream : stream_list)
		{
			_stream_map.emplace(stream.GetPort(), stream.GetStreamPath());
		}
	}

	std::optional<ov::String> StreamUrlResolver::GetStreamPath(int port)
	{
		std::shared_lock lock{_stream_map_mutex};
		auto it = _stream_map.find(port);

		return (it != _stream_map.end()) ? std::optional<ov::String>(it->second) : std::nullopt;
	}

	std::optional<info::Host> StreamUrlResolver::GetHostInfo(const ov::String &host) const
	{
		auto orchestrator = ocst::Orchestrator::GetInstance();

		// 1. Check if `host` is a name of virtual host
		auto host_info	  = orchestrator->GetHostInfo(host);

		if (host_info.has_value())
		{
			// `host` is a name of virtual host
			return host_info;
		}

		// 2. If `host` is not a name of virtual host, consider it as a domain and get the vhost
		auto vhost = orchestrator->GetVhostNameFromDomain(host);

		if (vhost.IsEmpty())
		{
			logte("Could not find VirtualHost (%s)", host.CStr());
			return std::nullopt;
		}

		return orchestrator->GetHostInfo(vhost);
	}

	std::tuple<std::shared_ptr<ov::Url>, std::optional<info::Host>> StreamUrlResolver::Resolve(const std::shared_ptr<ov::Socket> &remote)
	{
		// stream_id can be in the following format:
		//
		//   #!::u=abc123,bmd_uuid=1234567890...
		//
		// https://github.com/Haivision/srt/blob/master/docs/features/access-control.md
		constexpr static const char SRT_STREAM_ID_PREFIX[] = "#!::";
		constexpr static const char SRT_USER_NAME_PREFIX[] = "u=";

		std::shared_ptr<ov::Url> parsed_url				   = nullptr;

		// Get app/stream name from stream_id
		auto streamid									   = remote->GetStreamId();

		// stream_path takes one of two forms:
		//
		// 1. urlencode(srt://host[:port]/app/stream[?query=value]) (deprecated)
		// 2. <host>/<app>/<stream>[?query=value]
		ov::String stream_path;

		if (streamid.IsEmpty())
		{
			auto path = GetStreamPath(remote->GetLocalAddress()->Port());

			if (path.has_value() == false)
			{
				logte("There is no stream information in the stream map for the SRT client: %s", remote->ToString().CStr());
				return {nullptr, std::nullopt};
			}

			stream_path = path.value();
		}
		else
		{
			auto final_streamid = streamid;
			ov::String user_name;
			bool from_user_name = false;

			if (final_streamid.HasPrefix(SRT_STREAM_ID_PREFIX))
			{
				// Remove the prefix `#!::`
				final_streamid	= final_streamid.Substring(OV_COUNTOF(SRT_STREAM_ID_PREFIX) - 1);

				auto key_values = final_streamid.Split(",");

				// Extract user name part (u=xxx)
				for (const auto &key_value : key_values)
				{
					if (key_value.HasPrefix(SRT_USER_NAME_PREFIX))
					{
						final_streamid = key_value.Substring(OV_COUNTOF(SRT_USER_NAME_PREFIX) - 1);
						from_user_name = true;

						break;
					}
				}
			}

			if (final_streamid.IsEmpty())
			{
				if (from_user_name)
				{
					logte("Empty user name is not allowed in the streamid: [%s]", streamid.CStr());
				}
				else
				{
					logte("Empty streamid is not allowed");
				}

				return {nullptr, std::nullopt};
			}

			stream_path = ov::Url::Decode(final_streamid);

			if (stream_path.IsEmpty())
			{
				if (from_user_name)
				{
					logte("Invalid user name in the streamid: [%s] (streamid: [%s])", user_name.CStr(), streamid.CStr());
				}
				else
				{
					logte("Invalid streamid: [%s]", streamid.CStr());
				}

				return {nullptr, std::nullopt};
			}
		}

		if (stream_path.HasPrefix("srt://"))
		{
			// Deprecated format
			logtw("The srt://... format is deprecated. Use {host}/{app}/{stream} format instead: %s", streamid.CStr());
		}
		else
		{
			// {host}/{app}/{stream}[/{playlist}] format
			auto parts		= stream_path.Split("/");
			auto part_count = parts.size();

			if ((part_count != 3) && (part_count != 4))
			{
				logte("The streamid for SRT must be in the following format: {host}/{app}/{stream}[/{playlist}], but [%s]", stream_path.CStr());
				return {nullptr, std::nullopt};
			}

			// Convert to srt://{host}/{app}/{stream}[/{playlist}]
			stream_path.Prepend("srt://");
		}

		auto final_url = stream_path.IsEmpty() ? nullptr : ov::Url::Parse(stream_path);

		if (final_url == nullptr)
		{
			auto extra_log = streamid.IsEmpty() ? "" : ov::String::FormatString(" (streamid: [%s])", streamid.CStr());
			logte("The streamid for SRT must be in one of the following formats: srt://{host}[:{port}]/{app}/{stream}[/{playlist}][?{query}={value}] or {host}/{app}/{stream}[/{playlist}], but [%s]%s", stream_path.CStr(), extra_log.CStr());
		}

		return {final_url, GetHostInfo(final_url->Host())};
	}
}  // namespace modules::srt
