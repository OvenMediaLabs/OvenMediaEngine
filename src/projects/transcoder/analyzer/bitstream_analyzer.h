//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
//
// Reads a MediaPacket to learn about it, never changing or splitting it:
//
//   - Video: reads the resolution. Audio: reads the sample rate and channels.
//   - Counts how many frames are in the packet.
//   - Warns once if a packet has more than one frame (the rest of the pipeline
//     expects exactly one).
//
// Codecs: H.264, H.265, AV1, VP8 (video), AAC, MP3, MP2, Opus (audio).
//
#pragma once

#include <base/mediarouter/media_buffer.h>
#include <base/mediarouter/media_type.h>

#include <cstdint>
#include <memory>

class BitstreamAnalyzer
{
public:
	BitstreamAnalyzer()	 = default;
	~BitstreamAnalyzer() = default;

	// Chooses the codec to analyze. Returns false if the codec is not supported.
	bool Init(cmn::MediaCodecId codec_id);

	// True once Init() has succeeded.
	bool IsValid() const noexcept
	{
		return _valid;
	}

	// Analyzes one packet and updates the results below. Does not change the packet.
	// Returns false if the analyzer is not ready or the packet is empty.
	bool Analyze(const std::shared_ptr<const MediaPacket> &packet);

	// Runs Analyze() and returns true if the stream format changed from the
	// previous packet (video: width/height, audio: sample rate/channels). The
	// first successful analysis only sets the baseline, so it returns false.
	// Returns false when Analyze() fails.
	bool IsFormatChanged(const std::shared_ptr<const MediaPacket> &packet);

	// Results from the last Analyze() call.
	int GetWidth() const noexcept
	{
		return _width;
	}  // video only
	int GetHeight() const noexcept
	{
		return _height;
	}  // video only
	int GetSampleRate() const noexcept
	{
		return _sample_rate;
	}  // audio only
	int GetChannels() const noexcept
	{
		return _channels;
	}  // audio only
	int64_t GetPts() const noexcept
	{
		return _pts;
	}
	int64_t GetDts() const noexcept
	{
		return _dts;
	}

	// How many frames the packet held (1 is normal). More than one is unexpected.
	int GetFrameCount() const noexcept
	{
		return _frame_count;
	}
	bool IsMultiFrame() const noexcept
	{
		return _frame_count > 1;
	}

	// A short, human-readable summary of the last Analyze() result (codec,
	// resolution or sample rate/channels, pts/dts, frame count).
	ov::String GetInfoString() const;

	// Resets everything back to the uninitialized state.
	void Close();

private:
	// Single pass over the buffer: reads width/height (from SPS / sequence header)
	// and returns how many frames are packed in it (at least 1).
	int AnalyzeVideo(const uint8_t *buf, int buf_size);

	// Single pass over the buffer: reads sample rate / channels and returns how
	// many frames are packed in it (at least 1).
	int AnalyzeAudio(const uint8_t *buf, int buf_size);

	cmn::MediaCodecId _codec_id = cmn::MediaCodecId::None;
	bool _valid					= false;

	int _width					= 0;
	int _height					= 0;

	int _sample_rate			= 0;
	int _channels				= 0;

	int64_t _pts				= 0;
	int64_t _dts				= 0;

	int _frame_count			= 0;

	// Warn about multi-frame packets only once, so the log is not flooded.
	bool _multi_frame_warned	= false;
};
