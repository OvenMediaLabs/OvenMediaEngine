//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "media_track.h"

#include <base/ovlibrary/converter.h>
#include <base/ovlibrary/ovlibrary.h>

#define OV_LOG_TAG "MediaTrack"

using namespace cmn;

MediaTrack::MediaTrack()
	: _id(0),
	  _media_type(MediaType::Unknown),
	  _codec_id(MediaCodecId::None),
	  _codec_module_id(cmn::MediaCodecModuleId::None),
	  _codec_device_id(0),
	  _codec_modules(""),
	  _bitrate_conf(0),
	  _byass(false),
	  _bypass_conf(false)
{
}

MediaTrack::MediaTrack(const MediaTrack &media_track)
{
	_id = media_track._id.load();
	Update(media_track);	
}

MediaTrack::~MediaTrack()
{
}

// Same ID required
bool MediaTrack::Update(const MediaTrack &media_track)
{
	ov::ScopedLock lock(
		_media_mutex, media_track._media_mutex,
		_video_mutex, media_track._video_mutex,
		_audio_mutex, media_track._audio_mutex,
		_subtitle_mutex, media_track._subtitle_mutex);

	if (_id != media_track.GetId())
	{
		return false;
	}

	// common
	_media_type = media_track._media_type.load();

	_codec_id = media_track._codec_id.load();
	_codec_module_id = media_track._codec_module_id.load();
	_codec_device_id = media_track._codec_device_id.load();
	_codec_modules = media_track._codec_modules;

	_public_name = media_track._public_name;
	_variant_name = media_track._variant_name;
	_group_index = media_track._group_index.load();
	_language = media_track._language;
	_characteristics = media_track._characteristics;

	_time_base = media_track._time_base;

	_bitrate_conf = media_track._bitrate_conf.load();

	_byass = media_track._byass.load();
	_bypass_conf = media_track._bypass_conf.load();

	std::atomic_store(&_decoder_configuration_record, std::atomic_load(&media_track._decoder_configuration_record));

	_origin_bitstream_format = media_track._origin_bitstream_format.load();

	// Video
	_framerate_conf = media_track._framerate_conf;
	_key_frame_interval_conf = media_track._key_frame_interval_conf;
	_key_frame_interval_type_conf = media_track._key_frame_interval_type_conf.load();
	_detect_long_key_frame_interval = media_track._detect_long_key_frame_interval.load();
	_detect_abnormal_framerate = media_track._detect_abnormal_framerate.load();
	_max_framerate = media_track._max_framerate.load();
	_video_timescale = media_track._video_timescale.load();
	_resolution = media_track._resolution;
	_max_resolution = media_track._max_resolution;
	_resolution_conf = media_track._resolution_conf;
	_b_frames = media_track._b_frames.load();
	_colorspace = media_track._colorspace.load();
	_preset = media_track._preset;
	_profile = media_track._profile;
	_thread_count = media_track._thread_count.load();
	_skip_frames_conf = media_track._skip_frames_conf.load();
	_keyframe_decode_only = media_track._keyframe_decode_only.load();
	_lookahead_conf = media_track._lookahead_conf.load();
	_extra_encoder_options = media_track._extra_encoder_options;
	SetOverlays(media_track.GetOverlays());

	// Audio
	_sample = media_track._sample;
	_channel_layout = media_track._channel_layout;
	_audio_timescale = media_track._audio_timescale.load();
	_audio_samples_per_frame = media_track._audio_samples_per_frame.load();

	// Subtitle
	_auto_select = media_track._auto_select.load();
	_default = media_track._default.load();
	_forced = media_track._forced.load();
	_engine = media_track._engine;
	_model = media_track._model;
	_source_language = media_track._source_language;
	_translation = media_track._translation.load();
	_output_track_label = media_track._output_track_label;
	_step_ms = media_track._step_ms.load();
	_length_ms = media_track._length_ms.load();
	_keep_ms = media_track._keep_ms.load();
	_stt_enabled = media_track._stt_enabled.load();

	_codec_status = media_track._codec_status.load();
	_extra_info = media_track._extra_info;
	_essential_track = media_track._essential_track.load();

	return true;
}

