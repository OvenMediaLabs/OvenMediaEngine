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
	: _max_framerate(0),
	  _video_timescale(0),
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
	ov::ScopedLock lock(_video_mutex);

	_resolution			   = resolution;
	_max_resolution.width  = std::max(_max_resolution.width, _resolution.width);
	_max_resolution.height = std::max(_max_resolution.height, _resolution.height);

	OV_ASSERT(_max_resolution.width >= _resolution.width, "Invalid resolution invariant: max width must be >= width");
	OV_ASSERT(_max_resolution.height >= _resolution.height, "Invalid resolution invariant: max height must be >= height");
}

cmn::Resolution VideoTrack::GetResolution() const
{
	ov::SharedLockGuard lock(_video_mutex);
	return _resolution;
}

void VideoTrack::SetMaxResolution(int32_t max_width, int32_t max_height)
{
	SetMaxResolution(cmn::Resolution{max_width, max_height});
}

void VideoTrack::SetMaxResolution(const cmn::Resolution &max_resolution)
{
	ov::ScopedLock lock(_video_mutex);

	_max_resolution.width  = std::max(std::max(_max_resolution.width, _resolution.width), max_resolution.width);
	_max_resolution.height = std::max(std::max(_max_resolution.height, _resolution.height), max_resolution.height);

	OV_ASSERT(_max_resolution.width >= _resolution.width, "Invalid resolution invariant: max width must be >= width");
	OV_ASSERT(_max_resolution.height >= _resolution.height, "Invalid resolution invariant: max height must be >= height");
}

cmn::Resolution VideoTrack::GetMaxResolution() const
{
	ov::SharedLockGuard lock(_video_mutex);
	return _max_resolution;
}

void VideoTrack::SetResolutionByConfig(int32_t width, int32_t height)
{
	SetResolutionByConfig(cmn::Resolution{width, height});
}

void VideoTrack::SetResolutionByConfig(const cmn::Resolution &resolution)
{
	ov::ScopedLock lock(_video_mutex);
	_resolution_conf = resolution;
}

cmn::Resolution VideoTrack::GetResolutionByConfig() const
{
	ov::SharedLockGuard lock(_video_mutex);
	return _resolution_conf;
}

bool VideoTrack::IsValidResolution() const
{
	ov::SharedLockGuard lock(_video_mutex);

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
	ov::ScopedLock lock(_video_mutex);
	_preset = preset;
}

ov::String VideoTrack::GetPreset() const
{
	ov::SharedLockGuard lock(_video_mutex);
	return _preset;
}

void VideoTrack::SetProfile(ov::String profile)
{
	ov::ScopedLock lock(_video_mutex);
	_profile = profile;
}

ov::String VideoTrack::GetProfile() const
{
	ov::SharedLockGuard lock(_video_mutex);
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

void VideoTrack::SetKeyFrameIntervalByConfig(int32_t key_frame_interval)
{
	ov::ScopedLock lock(_video_mutex);

	if (key_frame_interval > 0)
	{
		_key_frame_interval_conf = key_frame_interval;
	}
	else
	{
		_key_frame_interval_conf.reset();
	}
}

double VideoTrack::GetKeyFrameIntervalByConfig() const
{
	ov::SharedLockGuard lock(_video_mutex);
	return _key_frame_interval_conf.value_or(0.0);
}

void VideoTrack::SetKeyFrameIntervalTypeByConfig(cmn::KeyFrameIntervalType key_frame_interval_type)
{
	_key_frame_interval_type_conf = key_frame_interval_type;
}

cmn::KeyFrameIntervalType VideoTrack::GetKeyFrameIntervalTypeByConfig() const
{
	return _key_frame_interval_type_conf;
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

void VideoTrack::SetMaxFrameRate(double framerate)
{
	// Measured framerate is folded in by MediaTrack::SetFrameRateByMeasured()
	_max_framerate = std::max(_max_framerate.load(), framerate);
}

double VideoTrack::GetMaxFrameRate() const
{
	return _max_framerate;
}

void VideoTrack::SetFrameRateByConfig(double framerate)
{
	ov::ScopedLock lock(_video_mutex);

	if (framerate > 0)
	{
		_framerate_conf = framerate;
		_max_framerate = std::max(_max_framerate.load(), framerate);
	}
	else
	{
		_framerate_conf.reset();
	}
}

double VideoTrack::GetFrameRateByConfig() const
{
	ov::SharedLockGuard lock(_video_mutex);
	return _framerate_conf.value_or(0.0);
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
	ov::ScopedLock lock(_overlay_mutex);

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
	ov::SharedLockGuard lock(_overlay_mutex);
	return _overlays;
}

size_t VideoTrack::GetOverlaySignature() const
{
	ov::SharedLockGuard lock(_overlay_mutex);
	return _overlay_signature;
}

void VideoTrack::SetExtraEncoderOptionsByConfig(const ov::String &options)
{
	ov::ScopedLock lock(_video_mutex);
	_extra_encoder_options = options;
}

ov::String VideoTrack::GetExtraEncoderOptionsByConfig() const
{
	ov::SharedLockGuard lock(_video_mutex);
	return _extra_encoder_options;
}