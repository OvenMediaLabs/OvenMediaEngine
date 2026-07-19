//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "video_track.h"
#include "audio_track.h"
#include "subtitle_track.h"
#include "track_stats.h"

#include "decoder_configuration_record.h"
#include "base/ovlibrary/tsa/mutex.h"


#define VALID_BITRATE_CALCULATION_THRESHOLD_MSEC (1000)

typedef uint32_t MediaTrackId;

class MediaTrack : public VideoTrack, public AudioTrack, public SubtitleTrack
{
public:
	MediaTrack();
	MediaTrack(const MediaTrack &media_track);
	~MediaTrack();

	bool Update(const MediaTrack &media_track);

	// Version of this track description. A published MediaTrack is immutable;
	// a configuration change is delivered as a new version object attached to
	// the packets (consumers detect the boundary by pointer comparison).
	uint32_t GetVersion() const;
	void SetVersion(uint32_t version);

	// False while this is still the setup skeleton the stream was created with.
	// Published versions start at 1, so a change between published versions is
	// a real configuration change; the first published version arriving is not.
	bool IsPublished() const;

	// Compares the content description only (codec, timebase, DCR, resolution,
	// audio parameters), excluding identity labels, conf values and version
	bool HasSameContent(const MediaTrack &other) const;

	// Track ID
	void SetId(uint32_t id);
	uint32_t GetId() const;
	
	// Codec ID
	void SetCodecId(cmn::MediaCodecId id);
	cmn::MediaCodecId GetCodecId() const;

	// Codec Module ID (Used for transcoder)
	void SetCodecModuleId(cmn::MediaCodecModuleId id);
	cmn::MediaCodecModuleId GetCodecModuleId() const;

	// When using multiple hardware acceleration devices, 
	// this is the value to determine the device. (Used for transcoder)
	void SetCodecDeviceId(cmn::DeviceId id);
	cmn::DeviceId GetCodecDeviceId() const;

	// This is a candidate list of decoder/encoder modules. (Used for transcoder)
	// It is set from 
	// 	- OutputProfiles.HWAccels.Encoder.Modules
	//	- OutputProfiles.HWAccels.Decoder.Modules
	// 	- OutputProfiles.OutputProfile.Encodes.Video.Modules
	void SetCodecModules(const ov::String modules);
	ov::String GetCodecModules() const;

	// Variant Name (used for rendition of playlist)
	void SetVariantName(const ov::String &name);
	ov::String GetVariantName() const;

	// Group Index (used for rendition of playlist). Registration metadata,
	// assigned when the track joins a group; callable on a published track
	void SetGroupIndex(int index) const;
	int GetGroupIndex() const;

	// Public Name (used for multiple audio/video tracks. e.g. multilingual audio)
	void SetPublicName(const ov::String &name);
	ov::String GetPublicName() const;

	// Language (rfc5646)
	void SetLanguage(const ov::String &language);
	ov::String GetLanguage() const;

	// Characteristics (e.g. "main", "sign", "visually-impaired")
	void SetCharacteristics(const ov::String &characteristics);
	ov::String GetCharacteristics() const;

	// Media Type 
	void SetMediaType(cmn::MediaType type);
	cmn::MediaType GetMediaType() const;

	// Origin bitstream format
	void SetOriginBitstream(cmn::BitstreamFormat format);
	cmn::BitstreamFormat GetOriginBitstream() const;

	// Bypass 
	void SetBypass(bool flag);
	bool IsBypass() const;

	// Bypass (Set by user)
	void SetBypassByConfig(bool flag);
	bool IsBypassByConf() const;

	// Timebase 
	cmn::Timebase GetTimeBase() const;
	void SetTimeBase(int32_t num, int32_t den);
	void SetTimeBase(const cmn::Timebase &time_base);
	bool IsValidTimeBase() const;




	// Bitrate (Set by user)
	void SetBitrateByConfig(int32_t bitrate);
	int32_t GetBitrateByConfig() const;

	// Runtime statistics of this track. The owning stream links the same
	// TrackStats instance into every version of the track, so the reference
	// survives version swaps. Read-only for consumers.
	void LinkStats(const std::shared_ptr<TrackStats> &stats) const;
	std::shared_ptr<TrackStats> GetStats() const;