void MediaTrack::SetHasBframes(bool has_bframe)
{
	_stats->SetHasBframes(has_bframe);
}

bool MediaTrack::HasBframes() const
{
	return _stats->HasBframes();
}

void MediaTrack::SetId(uint32_t id)
{
	_id = id;
}

uint32_t MediaTrack::GetId() const
{
	return _id;
}

// Track Name (used for Renditions)
void MediaTrack::SetVariantName(const ov::String &name)
{
	ov::ScopedLock lock(_media_mutex);
	_variant_name = name;
}

ov::String MediaTrack::GetVariantName() const
{
	ov::SharedLockGuard lock(_media_mutex);
	if (_variant_name.IsEmpty())
	{
		// If variant name is not set, return media type string
		return cmn::GetMediaTypeString(GetMediaType());
	}

	return _variant_name;
}

void MediaTrack::SetGroupIndex(int index)
{
	_group_index = index;
}

int MediaTrack::GetGroupIndex() const
{
	return _group_index;
}

// Public Name (used for multiple audio/video tracks. e.g. multilingual audio)
void MediaTrack::SetPublicName(const ov::String &name)
{
	ov::ScopedLock lock(_media_mutex);
	_public_name = name;
}
ov::String MediaTrack::GetPublicName() const
{
	ov::SharedLockGuard lock(_media_mutex);
	return _public_name;
}

// Language (rfc5646)
void MediaTrack::SetLanguage(const ov::String &language)
{
	ov::ScopedLock lock(_media_mutex);
	_language = language;
}
ov::String MediaTrack::GetLanguage() const
{
	ov::SharedLockGuard lock(_media_mutex);
	return _language;
}

// Characteristics (e.g. "main", "sign", "visually-impaired")
void MediaTrack::SetCharacteristics(const ov::String &characteristics)
{
	ov::ScopedLock lock(_media_mutex);
	_characteristics = characteristics;
}

ov::String MediaTrack::GetCharacteristics() const
{
	ov::SharedLockGuard lock(_media_mutex);
	return _characteristics;
}

void MediaTrack::SetMediaType(MediaType type)
{
	_media_type = type;
}

MediaType MediaTrack::GetMediaType() const
{
	return _media_type;
}

void MediaTrack::SetCodecId(MediaCodecId id)
{
	_codec_id = id;
}

MediaCodecId MediaTrack::GetCodecId() const
{
	return _codec_id;
}

void MediaTrack::SetCodecModuleId(cmn::MediaCodecModuleId id)
{
	_codec_module_id = id;
}

cmn::MediaCodecModuleId MediaTrack::GetCodecModuleId() const
{
	return _codec_module_id;
}

void MediaTrack::SetCodecDeviceId(cmn::DeviceId id)
{
	_codec_device_id = id;
}

cmn::DeviceId MediaTrack::GetCodecDeviceId() const
{
	return _codec_device_id;
}

void MediaTrack::SetCodecModules(ov::String modules)
{
	ov::ScopedLock lock(_media_mutex);
	_codec_modules = modules;
}

ov::String MediaTrack::GetCodecModules() const
{
	ov::SharedLockGuard lock(_media_mutex);
	return _codec_modules;
}

void MediaTrack::SetOriginBitstream(cmn::BitstreamFormat format)
{
	_origin_bitstream_format = format;
}

cmn::BitstreamFormat MediaTrack::GetOriginBitstream() const
{
	return _origin_bitstream_format;
}

cmn::Timebase MediaTrack::GetTimeBase() const
{
	ov::SharedLockGuard lock(_media_mutex);
	return _time_base;
}

void MediaTrack::SetTimeBase(int32_t num, int32_t den)
{
	ov::ScopedLock lock(_media_mutex);
	_time_base.Set(num, den);
}

