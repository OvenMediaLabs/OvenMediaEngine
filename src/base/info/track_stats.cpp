//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#include "track_stats.h"

#include <base/mediarouter/media_buffer.h>

// Number of samples kept for abnormal-framerate detection
static constexpr size_t kAbnormalFpsCheckWindowSize = 30;

void TrackStats::OnFrameAdded(const std::shared_ptr<MediaPacket> &media_packet, const cmn::Timebase &timebase, cmn::MediaType media_type)
{
	if (_clock_from_first_frame_received.IsStart() == false)
	{
		_clock_from_first_frame_received.Start();
	}

	if (_timer_one_second.IsStart() == false)
	{
		_timer_one_second.Start();
	}

	size_t bytes = media_packet->GetDataLength();

	// [Timestamp-based] Calculate framerate/bitrate from the previous window, then reset.
	// The current frame is counted into the new window after accumulation below.
	if (_last_received_timestamp == -1)
	{
		_last_received_timestamp = media_packet->GetDts();
		_last_frame_count = 0;
		_last_frame_bytes = 0;
	}
	else
	{
		auto duration = (media_packet->GetDts() - _last_received_timestamp) * timebase.GetExpr();
		if (duration >= 1.0)
		{
			SetBitrateByMeasured(static_cast<int32_t>(_last_frame_bytes / duration * 8));
			SetFrameRateByMeasured(static_cast<double>(_last_frame_count) / duration);

			_last_received_timestamp = media_packet->GetDts();
			_last_frame_count = 0;
			_last_frame_bytes = 0;
		}
	}

	// [Wall-clock] Calculate framerate/bitrate from the previous window, then reset.
	// The current frame is counted into the new window after accumulation below.
	if (_timer_one_second.IsElapsed(1000))
	{
		// It can be greater than 1 second due to timer delay or frame processing time.
		auto seconds = static_cast<double>(_timer_one_second.Elapsed()) / 1000.0;

		SetBitrateLastSecond(static_cast<int32_t>(_last_seconds_frame_bytes * 8.0 / seconds));
		SetFrameRateLastSecond(static_cast<double>(_last_seconds_frame_count) / seconds);

		_last_seconds_frame_count = 0;
		_last_seconds_frame_bytes = 0;

		_timer_one_second.Restart();
	}

	// Accumulate all counters after both calculation windows have been evaluated.
	_total_frame_count++;
	_total_frame_bytes += bytes;
	_last_frame_count++;
	_last_frame_bytes += bytes;
	_last_seconds_frame_count++;
	_last_seconds_frame_bytes += bytes;

	// Keyframe statistics (uses _total_frame_count, so must follow accumulation above).
	if (media_type == cmn::MediaType::Video)
	{
		if (media_packet->GetFlag() == MediaPacketFlag::Key)
		{
			_total_key_frame_count++;
			if (_total_key_frame_count >= 2)
			{
				SetKeyFrameIntervalByMeasured(static_cast<double>(_total_frame_count - 1) / static_cast<double>(_total_key_frame_count - 1));
			}
			SetKeyFrameIntervalLatest(_key_frame_interval_count);
			_key_frame_interval_count = 1;
			_delta_frame_count_since_last_key_frame = 0;
		}
		else if (_key_frame_interval_count > 0)
		{
			_key_frame_interval_count++;
			_delta_frame_count_since_last_key_frame++;
		}
	}
}

int32_t TrackStats::GetBitrateByMeasured() const
{
	return _bitrate_measured;
}

void TrackStats::SetBitrateByMeasured(int32_t bitrate)
{
	_bitrate_measured = bitrate;
}

int32_t TrackStats::GetBitrateLastSecond() const
{
	return _bitrate_last_second;
}

void TrackStats::SetBitrateLastSecond(int32_t bitrate)
{
	_bitrate_last_second = bitrate;
}

double TrackStats::GetFrameRateByMeasured() const
{
	return _framerate_measured;
}

void TrackStats::SetFrameRateByMeasured(double framerate)
{
	_framerate_measured = framerate;
}

double TrackStats::GetFrameRateLastSecond() const
{
	return _framerate_last_second;
}

void TrackStats::SetFrameRateLastSecond(double framerate)
{
	_framerate_last_second = framerate;
}

void TrackStats::AddToMeasuredFramerateWindow(double framerate)
{
	ov::ScopedLock lock(_framerate_window_mutex);

	_measured_framerate_window.push_back(framerate);

	if (_measured_framerate_window.size() > kAbnormalFpsCheckWindowSize)
	{
		_measured_framerate_window.pop_front();
	}
}

std::deque<double> TrackStats::GetMeasuredFramerateWindow() const
{
	ov::SharedLockGuard lock(_framerate_window_mutex);
	return _measured_framerate_window;
}

double TrackStats::GetKeyFrameIntervalByMeasured() const
{
	return _key_frame_interval_measured;
}

void TrackStats::SetKeyFrameIntervalByMeasured(double key_frame_interval)
{
	_key_frame_interval_measured = key_frame_interval;
}

double TrackStats::GetKeyFrameIntervalLatest() const
{
	return _key_frame_interval_latest;
}

void TrackStats::SetKeyFrameIntervalLatest(double key_frame_interval)
{
	_key_frame_interval_latest = key_frame_interval;
}

int32_t TrackStats::GetDeltaFramesSinceLastKeyFrame() const
{
	return _delta_frame_count_since_last_key_frame;
}

void TrackStats::SetDeltaFrameCountSinceLastKeyFrame(int32_t delta_frame_count)
{
	_delta_frame_count_since_last_key_frame = delta_frame_count;
}

void TrackStats::SetStartFrameTime(int64_t time)
{
	_start_frame_time = time;
}

int64_t TrackStats::GetStartFrameTime() const
{
	return _start_frame_time;
}

void TrackStats::SetLastFrameTime(int64_t time)
{
	_last_frame_time = time;
}

int64_t TrackStats::GetLastFrameTime() const
{
	return _last_frame_time;
}

int64_t TrackStats::GetTotalFrameCount() const
{
	return _total_frame_count;
}

int64_t TrackStats::GetTotalFrameBytes() const
{
	return _total_frame_bytes;
}

bool TrackStats::IsQualityMeasured() const
{
	return _quality_measured;
}

void TrackStats::SetQualityMeasured()
{
	_quality_measured = true;
}
