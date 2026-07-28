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
		_published_time_steady = std::chrono::steady_clock::now().time_since_epoch().count();

		// Set last, so a reader that checks the on-air state finds a valid time
		_on_air = true;
	}

	std::chrono::system_clock::time_point StreamStats::GetPublishedTime() const
	{
		return std::chrono::system_clock::time_point(std::chrono::system_clock::duration(_published_time.load()));
	}

	std::chrono::steady_clock::time_point StreamStats::GetPublishedTimeSteady() const
	{
		return std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(_published_time_steady.load()));
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
}  // namespace info