void MediaTrack::SetTimeBase(const cmn::Timebase &time_base)
{
	ov::ScopedLock lock(_media_mutex);
	_time_base = time_base;
}

bool MediaTrack::IsValidTimeBase() const
{
	ov::SharedLockGuard lock(_media_mutex);
	return _time_base.IsValid();
}

void MediaTrack::SetStartFrameTime(int64_t time)
{
	_stats->SetStartFrameTime(time);
}

int64_t MediaTrack::GetStartFrameTime() const
{
	return _stats->GetStartFrameTime();
}

void MediaTrack::SetLastFrameTime(int64_t time)
{
	_stats->SetLastFrameTime(time);
}

int64_t MediaTrack::GetLastFrameTime() const
{
	return _stats->GetLastFrameTime();
}

void MediaTrack::SetBypass(bool flag)
{
	_byass = flag;
}

bool MediaTrack::IsBypass() const
{
	return _byass;
}

std::shared_ptr<DecoderConfigurationRecord> MediaTrack::GetDecoderConfigurationRecord() const
{
	return std::atomic_load(&_decoder_configuration_record);
}

void MediaTrack::SetDecoderConfigurationRecord(const std::shared_ptr<DecoderConfigurationRecord> &dcr)
{
	std::atomic_store(&_decoder_configuration_record, dcr);
}

void MediaTrack::SetCodecStatus(cmn::CodecStatus status)
{
	_codec_status = status;
}

cmn::CodecStatus MediaTrack::GetCodecStatus() const
{
	// Bypass tracks have no encoder init; they are always ready.
	if (IsBypass())
	{
		return cmn::CodecStatus::Ready;
	}
	return _codec_status;
}

void MediaTrack::SetExtraInfo(const ov::String &info)
{
	ov::ScopedLock lock(_media_mutex);
	_extra_info = info;
}

ov::String MediaTrack::GetExtraInfo() const
{
	ov::SharedLockGuard lock(_media_mutex);
	return _extra_info;
}

void MediaTrack::SetEssentialTrack(bool essential)
{
	_essential_track = essential;
}

bool MediaTrack::IsEssentialTrack() const
{
	return _essential_track;
}

ov::String MediaTrack::GetCodecsParameter() const
{
	switch (GetCodecId())
	{
		case cmn::MediaCodecId::H264:
		case cmn::MediaCodecId::H265:
		case cmn::MediaCodecId::Av1:
		case cmn::MediaCodecId::Aac:
		{
			auto config = GetDecoderConfigurationRecord();
			if (config != nullptr)
			{
				return config->GetCodecsParameter();
			}
			break;
		}
		
		case cmn::MediaCodecId::Opus:
		{
			// https://developer.mozilla.org/en-US/docs/Web/Media/Formats/codecs_parameter
			// In an MP4 container, the codecs parameter for Opus is "mp4a.ad"
			return "mp4a.ad";
		}

		case cmn::MediaCodecId::Vp8:
		{
			return "vp8";
		}

		case cmn::MediaCodecId::Vp9:
		{
			return "vp9";
		}

		case cmn::MediaCodecId::None:
		default:
			break;
	}

	return "";
}

