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

#include "decoder_configuration_record.h"

class MediaTrack;

// Immutable snapshot of the codec configuration of a track for one generation.
// It is created by the stream author (MediaRouter inbound / Transcoder output),
// attached to every MediaPacket, and must never be modified after publish.
// Always handle it as std::shared_ptr<const MediaConfig>.
class MediaConfig
{
public:
	// Build a snapshot from the current state of the media track
	static std::shared_ptr<MediaConfig> FromMediaTrack(const MediaTrack &track, uint32_t version, uint32_t msid);

	// Monotonically increasing per track (starts at 1)
	uint32_t GetVersion() const;
	// Msid of the stream when this config was published (label for debugging)
	uint32_t GetMsid() const;

	cmn::MediaType GetMediaType() const;
	cmn::MediaCodecId GetCodecId() const;
	const cmn::Timebase &GetTimeBase() const;

	std::shared_ptr<DecoderConfigurationRecord> GetDecoderConfigurationRecord() const;

	// Video only
	const cmn::Resolution &GetResolution() const;

	// Audio only
	const cmn::AudioSample &GetSample() const;
	const cmn::AudioChannel &GetChannel() const;
	int GetAudioSamplesPerFrame() const;

	// Compares interpretation content only, excluding version/msid labels
	bool HasSameContent(const std::shared_ptr<const MediaConfig> &other) const;

	ov::String GetInfoString() const;

private:
	uint32_t _version = 0;
	uint32_t _msid = 0;

	cmn::MediaType _media_type = cmn::MediaType::Unknown;
	cmn::MediaCodecId _codec_id = cmn::MediaCodecId::None;
	cmn::Timebase _time_base;

	std::shared_ptr<DecoderConfigurationRecord> _decoder_configuration_record = nullptr;

	cmn::Resolution _resolution;

	cmn::AudioSample _sample;
	cmn::AudioChannel _channel;
	int _audio_samples_per_frame = 0;
};
