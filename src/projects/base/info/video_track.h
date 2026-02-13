//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <modules/bitstream/h264/h264_parser.h>
#include <modules/bitstream/nalu/nal_unit_fragment_header.h>
#include "base/info/overlay.h"
#include "base/common_types.h"

class VideoTrack
{
public:
	VideoTrack();

	// Return the proper framerate for this track. 
	// If there is a framerate set by the user, it is returned. If not, the automatically measured framerate is returned	
	double GetFrameRate() const;

	void SetFrameRateByMeasured(double framerate);
	double GetFrameRateByMeasured() const;

	void AddToMeasuredFramerateWindow(double framerate);
	std::deque<double>  GetMeasuredFramerateWindow() const;

	void SetFrameRateLastSecond(double framerate);
	double GetFrameRateLastSecond() const;

	void SetFrameRateByConfig(double framerate);
	double GetFrameRateByConfig() const;

	void SetWidth(int32_t width);
	void SetMaxWidth(int32_t max_width); // for ovt sync
	int32_t GetWidth() const;
	int32_t GetMaxWidth() const;

	void SetWidthByConfig(int32_t width);
	int32_t GetWidthByConfig() const;

	void SetHeight(int32_t height);
	void SetMaxHeight(int32_t max_height);
	int32_t GetHeight() const;
	int32_t GetMaxHeight() const;

	void SetHeightByConfig(int32_t height);
	int32_t GetHeightByConfig() const;

	void SetVideoTimestampScale(double scale);
	double GetVideoTimestampScale() const;

	void SetHasBframes(bool has_bframe);
	bool HasBframes() const;

	void SetColorspace(cmn::VideoPixelFormatId colorspace);
	cmn::VideoPixelFormatId GetColorspace() const;	

	void SetPreset(ov::String preset);
	ov::String GetPreset() const;

	void SetProfile(ov::String profile);
	ov::String GetProfile() const;

	void SetThreadCount(int thread_count);
	int GetThreadCount();

	// Return the proper key_frame_interval for this track. 
	// If there is a key_frame_interval set by the user, it is returned. If not, the automatically measured key_frame_interval is returned
	double GetKeyFrameInterval() const;

	void SetKeyFrameIntervalByMeasured(double key_frame_interval);
	double GetKeyFrameIntervalByMeasured() const;

	void SetKeyFrameIntervalLastet(double key_frame_interval);
	double GetKeyFrameIntervalLatest() const;
	
	void SetKeyFrameIntervalByConfig(int32_t key_frame_interval);
	double GetKeyFrameIntervalByConfig() const;

	double GetKeyframeIntervalDurationMs() const;

	void SetKeyFrameIntervalTypeByConfig(cmn::KeyFrameIntervalType key_frame_interval_type);
	cmn::KeyFrameIntervalType GetKeyFrameIntervalTypeByConfig() const;

	void SetDeltaFrameCountSinceLastKeyFrame(int32_t delta_frame_count);
	int32_t GetDeltaFramesSinceLastKeyFrame() const;

	void SetDetectLongKeyFrameInterval(bool detect_long_key_frame_interval);
	int32_t GetDetectLongKeyFrameInterval() const;

	void SetDetectAbnormalFramerate(bool detect_abnormal_framerate);
	bool GetDetectAbnormalFramerate() const;

	void SetBFrames(int32_t b_frames);
	int32_t GetBFrames();

	void SetSkipFramesByConfig(int32_t skip_frames);
	int32_t GetSkipFramesByConfig() const;

	// decoder only parameter
	bool IsKeyframeDecodeOnly() const;
	void SetKeyframeDecodeOnly(bool keyframe_decode_only);

	void SetLookaheadByConfig(int32_t lookahead);
	int32_t GetLookaheadByConfig() const;
	
	void SetExtraEncoderOptionsByConfig(const ov::String &options);
	ov::String GetExtraEncoderOptionsByConfig() const;

protected:

	// framerate (measurement)
	double _framerate = 0;
	// framerate (set by user)
	double _framerate_conf = 0;
	// framerate last one second (measurement)
	double _framerate_last_second = 0;

	double _video_timescale;
	
	// Resolution
	int32_t _width = 0;
	int32_t _height = 0;
	int32_t _max_width = 0;
	int32_t _max_height = 0;

	// Resolution (set by user)
	int32_t _width_conf = 0;
	int32_t _height_conf = 0;

	// Key Frame Interval Avg (measurement)
	double _key_frame_interval = 0;
	// Key Frame Interval Latest (measurement)
	double _key_frame_interval_latest = 0;
	// Key Frame Interval (set by user)
	double _key_frame_interval_conf = 0;
	// Delta Frame Count since last key frame
	int32_t _delta_frame_count_since_last_key_frame = 0;

	// Detect long key frame interval (set by mediarouter)
	bool _detect_long_key_frame_interval = false;

	// Key Frame Interval Type (set by user)
	cmn::KeyFrameIntervalType _key_frame_interval_type_conf = cmn::KeyFrameIntervalType::FRAME;

	// Number of B-frame (set by user)
	int32_t _b_frames = 0;
	
	// B-frame (set by mediarouter)
	bool _has_bframe = false;

	// Colorspace of video
	// This variable is temporarily used in the Pixel Format defined by FFMPEG.
	cmn::VideoPixelFormatId _colorspace = cmn::VideoPixelFormatId::None;	

	// Preset for encoder (set by user)
	ov::String _preset;

	// Profile (set by user, used for h264, h265 codec)
	ov::String _profile;
	
	// Thread count of codec (set by user)
	int _thread_count = 0;	

	// Skip frames (set by user)
	// If the set value is greater than or equal to 0, the skip frame is automatically calculated. 
	// The skip frame is not less than the value set by the user.
	// -1 : No SkipFrame
	// 0 ~ 120 : minimum value of SkipFrames. it is automatically calculated and the SkipFrames value is changed.
	int32_t _skip_frames_conf = -1;

	// @decoder
	// Keyframe Decode Only (set by user)
	bool _keyframe_decode_only = false;

	// @encoder
	// Lookahead (set by user)
	int32_t _lookahead_conf = -1;

	// Abnormal key frame interval detection
	bool _detect_abnormal_framerate = false;
	std::deque<double>  _measured_framerate_window;

	ov::String _extra_encoder_options;
public:
	// Overlay (set by user)
	void SetOverlays(const std::vector<std::shared_ptr<info::Overlay>> &overlays);
	std::vector<std::shared_ptr<info::Overlay>> GetOverlays() const;
	size_t GetOverlaySignature() const;
 
protected:
	std::vector<std::shared_ptr<info::Overlay>> _overlays;
	size_t _overlay_signature; // Default is 0, meaning no overlay.
	mutable std::shared_mutex _overlay_mutex;
};