ov::String MediaTrack::GetInfoString()
{
	ov::String out_str = "";

	const char *codec_status_str = cmn::GetCodecStatusString(GetCodecStatus());

	switch (GetMediaType())
	{
		case MediaType::Video:
			out_str.AppendFormat(
				"Video Track #%d: "
				"Public Name(%s) "
				"Variant Name(%s) "
				"Bitrate(%s) "
				"Codec(%s,%s:%d%s%s) "
				"BSF(%s) "
				"Resolution(%s) "
				"MaxResolution(%s) "
				"Framerate(%.2f) "
				"MaxFramerate(%.2f) "
				"KeyInterval(%.2f/%s) "
				"SkipFrames(%d) "
				"BFrames(%d) ",
				GetId(), GetPublicName().CStr(), GetVariantName().CStr(),
				ov::Converter::BitToString(GetBitrate()).CStr(),
				cmn::GetCodecIdString(GetCodecId()), IsBypass()?"Passthrough":cmn::GetCodecModuleIdString(GetCodecModuleId()), GetCodecDeviceId(),
				codec_status_str ? "," : "", codec_status_str ? codec_status_str : "",
				GetBitstreamFormatString(GetOriginBitstream()),
				GetResolution().ToString().CStr(),
				GetMaxResolution().ToString().CStr(),
				GetFrameRate(), GetMaxFrameRate(),
				GetKeyFrameInterval(),
				cmn::GetKeyFrameIntervalTypeToString(GetKeyFrameIntervalTypeByConfig()),
				GetSkipFramesByConfig(),
				GetBFrames());
			break;

		case MediaType::Audio:
			out_str.AppendFormat(
				"Audio Track #%d: "
				"Public Name(%s) "
				"Variant Name(%s) "
				"Bitrate(%s) "
				"Codec(%s,%s:%d%s%s) "
				"BSF(%s) "
				"Samplerate(%s) "
				"Format(%s) "
				"Channel(%s) ",
				GetId(), GetPublicName().CStr(), GetVariantName().CStr(),
				ov::Converter::BitToString(GetBitrate()).CStr(),
				cmn::GetCodecIdString(GetCodecId()), IsBypass()?"Passthrough":cmn::GetCodecModuleIdString(GetCodecModuleId()), GetCodecDeviceId(),
				codec_status_str ? "," : "", codec_status_str ? codec_status_str : "",
				GetBitstreamFormatString(GetOriginBitstream()),
				ov::Converter::ToSiString(GetSampleRate(), 1).CStr(),
				GetSample().GetName(),
				GetChannel().GetName());
			break;

		case MediaType::Data:
			out_str.AppendFormat(
				"Data  Track #%d: "
				"Public Name(%s) "
				"Variant Name(%s) "
				"Codec(%s,%s%s%s) "
				"BSF(%s) ",
				GetId(), GetPublicName().CStr(), GetVariantName().CStr(),
				cmn::GetCodecIdString(GetCodecId()), IsBypass()?"Passthrough":cmn::GetCodecModuleIdString(GetCodecModuleId()),
				codec_status_str ? "," : "", codec_status_str ? codec_status_str : "",
				GetBitstreamFormatString(GetOriginBitstream()));
			break;

		case MediaType::Subtitle:
		{
			auto extra_info = GetExtraInfo();
			out_str.AppendFormat(
				"Subtitle Track #%d: "
				"Public Name(%s) "
				"Variant Name(%s) "
				"Codec(%s%s%s) "
				"timebase(%s)",
				GetId(), GetPublicName().CStr(), GetVariantName().CStr(),
				cmn::GetCodecIdString(GetCodecId()),
				codec_status_str ? "," : "", codec_status_str ? codec_status_str : "",
				GetTimeBase().ToString().CStr());
			if (extra_info.IsEmpty() == false)
			{
				out_str.AppendFormat(" %s", extra_info.CStr());
			}
			break;
		}

		default:
			break;
	}

	if (GetMediaType() != MediaType::Subtitle)
	{
		out_str.AppendFormat("timebase(%s)", GetTimeBase().ToString().CStr());
	}

	return out_str;
}

