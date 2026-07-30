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
}  // namespace info
