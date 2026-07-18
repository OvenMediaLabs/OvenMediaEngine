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

class MediaConfigBuilder;

// Immutable snapshot of the codec configuration of a track for one generation.
// It is created by the stream author (MediaRouter inbound / Transcoder output),
// attached to every MediaPacket, and must never be modified after publish.
// Always handle it as std::shared_ptr<const MediaConfig>.
class MediaConfig
{
	friend class MediaConfigBuilder;

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

// Mutable working state of a config author (MediaRouter). It collects content
// values from provider hints and in-band bitstream parsing, and builds the
// immutable MediaConfig snapshots that are published to consumers.
// Not thread-safe: owned and used only by the author's worker thread.
class MediaConfigBuilder
{
public:
	// Take the setup values of the track as the starting point
	void SeedFromTrack(const MediaTrack &track);

	cmn::MediaType GetMediaType() const;

	cmn::MediaCodecId GetCodecId() const;
	void SetCodecId(cmn::MediaCodecId codec_id);

	const cmn::Timebase &GetTimeBase() const;
	void SetTimeBase(const cmn::Timebase &time_base);
	bool IsValidTimeBase() const;

	std::shared_ptr<DecoderConfigurationRecord> GetDecoderConfigurationRecord() const;
	template <typename T, typename = typename std::enable_if<std::is_base_of<DecoderConfigurationRecord, T>::value>::type>
	std::shared_ptr<T> GetDecoderConfigurationRecordAs() const
	{
		return std::dynamic_pointer_cast<T>(_decoder_configuration_record);
	}
	// Content-equal records are ignored (the current object and generation are kept)
	void SetDecoderConfigurationRecord(const std::shared_ptr<DecoderConfigurationRecord> &dcr);

	const cmn::Resolution &GetResolution() const;
	void SetResolution(int32_t width, int32_t height);
	void SetResolution(const cmn::Resolution &resolution);
	bool IsValidResolution() const;

	int32_t GetSampleRate() const;
	void SetSampleRate(int32_t sample_rate);
	void SetSampleFormat(cmn::AudioSample::Format format);
	const cmn::AudioSample &GetSample() const;

	const cmn::AudioChannel &GetChannel() const;
	void SetChannelLayout(cmn::AudioChannel::Layout layout);
	bool IsValidChannel() const;

	void SetAudioSamplesPerFrame(int nbsamples);

	// True when the collected values are enough to describe the track (per codec)
	bool IsComplete() const;

	// Set whenever a setter actually changed a value; cleared by the author
	// after it evaluated a rebuild
	bool IsDirty() const;
	void ClearDirty();

	std::shared_ptr<const MediaConfig> Build(uint32_t version, uint32_t msid) const;

private:
	bool _seeded = false;
	bool _dirty = false;

	cmn::MediaType _media_type = cmn::MediaType::Unknown;
	cmn::MediaCodecId _codec_id = cmn::MediaCodecId::None;
	cmn::Timebase _time_base;

	std::shared_ptr<DecoderConfigurationRecord> _decoder_configuration_record = nullptr;

	cmn::Resolution _resolution;

	cmn::AudioSample _sample;
	cmn::AudioChannel _channel;
	int _audio_samples_per_frame = 0;
};