ov::String MediaTrack::GetInfoStringForCreated()
{
	ov::String out_str;

	const char *codec_status_str = cmn::GetCodecStatusString(GetCodecStatus());

	switch (GetMediaType())
	{
		case MediaType::Video:
		{
			out_str.AppendFormat("Video Track #%u: Public Name(%s) Variant Name(%s) ",
				GetId(), GetPublicName().CStr(), GetVariantName().CStr());

			// Field order matches GetInfoString(); measured values are shown only once known, so no zeros at creation
			auto bitrate = GetBitrate();
			if (bitrate > 0)
				out_str.AppendFormat("Bitrate(%s) ", ov::Converter::BitToString(bitrate).CStr());

			out_str.AppendFormat("Codec(%s%s%s) BSF(%s) ",
				cmn::GetCodecIdString(GetCodecId()),
				codec_status_str ? "," : "", codec_status_str ? codec_status_str : "",
				GetBitstreamFormatString(GetOriginBitstream()));

			auto resolution = GetResolution();
			if (resolution.width > 0 && resolution.height > 0)
				out_str.AppendFormat("Resolution(%s) ", resolution.ToString().CStr());

			auto max_resolution = GetMaxResolution();
			if (max_resolution.width > 0 && max_resolution.height > 0)
				out_str.AppendFormat("MaxResolution(%s) ", max_resolution.ToString().CStr());

			auto framerate = GetFrameRate();
			if (framerate > 0)
				out_str.AppendFormat("Framerate(%.2f) ", framerate);

			auto max_framerate = GetMaxFrameRate();
			if (max_framerate > 0)
				out_str.AppendFormat("MaxFramerate(%.2f) ", max_framerate);
			break;
		}

		case MediaType::Audio:
		{
			out_str.AppendFormat("Audio Track #%u: Public Name(%s) Variant Name(%s) ",
				GetId(), GetPublicName().CStr(), GetVariantName().CStr());

			auto bitrate = GetBitrate();
			if (bitrate > 0)
				out_str.AppendFormat("Bitrate(%s) ", ov::Converter::BitToString(bitrate).CStr());

			out_str.AppendFormat("Codec(%s%s%s) BSF(%s) ",
				cmn::GetCodecIdString(GetCodecId()),
				codec_status_str ? "," : "", codec_status_str ? codec_status_str : "",
				GetBitstreamFormatString(GetOriginBitstream()));

			auto samplerate = GetSampleRate();
			if (samplerate > 0)
				out_str.AppendFormat("Samplerate(%s) ", ov::Converter::ToSiString(samplerate, 1).CStr());

			auto sample = GetSample();
			if (sample.GetFormat() != cmn::AudioSample::Format::None)
				out_str.AppendFormat("Format(%s) ", sample.GetName());

			if (IsValidChannel())
				out_str.AppendFormat("Channel(%s) ", GetChannel().GetName());
			break;
		}

		case MediaType::Data:
			out_str.AppendFormat(
				"Data  Track #%u: Public Name(%s) Variant Name(%s) Codec(%s%s%s) BSF(%s) ",
				GetId(), GetPublicName().CStr(), GetVariantName().CStr(),
				cmn::GetCodecIdString(GetCodecId()),
				codec_status_str ? "," : "", codec_status_str ? codec_status_str : "",
				GetBitstreamFormatString(GetOriginBitstream()));
			break;

		case MediaType::Subtitle:
		{
			out_str.AppendFormat(
				"Subtitle Track #%u: Public Name(%s) Variant Name(%s) Codec(%s%s%s) ",
				GetId(), GetPublicName().CStr(), GetVariantName().CStr(),
				cmn::GetCodecIdString(GetCodecId()),
				codec_status_str ? "," : "", codec_status_str ? codec_status_str : "");

			auto extra_info = GetExtraInfo();
			if (extra_info.IsEmpty() == false)
				out_str.AppendFormat("%s ", extra_info.CStr());
			break;
		}

		default:
			break;
	}

	out_str.AppendFormat("timebase(%s)", GetTimeBase().ToString().CStr());

	return out_str;
}

