//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "video_track.h"

VideoTrack::VideoTrack()
	: _framerate_last_second(0),
	  _video_timescale(0),
	  _key_frame_interval_latest(0),
	  _delta_frame_count_since_last_key_frame(0),
	  _key_frame_interval_type_conf(cmn::KeyFrameIntervalType::FRAME),
	  _b_frames(0),
	  _has_bframe(false),
	  _colorspace(cmn::VideoPixelFormatId::None),	  
	  _preset(""),
	  _profile(""),
	  _thread_count(0),
	  _skip_frames_conf(-1), // Default value is -1
	  _keyframe_decode_only(false),
	  _lookahead_conf(-1),
	  _overlay_signature(0)
{
}

void VideoTrack::SetResolution(int32_t width, int32_t height)
{
	SetResolution(cmn::Resolution{width, height});
}

void VideoTrack::SetResolution(const cmn::Resolution &resolution)
{
	std::scoped_lock lock(_video_mutex);

	_resolution			   = resolution;
	_max_resolution.width  = std::max(_max_resolution.width, _resolution.width);
	_max_resolution.height = std::max(_max_resolution.height, _resolution.height);

	OV_ASSERT(_max_resolution.width >= _resolution.width, "Invalid resolution invariant: max width must be >= width");
	OV_ASSERT(_max_resolution.height >= _resolution.height, "Invalid resolution invariant: max height must be >= height");
}

cmn::Resolution VideoTrack::GetResolution() const
{
	std::shared_lock lock(_video_mutex);
	return _resolution;
}

void VideoTrack::SetMaxResolution(int32_t max_width, int32_t max_height)
{
	SetMaxResolution(cmn::Resolution{max_width, max_height});
}

void VideoTrack::SetMaxResolution(const cmn::Resolution &max_resolution)
{
	std::scoped_lock lock(_video_mutex);

	_max_resolution.width  = std::max(std::max(_max_resolution.width, _resolution.width), max_resolution.width);
	_max_resolution.height = std::max(std::max(_max_resolution.height, _resolution.height), max_resolution.height);

	OV_ASSERT(_max_resolution.width >= _resolution.width, "Invalid resolution invariant: max width must be >= width");
	OV_ASSERT(_max_resolution.height >= _resolution.height, "Invalid resolution invariant: max height must be >= height");
}

cmn::Resolution VideoTrack::GetMaxResolution() const
{
	std::shared_lock lock(_video_mutex);
	return _max_resolution;
}

void VideoTrack::SetResolutionByConfig(int32_t width, int32_t height)
{
	SetResolutionByConfig(cmn::Resolution{width, height});
}

void VideoTrack::SetResolutionByConfig(const cmn::Resolution &resolution)
{
	std::scoped_lock lock(_video_mutex);
	_resolution_conf = resolution;
}

cmn::Resolution VideoTrack::GetResolutionByConfig() const
{
	std::shared_lock lock(_video_mutex);
	return _resolution_conf;
}

bool VideoTrack::IsValidResolution() const
{
	std::shared_lock lock(_video_mutex);

	return (_resolution.width > 0) && (_resolution.height > 0);
}

void VideoTrack::SetVideoTimestampScale(double scale)
{
	_video_timescale = scale;
}

double VideoTrack::GetVideoTimestampScale() const
{
	return _video_timescale;
}

void VideoTrack::SetPreset(ov::String preset)
{
	std::scoped_lock lock(_video_mutex);
	_preset = preset;
}

ov::String VideoTrack::GetPreset() const
{
	std::shared_lock lock(_video_mutex);
	return _preset;
}

void VideoTrack::SetProfile(ov::String profile)
{
	std::scoped_lock lock(_video_mutex);
	_profile = profile;
}

ov::String VideoTrack::GetProfile() const
{
	std::shared_lock lock(_video_mutex);
	return _profile;
}

void VideoTrack::SetHasBframes(bool has_bframe)
{
	_has_bframe = has_bframe;
}

bool VideoTrack::HasBframes() const
{
	return _has_bframe;
}

void VideoTrack::SetThreadCount(int thread_count)
{
	_thread_count = thread_count;
}

int VideoTrack::GetThreadCount()
{
	return _thread_count;
}

VideoTrack::FrameSnapshot VideoTrack::GetFrameSnapshot() const
{
	std::shared_lock lock(_video_mutex);
	return _frame_snapshot;
}

double VideoTrack::GetKeyFrameInterval() const
{
	std::shared_lock lock(_video_mutex);
	return _frame_snapshot.GetKeyFrameInterval();
}

void VideoTrack::SetKeyFrameIntervalByMeasured(double key_frame_interval)
{
	std::scoped_lock lock(_video_mutex);
	_frame_snapshot.key_frame_interval = key_frame_interval;
}

double VideoTrack::GetKeyFrameIntervalByMeasured() const
{
	std::shared_lock lock(_video_mutex);
	return _frame_snapshot.key_frame_interval;
}

void VideoTrack::AddToMeasuredFramerateWindow(double framerate)
{
	std::scoped_lock lock(_video_mutex);

	size_t kAbnormalFpsCheckWindowSize = 60;

	_measured_framerate_window.push_back(framerate);

	if (_measured_framerate_window.size() > kAbnormalFpsCheckWindowSize)
	{
		_measured_framerate_window.pop_front();
	}
}

std::deque<double> VideoTrack::GetMeasuredFramerateWindow() const
{
	std::shared_lock lock(_video_mutex);

	return _measured_framerate_window;
}

void VideoTrack::SetKeyFrameIntervalLastet(double key_frame_interval)
{
	_key_frame_interval_latest = key_frame_interval;
}

double VideoTrack::GetKeyFrameIntervalLatest() const
{
	return _key_frame_interval_latest;
}

