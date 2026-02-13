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
	: _framerate(0),
	  _framerate_conf(0),
	  _framerate_last_second(0),
	  _video_timescale(0),
	  _width(0),
	  _height(0),
	  _width_conf(0),
	  _height_conf(0),
	  _key_frame_interval(0),
	  _key_frame_interval_latest(0),
	  _key_frame_interval_conf(0),
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

void VideoTrack::SetWidth(int32_t width)
{
	_width = width;

	if (width > _max_width)
	{
		_max_width = width;
	}
}

void VideoTrack::SetMaxWidth(int32_t max_width)
{
	_max_width = max_width;
}

int32_t VideoTrack::GetWidth() const
{
	return _width;
}

int32_t VideoTrack::GetMaxWidth() const
{
	return _max_width;
}

void VideoTrack::SetHeight(int32_t height)
{
	_height = height;

	if (height > _max_height)
	{
		_max_height = height;
	}
}

void VideoTrack::SetMaxHeight(int32_t max_height)
{
	_max_height = max_height;
}

int32_t VideoTrack::GetHeight() const
{
	return _height;
}

int32_t VideoTrack::GetMaxHeight() const
{
	return _max_height;
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
	_preset = preset;
}

ov::String VideoTrack::GetPreset() const
{
	return _preset;
}

void VideoTrack::SetProfile(ov::String profile)
{
	_profile = profile;
}

ov::String VideoTrack::GetProfile() const
{
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

double VideoTrack::GetKeyFrameInterval() const
{
	if(_key_frame_interval_conf > 0)
	{
		return _key_frame_interval_conf;
	}

	return _key_frame_interval;
}

void VideoTrack::SetKeyFrameIntervalByMeasured(double key_frame_interval)
{
	_key_frame_interval = key_frame_interval;
}

double VideoTrack::GetKeyFrameIntervalByMeasured() const
{
	return _key_frame_interval;
}

void VideoTrack::AddToMeasuredFramerateWindow(double framerate)
{
	size_t kAbnormalFpsCheckWindowSize = 60;

	_measured_framerate_window.push_back(framerate);

	if (_measured_framerate_window.size() > kAbnormalFpsCheckWindowSize)
	{
		_measured_framerate_window.pop_front();
	}
}

std::deque<double> VideoTrack::GetMeasuredFramerateWindow() const
{
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
	_key_frame_interval_conf = key_frame_interval;
}

double VideoTrack::GetKeyFrameIntervalByConfig() const
{
	return _key_frame_interval_conf;
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
	double keyframe_interval = std::ceil(GetKeyFrameInterval());
	double framerate = std::ceil(GetFrameRate());

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
	if(_framerate_conf > 0)
	{
		return _framerate_conf;
	}

	return _framerate;
}

void VideoTrack::SetFrameRateByMeasured(double framerate)
{
	_framerate = framerate;
}

double VideoTrack::GetFrameRateByMeasured() const
{
	return _framerate;
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
	_framerate_conf = framerate;
}

double VideoTrack::GetFrameRateByConfig() const
{
	return _framerate_conf;
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

void VideoTrack::SetWidthByConfig(int32_t width)
{
	_width_conf = width;
}
int32_t VideoTrack::GetWidthByConfig() const
{
	return _width_conf;
}

void VideoTrack::SetHeightByConfig(int32_t height)
{
	_height_conf = height;
}

int32_t VideoTrack::GetHeightByConfig() const
{
	return _height_conf;
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
	std::unique_lock<std::shared_mutex> lock(_overlay_mutex);

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
	std::shared_lock<std::shared_mutex> lock(_overlay_mutex);
	return _overlays;
}

size_t VideoTrack::GetOverlaySignature() const
{
	std::shared_lock<std::shared_mutex> lock(_overlay_mutex);
	return _overlay_signature;
}

void VideoTrack::SetExtraEncoderOptionsByConfig(const ov::String &options)
{
	_extra_encoder_options = options;
}

ov::String VideoTrack::GetExtraEncoderOptionsByConfig() const
{
	return _extra_encoder_options;
}