bool MediaTrack::IsValid()
{
	if (_is_valid == true)
	{
		return true;
	}

	// data type is always valid
	if (GetMediaType() == MediaType::Data)
	{
		_is_valid = true;
		return true;
	}

	switch (GetCodecId())
	{
		case MediaCodecId::H264: {
			if (IsValidResolution() && IsValidTimeBase() && GetDecoderConfigurationRecord() != nullptr)

			{
				_is_valid = true;
				return true;
			}
		}
		break;
		case MediaCodecId::H265: {
			if (IsValidResolution() && IsValidTimeBase() && GetDecoderConfigurationRecord() != nullptr)
			{
				_is_valid = true;
				return true;
			}
		}
		break;
		case MediaCodecId::Vp8: {
			if (IsValidResolution() && IsValidTimeBase())
			{
				_is_valid = true;
				return true;
			}
		}
		break;
		case MediaCodecId::Av1: {
			// `GetCodecsParameter()` derives the AV1 codecs string from the decoder configuration record,
			// and HLS/LL-HLS playlists consume it directly;
			// require a non-null DCR so the track is not marked valid with an empty CODECS string
			// (aligned with H264/H265).
			if (IsValidResolution() && IsValidTimeBase() && GetDecoderConfigurationRecord() != nullptr)
			{
				_is_valid = true;
				return true;
			}
		}
		break;
		case MediaCodecId::Vp9:
		case MediaCodecId::Flv: {
			if (IsValidResolution() && IsValidTimeBase())
			{
				_is_valid = true;
				return true;
			}
		}
		break;
		case MediaCodecId::Jpeg:
		case MediaCodecId::Png:
		case MediaCodecId::Webp: {
			if (IsValidResolution() && IsValidTimeBase())
			{
				_is_valid = true;
				return true;
			}
		}
		break;
		case MediaCodecId::Aac: {
			if (IsValidTimeBase() && IsValidChannel() && GetSampleRate() > 0 && GetDecoderConfigurationRecord() != nullptr)
			{
				_is_valid = true;
				return true;
			}
		}
		break;
		case MediaCodecId::Opus: {
			auto audio_sample = GetSample();
			if (IsValidTimeBase() && IsValidChannel() && audio_sample.GetRate() == cmn::AudioSample::Rate::R48000)
			{
				_is_valid = true;
				return true;
			}
		}
		break;
		case MediaCodecId::Mp2:
		case MediaCodecId::Mp3: {
			if (IsValidTimeBase() && IsValidChannel())
			{
				_is_valid = true;
				return true;
			}
		}
		break;
		case MediaCodecId::Whisper: {
			if (_codec_status != cmn::CodecStatus::Unknown)
			{
				_is_valid = true;
				return true;
			}
		}
		break;
		case MediaCodecId::WebVTT: {
			_is_valid = true;
			return true;
		}

		default:
			break;
	}

	return false;
}

bool MediaTrack::HasQualityMeasured()
{
	if (_stats->IsQualityMeasured() == true)
	{
		return true;
	}

	switch (GetMediaType())
	{
		case MediaType::Video:
		{
			// It can be used when the value is set in the provider or settings, or when it is measured.
			if ((_stats->GetBitrateByMeasured() > 0 || _bitrate_conf > 0) && (GetFrameRate() > 0.0))
			{
				_stats->SetQualityMeasured();
			}
		}
		break;

		case MediaType::Audio:
		{
			if (_stats->GetBitrateByMeasured() > 0 || _bitrate_conf > 0)
			{
				_stats->SetQualityMeasured();
			}
		}
		break;

		default:
			_stats->SetQualityMeasured();
			break;
	}

	return _stats->IsQualityMeasured();
}

void MediaTrack::OnFrameAdded(const std::shared_ptr<MediaPacket> &media_packet)
{
	_stats->OnFrameAdded(media_packet, GetTimeBase(), GetMediaType());

	if (GetMediaType() == cmn::MediaType::Video)
	{
		// Keep the high-water mark in sync with the measurement
		SetMaxFrameRate(_stats->GetFrameRateByMeasured());
	}
}

int64_t MediaTrack::GetTotalFrameCount() const
{
	return _stats->GetTotalFrameCount();
}

int64_t MediaTrack::GetTotalFrameBytes() const
{
	return _stats->GetTotalFrameBytes();
}

std::shared_ptr<TrackStats> MediaTrack::GetStats() const
{
	return _stats;
}

void MediaTrack::AdoptStats(const std::shared_ptr<TrackStats> &stats)
{
	if (stats != nullptr)
	{
		_stats = stats;
	}
}

