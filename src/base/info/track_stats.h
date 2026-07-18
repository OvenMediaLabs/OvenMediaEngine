//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/common_types.h>

class MediaPacket;

// Runtime measurement counters of one track.
// This is the deliberately shared mutable state between modules: every copy of
// a stream shares the same TrackStats instance through its MediaTrack, while
// the track description flows separately as immutable generation snapshots.
class TrackStats
{
public:
	// Accumulate one media packet. Timebase and media type come from the owning track.
	void OnFrameAdded(const std::shared_ptr<MediaPacket> &media_packet, const cmn::Timebase &timebase, cmn::MediaType media_type);

	// Bitrate (measured)
	int32_t GetBitrateByMeasured() const;
	void SetBitrateByMeasured(int32_t bitrate);
	int32_t GetBitrateLastSecond() const;
	void SetBitrateLastSecond(int32_t bitrate);

	// Framerate (measured, video only)
	double GetFrameRateByMeasured() const;
	void SetFrameRateByMeasured(double framerate);
	double GetFrameRateLastSecond() const;
	void SetFrameRateLastSecond(double framerate);
	void AddToMeasuredFramerateWindow(double framerate);
	std::deque<double> GetMeasuredFramerateWindow() const;

	// Key frame interval (measured, video only)
	double GetKeyFrameIntervalByMeasured() const;
	void SetKeyFrameIntervalByMeasured(double key_frame_interval);
	double GetKeyFrameIntervalLatest() const;
	void SetKeyFrameIntervalLatest(double key_frame_interval);
	int32_t GetDeltaFramesSinceLastKeyFrame() const;
	void SetDeltaFrameCountSinceLastKeyFrame(int32_t delta_frame_count);

	// Frame time (milliseconds since epoch)
	void SetStartFrameTime(int64_t time);
	int64_t GetStartFrameTime() const;
	void SetLastFrameTime(int64_t time);
	int64_t GetLastFrameTime() const;

	// Totals
	int64_t GetTotalFrameCount() const;
	int64_t GetTotalFrameBytes() const;

	// Latched once the owning track decides its quality has been measured
	bool IsQualityMeasured() const;
	void SetQualityMeasured();

	// B-frames detected in the bitstream by the config author
	void SetHasBframes(bool has_bframe);
	bool HasBframes() const;

	// Media config changes observed by the config author (operator-facing:
	// how many times this track's content configuration changed, and when last)
	void OnConfigChanged(int64_t time_ms);
	uint32_t GetConfigChangeCount() const;
	int64_t GetLastConfigChangeTimeMs() const;

private:
	// Bitrate
	std::atomic<int32_t> _bitrate_measured = 0;
	std::atomic<int32_t> _bitrate_last_second = 0;

	// Framerate
	std::atomic<double> _framerate_measured = 0.0;
	std::atomic<double> _framerate_last_second = 0.0;

	mutable ov::SharedMutex _framerate_window_mutex;
	std::deque<double> _measured_framerate_window OV_GUARDED_BY(_framerate_window_mutex);

	// Key frame interval
	std::atomic<double> _key_frame_interval_measured = 0.0;
	std::atomic<double> _key_frame_interval_latest = 0.0;
	std::atomic<int32_t> _delta_frame_count_since_last_key_frame = 0;

	// Frame time
	std::atomic<int64_t> _start_frame_time = 0;
	std::atomic<int64_t> _last_frame_time = 0;

	// Measurement clocks
	ov::StopWatch _clock_from_first_frame_received;
	ov::StopWatch _timer_one_second;

	// Totals and calculation windows
	std::atomic<uint64_t> _total_frame_count = 0;
	std::atomic<uint64_t> _total_frame_bytes = 0;
	std::atomic<uint64_t> _total_key_frame_count = 0;
	std::atomic<int32_t> _key_frame_interval_count = 0;

	std::atomic<uint64_t> _last_seconds_frame_count = 0;
	std::atomic<uint64_t> _last_seconds_frame_bytes = 0;

	std::atomic<uint64_t> _last_frame_count = 0;
	std::atomic<uint64_t> _last_frame_bytes = 0;

	std::atomic<int64_t> _last_received_timestamp = -1;

	std::atomic<bool> _quality_measured = false;

	std::atomic<bool> _has_bframe = false;

	// Media config changes
	std::atomic<uint32_t> _config_change_count = 0;
	std::atomic<int64_t> _last_config_change_time_ms = 0;
};