	// Configured value if set, otherwise the measured one (0 before measurement)
	int32_t GetBitrate() const;
	double GetFrameRate() const;
	double GetKeyFrameInterval() const;
	double GetKeyframeIntervalDurationMs() const;

	// Measured values, delegated to the linked statistics
	int32_t GetBitrateByMeasured() const;
	int32_t GetBitrateLastSecond() const;
	double GetFrameRateByMeasured() const;


	bool IsValid() const;

	std::shared_ptr<DecoderConfigurationRecord> GetDecoderConfigurationRecord() const;
	template <typename T, typename = typename std::enable_if<std::is_base_of<DecoderConfigurationRecord, T>::value>::type>
	std::shared_ptr<T> GetDecoderConfigurationRecordAs() const
	{
		return std::dynamic_pointer_cast<T>(GetDecoderConfigurationRecord());
	}
	void SetDecoderConfigurationRecord(const std::shared_ptr<DecoderConfigurationRecord> &dcr);
	
	ov::String GetCodecsParameter() const;








	std::shared_ptr<MediaTrack> Clone() const;

	ov::String GetInfoString() const;

	// Track info for the stream-created log; measured fields (resolution, framerate, bitrate, ...) are shown only once known
	ov::String GetInfoStringForCreated() const;

	// Codec status: set by encoder/decoder after initialization
	using CodecStatus = cmn::CodecStatus;
	void SetCodecStatus(cmn::CodecStatus status);
	cmn::CodecStatus GetCodecStatus() const;

	// Extra info: codec-specific human-readable metadata (e.g. Engine/Model/Source for Whisper)
	void SetExtraInfo(const ov::String &info);
	ov::String GetExtraInfo() const;

	// If false, encoder failure for this track is non-fatal and the stream continues without it.
	void SetEssentialTrack(bool essential);
	bool IsEssentialTrack() const;

protected:
	mutable ov::SharedMutex _media_mutex;

	// Track ID
	std::atomic<uint32_t> _id;

	// Media Type
	std::atomic<cmn::MediaType> _media_type;

	// Codec
	std::atomic<cmn::MediaCodecId> _codec_id;
	std::atomic<cmn::MediaCodecModuleId> _codec_module_id;
	std::atomic<cmn::DeviceId> _codec_device_id;
	ov::String _codec_modules OV_GUARDED_BY(_media_mutex);

	// Variant Name : Original encoder profile that made this track 
	// from <OutputProfile><Encodes>(<Video> || <Audio> || <Image>)<Name>
	ov::String _variant_name OV_GUARDED_BY(_media_mutex);
	mutable std::atomic<int> _group_index = -1;

	// Set by AudioMap or VideoMap or SubtitleMap
	ov::String _public_name OV_GUARDED_BY(_media_mutex);
	ov::String _language OV_GUARDED_BY(_media_mutex);
	ov::String _characteristics OV_GUARDED_BY(_media_mutex);

	// Bitrate (Set by user)
	std::atomic<int32_t> _bitrate_conf = 0;

	// Bitstream format 
	std::atomic<cmn::BitstreamFormat> _origin_bitstream_format = cmn::BitstreamFormat::Unknown;

	// Timebase
	cmn::Timebase _time_base OV_GUARDED_BY(_media_mutex);


	// Bypass
	std::atomic<bool> _byass;
	// Bypass (Set by user)
	std::atomic<bool> _bypass_conf;

	// Validity (lazily computed cache)
	mutable std::atomic<bool> _is_valid = false;


	// Codec specific object
	// AVCDecoderConfigurationRecord, HEVCDecoderConfigurationRecord, AudioSpecificConfig 
	std::shared_ptr<DecoderConfigurationRecord> _decoder_configuration_record = nullptr;

	// Codec status and extra info
	std::atomic<cmn::CodecStatus> _codec_status = cmn::CodecStatus::Unknown;
	ov::String _extra_info OV_GUARDED_BY(_media_mutex);

	// If false, encoder failure for this track is non-fatal and the stream continues without it.
	std::atomic<bool> _essential_track = true;

	// Version number of this description (0 = setup skeleton, 1 = first published)
	std::atomic<uint32_t> _version = 0;

	// Shared measurement object, linked by the owning stream (not part of the
	// description; every version of the track points to the same instance)
	mutable std::shared_ptr<TrackStats> _stats;
};