int32_t MediaTrack::GetBitrate() const
{
	if (_bitrate_conf > 0)
	{
		return _bitrate_conf;
	}

	return _stats->GetBitrateByMeasured();
}

void MediaTrack::SetBitrateByMeasured(int32_t bitrate)
{
	_stats->SetBitrateByMeasured(bitrate);
}

int32_t MediaTrack::GetBitrateByMeasured() const
{
	return _stats->GetBitrateByMeasured();
}

void MediaTrack::SetBitrateByConfig(int32_t bitrate)
{
	_bitrate_conf = bitrate;
}

int32_t MediaTrack::GetBitrateByConfig() const
{
	return _bitrate_conf;
}

void MediaTrack::SetBitrateLastSecond(int32_t bitrate)
{
	_stats->SetBitrateLastSecond(bitrate);
}

int32_t MediaTrack::GetBitrateLastSecond() const
{
	return _stats->GetBitrateLastSecond();
}

double MediaTrack::GetFrameRate() const
{
	auto framerate_conf = GetFrameRateByConfig();
	if (framerate_conf > 0.0)
	{
		return framerate_conf;
	}

	return _stats->GetFrameRateByMeasured();
}

void MediaTrack::SetFrameRateByMeasured(double framerate)
{
	_stats->SetFrameRateByMeasured(framerate);
	SetMaxFrameRate(framerate);
}

double MediaTrack::GetFrameRateByMeasured() const
{
	return _stats->GetFrameRateByMeasured();
}

void MediaTrack::SetFrameRateLastSecond(double framerate)
{
	_stats->SetFrameRateLastSecond(framerate);
}

double MediaTrack::GetFrameRateLastSecond() const
{
	return _stats->GetFrameRateLastSecond();
}

void MediaTrack::AddToMeasuredFramerateWindow(double framerate)
{
	_stats->AddToMeasuredFramerateWindow(framerate);
}

std::deque<double> MediaTrack::GetMeasuredFramerateWindow() const
{
	return _stats->GetMeasuredFramerateWindow();
}

double MediaTrack::GetKeyFrameInterval() const
{
	auto key_frame_interval_conf = GetKeyFrameIntervalByConfig();
	if (key_frame_interval_conf > 0.0)
	{
		return key_frame_interval_conf;
	}

	return _stats->GetKeyFrameIntervalByMeasured();
}

void MediaTrack::SetKeyFrameIntervalByMeasured(double key_frame_interval)
{
	_stats->SetKeyFrameIntervalByMeasured(key_frame_interval);
}

double MediaTrack::GetKeyFrameIntervalByMeasured() const
{
	return _stats->GetKeyFrameIntervalByMeasured();
}

void MediaTrack::SetKeyFrameIntervalLastet(double key_frame_interval)
{
	_stats->SetKeyFrameIntervalLatest(key_frame_interval);
}

double MediaTrack::GetKeyFrameIntervalLatest() const
{
	return _stats->GetKeyFrameIntervalLatest();
}

void MediaTrack::SetDeltaFrameCountSinceLastKeyFrame(int32_t delta_frame_count)
{
	_stats->SetDeltaFrameCountSinceLastKeyFrame(delta_frame_count);
}

int32_t MediaTrack::GetDeltaFramesSinceLastKeyFrame() const
{
	return _stats->GetDeltaFramesSinceLastKeyFrame();
}

double MediaTrack::GetKeyframeIntervalDurationMs() const
{
	double keyframe_interval = std::ceil(GetKeyFrameInterval());
	double framerate = std::ceil(GetFrameRate());

	if (framerate <= 0.0)
	{
		return 0.0;
	}

	return (keyframe_interval / framerate) * 1000.0;
}

void MediaTrack::SetBypassByConfig(bool flag)
{
	_bypass_conf = flag;
}

bool MediaTrack::IsBypassByConf() const
{
	return _bypass_conf;
}

std::shared_ptr<MediaTrack> MediaTrack::Clone()
{
	return std::make_shared<MediaTrack>(*this);
}
