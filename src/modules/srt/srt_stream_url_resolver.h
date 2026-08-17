//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/host.h>
#include <base/ovsocket/ovsocket.h>
#include <config/config.h>

namespace modules::srt
{
	class StreamUrlResolver
	{
	public:
		void Initialize(const std::vector<cfg::cmn::SrtStream> &stream_list);
		// First: Request URL extracted from the `streamid`
		// Second: Host information (VirtualHost)
		std::tuple<std::shared_ptr<ov::Url>, std::optional<info::Host>> Resolve(const std::shared_ptr<ov::Socket> &remote);

	private:
		std::optional<ov::String> GetStreamPath(int port);

		/// @brief Get host information based on the provided vhost/host name.
		///
		/// @param vhost_name The name of the virtual host or host.
		///
		/// @returns Host Info
		std::optional<info::Host> GetHostInfo(const ov::String &host) const;

	private:
		ov::SharedMutex _stream_map_mutex;
		// Key: port, Value: stream_path
		std::map<int, ov::String> _stream_map OV_GUARDED_BY(_stream_map_mutex);
	};

}  // namespace modules::srt
