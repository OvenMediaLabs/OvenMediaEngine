//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#include "stream_stats.h"

namespace info
{
	std::shared_ptr<const ConnectionInfo> ConnectionInfo::From(const ov::SocketAddress *local_address, const ov::SocketAddress *remote_address, ov::SocketType socket_type, const char *transport, uint64_t version)
	{
		auto connection_info	   = std::make_shared<ConnectionInfo>();

		auto protocol			   = ov::StringFromSocketType(socket_type);

		connection_info->protocol  = protocol;
		connection_info->transport = (transport != nullptr) ? transport : protocol;

		if ((local_address != nullptr) && local_address->IsValid())
		{
			connection_info->local_address = local_address->GetIpAddress();
			connection_info->local_port	   = local_address->Port();
		}
		if ((remote_address != nullptr) && remote_address->IsValid())
		{
			connection_info->remote_address = remote_address->GetIpAddress();
			connection_info->remote_port	= remote_address->Port();
		}
		connection_info->version = version;

		return connection_info;
	}

	std::shared_ptr<const ConnectionInfo> ConnectionInfo::From(const ov::SocketAddressPair &address_pair, ov::SocketType socket_type, const char *transport, uint64_t version)
	{
		return From(&(address_pair.GetLocalAddress()), &(address_pair.GetRemoteAddress()), socket_type, transport, version);
	}

	std::shared_ptr<const ConnectionInfo> ConnectionInfo::From(const std::shared_ptr<ov::Socket> &socket, const char *transport, uint64_t version)
	{
		if (socket == nullptr)
		{
			return nullptr;
		}

		return From(socket->GetLocalAddress().get(), socket->GetRemoteAddress().get(), socket->GetType(), transport, version);
	}

	ov::String ConnectionInfo::ToString() const
	{
		auto result = ov::String::FormatString(
			"%s (%s, local: ",
			transport.CStr(), protocol.CStr());

		if (local_port.has_value())
		{
			result.AppendFormat("%s:%u", local_address.CStr(), local_port.value());
		}
		else
		{
			result.Append("N/A");
		}

		result.Append(", remote: ");

		if (remote_port.has_value())
		{
			result.AppendFormat("%s:%u", remote_address.CStr(), remote_port.value());
		}
		else
		{
			result.Append("N/A");
		}

		result.Append(")");

		return result;
	}

	StreamStats::StreamStats()
		: _created_time(std::chrono::system_clock::now())
	{
	}

	std::chrono::system_clock::time_point StreamStats::GetCreatedTime() const
	{
		return _created_time;
	}

	void StreamStats::SetPublishedTime(const std::chrono::system_clock::time_point &time)
	{
		_published_time = time.time_since_epoch().count();

		// Set last, so a reader that checks the on-air state finds a valid time
		_on_air = true;
	}

	std::chrono::system_clock::time_point StreamStats::GetPublishedTime() const
	{
		return std::chrono::system_clock::time_point(std::chrono::system_clock::duration(_published_time.load()));
	}

	bool StreamStats::IsOnAir() const
	{
		return _on_air;
	}

	void StreamStats::SetOnAir(bool on_air)
	{
		if (on_air)
		{
			SetPublishedTime(std::chrono::system_clock::now());
			return;
		}

		_on_air = false;
	}

	void StreamStats::SetFirstMediaTime()
	{
		if (_first_media_time.load() != 0)
		{
			return;
		}

		// Set the steady value first, so a reader that finds the wall clock time set
		// always finds the steady one too
		_first_media_time_steady = std::chrono::steady_clock::now().time_since_epoch().count();
		_first_media_time = std::chrono::system_clock::now().time_since_epoch().count();
	}

	bool StreamStats::HasFirstMediaTime() const
	{
		return _first_media_time.load() != 0;
	}

	std::chrono::system_clock::time_point StreamStats::GetFirstMediaTime() const
	{
		return std::chrono::system_clock::time_point(std::chrono::system_clock::duration(_first_media_time.load()));
	}

	std::chrono::steady_clock::time_point StreamStats::GetFirstMediaTimeSteady() const
	{
		return std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(_first_media_time_steady.load()));
	}

	void StreamStats::SetPrepared(bool prepared)
	{
		_prepared_time = prepared ? std::chrono::system_clock::now().time_since_epoch().count() : 0;
	}

	std::chrono::system_clock::time_point StreamStats::GetPreparedTime() const
	{
		return std::chrono::system_clock::time_point(std::chrono::system_clock::duration(_prepared_time.load()));
	}

	void StreamStats::SetMediaSource(const ov::String &url)
	{
		ov::LockGuard lock(_media_source_mutex);
		_media_source = url;
	}

	ov::String StreamStats::GetMediaSource() const
	{
		ov::LockGuard lock(_media_source_mutex);
		return _media_source;
	}

	bool StreamStats::SetConnectionInfo(const std::shared_ptr<const ConnectionInfo> &connection_info)
	{
		ov::LockGuard lock(_connection_info_mutex);

		// Equal or lower versions are stale/duplicate publications from a racing thread
		if ((_connection_info != nullptr) && (connection_info != nullptr) &&
			(connection_info->version != 0) && (connection_info->version <= _connection_info->version))
		{
			return false;
		}

		_connection_info = connection_info;
		return true;
	}

	std::shared_ptr<const ConnectionInfo> StreamStats::GetConnectionInfo() const
	{
		ov::LockGuard lock(_connection_info_mutex);
		return _connection_info;
	}
}  // namespace info