void VideoTrack::SetKeyFrameIntervalByConfig(int32_t key_frame_interval)
{
	std::scoped_lock lock(_video_mutex);

	if (key_frame_interval > 0)
	{
		_frame_snapshot.key_frame_interval_conf = key_frame_interval;
	}
	else
	{
		_frame_snapshot.key_frame_interval_conf.reset();
	}
}

double VideoTrack::GetKeyFrameIntervalByConfig() const
{
	std::shared_lock lock(_video_mutex);
	return _frame_snapshot.key_frame_interval_conf.value_or(0.0);
}

void VideoTrack::SetKeyFrameIntervalTypeByConfig(cmn::KeyFrameIntervalType key_frame_interval_type)
{
	_key_frame_interval_type_conf = key_frame_interval_type;
}

cmn::KeyFrameIntervalType VideoTrack::GetKeyFrameIntervalTypeByConfig() const
{
	return _key_frame_interval_type_conf;
}

double VideoTrack::GetKeyframeIntervalDurationMs() const
{
	const auto frame_snapshot = GetFrameSnapshot();

	double keyframe_interval = std::ceil(frame_snapshot.GetKeyFrameInterval());
	double framerate = std::ceil(frame_snapshot.GetFrameRate());

	if (framerate <= 0.0)
	{
		return 0.0;
	}

	return (keyframe_interval / framerate) * 1000.0;
}

void VideoTrack::SetBFrames(int32_t b_frames)
{
	_b_frames = b_frames;
}

int32_t VideoTrack::GetBFrames()
{
	return _b_frames;
}

void VideoTrack::SetColorspace(cmn::VideoPixelFormatId colorspace)
{
	_colorspace = colorspace;
}

cmn::VideoPixelFormatId VideoTrack::GetColorspace() const
{
	return _colorspace;
}

double VideoTrack::GetFrameRate() const
{
	std::shared_lock lock(_video_mutex);
	return _frame_snapshot.framerate_conf.value_or(_frame_snapshot.framerate);
}

void VideoTrack::SetFrameRateByMeasured(double framerate)
{
	std::scoped_lock lock(_video_mutex);
	_frame_snapshot.framerate = framerate;
}

double VideoTrack::GetFrameRateByMeasured() const
{
	std::shared_lock lock(_video_mutex);
	return _frame_snapshot.framerate;
}

void VideoTrack::SetFrameRateLastSecond(double framerate)
{
	_framerate_last_second = framerate;
}

double VideoTrack::GetFrameRateLastSecond() const
{
	return _framerate_last_second;
}

void VideoTrack::SetFrameRateByConfig(double framerate)
{
	std::scoped_lock lock(_video_mutex);

	if (framerate > 0)
	{
		_frame_snapshot.framerate_conf = framerate;
	}
	else
	{
		_frame_snapshot.framerate_conf.reset();
	}
}

double VideoTrack::GetFrameRateByConfig() const
{
	std::shared_lock lock(_video_mutex);
	return _frame_snapshot.framerate_conf.value_or(0.0);
}

void VideoTrack::SetDeltaFrameCountSinceLastKeyFrame(int32_t delta_frame_count)
{
	_delta_frame_count_since_last_key_frame = delta_frame_count;
}

int32_t VideoTrack::GetDeltaFramesSinceLastKeyFrame() const
{
	return _delta_frame_count_since_last_key_frame;
}

void VideoTrack::SetDetectLongKeyFrameInterval(bool detect_long_key_frame_interval)
{
	_detect_long_key_frame_interval = detect_long_key_frame_interval;
}

int32_t VideoTrack::GetDetectLongKeyFrameInterval() const
{
	return _detect_long_key_frame_interval;
}

void VideoTrack::SetDetectAbnormalFramerate(bool detect_abnormal_framerate)
{
	_detect_abnormal_framerate = detect_abnormal_framerate;
}

bool VideoTrack::GetDetectAbnormalFramerate() const
{
	return _detect_abnormal_framerate;
}

void VideoTrack::SetSkipFramesByConfig(int32_t skip_frames)
{
	_skip_frames_conf = skip_frames;
}

int32_t VideoTrack::GetSkipFramesByConfig() const
{
	return _skip_frames_conf;
}

bool VideoTrack::IsKeyframeDecodeOnly() const
{
	return _keyframe_decode_only;
}

void VideoTrack::SetKeyframeDecodeOnly(bool keyframe_decode_only)
{
	_keyframe_decode_only = keyframe_decode_only;
}

void VideoTrack::SetLookaheadByConfig(int32_t lookahead)
{
	_lookahead_conf = lookahead;
}

int32_t VideoTrack::GetLookaheadByConfig() const
{
	return _lookahead_conf;
}

void VideoTrack::SetOverlays(const std::vector<std::shared_ptr<info::Overlay>> &overlays)
{
	std::scoped_lock lock(_overlay_mutex);

	if (overlays.empty())
	{
		_overlays.clear();
		_overlay_signature = 0;
		return;
	}

	_overlays.assign(overlays.begin(), overlays.end());
	_overlay_signature = info::Overlay::GetSignature(overlays);
}

std::vector<std::shared_ptr<info::Overlay>> VideoTrack::GetOverlays() const
{
	std::shared_lock lock(_overlay_mutex);
	return _overlays;
}

size_t VideoTrack::GetOverlaySignature() const
{
	std::shared_lock lock(_overlay_mutex);
	return _overlay_signature;
}

void VideoTrack::SetExtraEncoderOptionsByConfig(const ov::String &options)
{
	std::scoped_lock lock(_video_mutex);
	_extra_encoder_options = options;
}

ov::String VideoTrack::GetExtraEncoderOptionsByConfig() const
{
	std::shared_lock lock(_video_mutex);
	return _extra_encoder_options;
}