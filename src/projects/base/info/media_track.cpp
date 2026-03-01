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
	  _bitrate(0),
	  _bitrate_conf(0),
	  _byass(false),
	  _bypass_conf(false),
	  _start_frame_time(0),
	  _last_frame_time(0)
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
	std::scoped_lock(
		_media_mutex, media_track._media_mutex,
		_video_mutex, media_track._video_mutex,
		_audio_mutex, media_track._audio_mutex
	);
	
	if (_id != media_track.GetId())
	{
		return false;
	}

	// common
	_media_type = media_track._media_type.load();

	_codec_id = media_track._codec_id.load();
	_codec_module_id = media_track._codec_module_id.load();

	_public_name = media_track._public_name;
	_variant_name = media_track._variant_name;
	_language = media_track._language;
	_characteristics = media_track._characteristics;

	_time_base = media_track._time_base;

	_bitrate = media_track._bitrate.load();
	_bitrate_conf = media_track._bitrate_conf.load();

	_byass = media_track._byass.load();
	_bypass_conf = media_track._bypass_conf.load();

	_start_frame_time = 0;
	_last_frame_time = 0;

	std::atomic_store(&_decoder_configuration_record, std::atomic_load(&media_track._decoder_configuration_record));

	_origin_bitstream_format = media_track._origin_bitstream_format.load();

	// Video
	_frame_snapshot = media_track._frame_snapshot;
	_resolution = media_track._resolution;
	_max_resolution = media_track._max_resolution;
	_resolution_conf = media_track._resolution_conf;

	// Audio
	_sample = media_track._sample;
	_channel_layout = media_track._channel_layout;
	_audio_timescale = media_track._audio_timescale.load();
	_audio_samples_per_frame = media_track._audio_samples_per_frame.load();

	// Subtitle
	_auto_select = media_track._auto_select;
	_default = media_track._default;
	_forced = media_track._forced;
	_engine = media_track._engine;
	_model = media_track._model;
	_source_language = media_track._source_language;
	_translation = media_track._translation;

	return true;
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
	std::scoped_lock lock(_media_mutex);
	_variant_name = name;
}

ov::String MediaTrack::GetVariantName() const
{
	std::shared_lock lock(_media_mutex);
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
	std::scoped_lock lock(_media_mutex);
	_public_name = name;
}
ov::String MediaTrack::GetPublicName() const
{
	std::shared_lock lock(_media_mutex);
	return _public_name;
}

// Language (rfc5646)
void MediaTrack::SetLanguage(const ov::String &language)
{
	std::scoped_lock lock(_media_mutex);
	_language = language;
}
ov::String MediaTrack::GetLanguage() const
{
	std::shared_lock lock(_media_mutex);
	return _language;
}

// Characteristics (e.g. "main", "sign", "visually-impaired")
void MediaTrack::SetCharacteristics(const ov::String &characteristics)
{
	std::scoped_lock lock(_media_mutex);
	_characteristics = characteristics;
}

ov::String MediaTrack::GetCharacteristics() const
{
	std::shared_lock lock(_media_mutex);
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
	std::scoped_lock lock(_media_mutex);
	_codec_modules = modules;
}

ov::String MediaTrack::GetCodecModules() const
{
	std::shared_lock lock(_media_mutex);
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
	std::shared_lock lock(_media_mutex);
	return _time_base;
}

void MediaTrack::SetTimeBase(int32_t num, int32_t den)
{
	std::scoped_lock lock(_media_mutex);
	_time_base.Set(num, den);
}

void MediaTrack::SetTimeBase(const cmn::Timebase &time_base)
{
	std::scoped_lock lock(_media_mutex);
	_time_base = time_base;
}

bool MediaTrack::IsValidTimeBase() const
{
	std::shared_lock lock(_media_mutex);
	return _time_base.IsValid();
}

void MediaTrack::SetStartFrameTime(int64_t time)
{
	_start_frame_time = time;
}

int64_t MediaTrack::GetStartFrameTime() const
{
	return _start_frame_time;
}

void MediaTrack::SetLastFrameTime(int64_t time)
{
	_last_frame_time = time;
}

int64_t MediaTrack::GetLastFrameTime() const
{
	return _last_frame_time;
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

ov::String MediaTrack::GetCodecsParameter() const
{
	switch (GetCodecId())
	{
		case cmn::MediaCodecId::H264:
		case cmn::MediaCodecId::H265:
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

	switch (GetMediaType())
	{
		case MediaType::Video:
			out_str.AppendFormat(
				"Video Track #%d: "
				"Public Name(%s) "
				"Variant Name(%s) "
				"Bitrate(%s) "
				"Codec(%s,%s:%d) "
				"BSF(%s) "
				"Resolution(%s) "
				"MaxResolution(%s) "
				"Framerate(%.2f) "
				"KeyInterval(%.2f/%s) "
				"SkipFrames(%d) "
				"BFrames(%d) ",
				GetId(), GetPublicName().CStr(), GetVariantName().CStr(),
				ov::Converter::BitToString(GetBitrate()).CStr(),
				cmn::GetCodecIdString(GetCodecId()), IsBypass()?"Passthrough":cmn::GetCodecModuleIdString(GetCodecModuleId()), GetCodecDeviceId(),
				GetBitstreamFormatString(GetOriginBitstream()),
				GetResolution().ToString().CStr(),
				GetMaxResolution().ToString().CStr(),
				GetFrameRate(),
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
				"Codec(%s,%s:%d) "
				"BSF(%s) "
				"Samplerate(%s) "
				"Format(%s) "
				"Channel(%s) ",
				GetId(), GetPublicName().CStr(), GetVariantName().CStr(),
				ov::Converter::BitToString(GetBitrate()).CStr(),
				cmn::GetCodecIdString(GetCodecId()), IsBypass()?"Passthrough":cmn::GetCodecModuleIdString(GetCodecModuleId()), GetCodecDeviceId(),
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
				"Codec(%s,%s) "
				"BSF(%s) ",
				GetId(), GetPublicName().CStr(), GetVariantName().CStr(),
				cmn::GetCodecIdString(GetCodecId()), IsBypass()?"Passthrough":cmn::GetCodecModuleIdString(GetCodecModuleId()),
				GetBitstreamFormatString(GetOriginBitstream()));
			break;
		case MediaType::Subtitle:
			out_str.AppendFormat(
				"Subtitle Track #%d: "
				"Public Name(%s) "
				"Variant Name(%s) "
				"Codec(%s,%s:%d) "
				"BSF(%s) ",
				GetId(), GetPublicName().CStr(), GetVariantName().CStr(),
				cmn::GetCodecIdString(GetCodecId()), IsBypass()?"Passthrough":cmn::GetCodecModuleIdString(GetCodecModuleId()), GetCodecDeviceId(),
				GetBitstreamFormatString(GetOriginBitstream()));
			break;

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
	if(GetMediaType() == MediaType::Data || 
		GetMediaType() == MediaType::Subtitle)
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
			if (IsValidTimeBase() && IsValidChannel() && GetDecoderConfigurationRecord() != nullptr)
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
		case MediaCodecId::Mp3: {
			if (IsValidTimeBase() && IsValidChannel())
			{
				_is_valid = true;
				return true;
			}
		}
		break;

		default:
			break;
	}

	return false;
}

bool MediaTrack::HasQualityMeasured()
{
	std::scoped_lock lock(_media_mutex, _video_mutex);
	
	if (_has_quality_measured == true)
	{
		return true;
	}

	switch (GetMediaType())
	{
		case MediaType::Video:
		{
			// It can be used when the value is set in the provider or settings, or when it is measured.
			if ((_bitrate > 0 || _bitrate_conf > 0) && (_frame_snapshot.GetFrameRate() > 0.0))
			{
				_has_quality_measured = true;
			}
		}
		break;

		case MediaType::Audio:
		{
			if (_bitrate > 0 || _bitrate_conf > 0)
			{
				_has_quality_measured = true;
			}
		}
		break;

		default:
			_has_quality_measured = true;
			break;
	}

	return _has_quality_measured;
}

void MediaTrack::OnFrameAdded(const std::shared_ptr<MediaPacket> &media_packet)
{
	if (_clock_from_first_frame_received.IsStart() == false)
	{
		_clock_from_first_frame_received.Start();
	}

	if (_timer_one_second.IsStart() == false)
	{
		_timer_one_second.Start();
	}

	size_t bytes = media_packet->GetDataLength();

	_total_frame_count++;
	_total_frame_bytes += bytes;

	_last_frame_count++;
	_last_frame_bytes += bytes;

	_last_seconds_frame_count++;
	_last_seconds_frame_bytes += bytes;

	// Calculate the framerate and bitrate every second(base on packet timestamp).
	if (_last_received_timestamp == 0)
	{
		_last_received_timestamp = media_packet->GetDts();
	}
	else
	{
		auto duration = (media_packet->GetDts() - _last_received_timestamp) * _time_base.GetExpr();
		if (duration >= 1.0)
		{
			auto bitrate = static_cast<int32_t>(_last_frame_bytes / duration * 8);
			SetBitrateByMeasured(bitrate);

			auto framerate = static_cast<double>(_last_frame_count) / duration;
			SetFrameRateByMeasured(framerate);

			_last_received_timestamp = media_packet->GetDts();
			_last_frame_count = 0;
			_last_frame_bytes = 0;
		}
	}

	// Calculate the framerate and bitrate every second(base on the system clock).
	if (_timer_one_second.IsElapsed(1000))
	{
		// It can be greater than 1 second due to the delay of the timer or the processing time of the frame.
		auto seconds = static_cast<double>(_timer_one_second.Elapsed()) / 1000.0;

		auto bitrate = static_cast<int32_t>(_last_seconds_frame_bytes * 8) / seconds;
		SetBitrateLastSecond(bitrate);

		auto framerate = static_cast<double>(_last_seconds_frame_count) / seconds;
		SetFrameRateLastSecond(framerate);

		_last_seconds_frame_count = 0;
		_last_seconds_frame_bytes = 0;

		_timer_one_second.Restart();
	}

	if (GetMediaType() == cmn::MediaType::Video)
	{
		if (media_packet->GetFlag() == MediaPacketFlag::Key)
		{
			_total_key_frame_count++;
			auto key_frame_interval_avg = static_cast<double>(_total_frame_count) / static_cast<double>(_total_key_frame_count);

			// Average
			SetKeyFrameIntervalByMeasured(key_frame_interval_avg);

			// Lastest
			SetKeyFrameIntervalLastet(_key_frame_interval_count);
			_key_frame_interval_count = 1;
			_delta_frame_count_since_last_key_frame = 0;
		}
		else if (_key_frame_interval_count > 0)
		{
			_key_frame_interval_count++;
			_delta_frame_count_since_last_key_frame ++;
			SetDeltaFrameCountSinceLastKeyFrame(_delta_frame_count_since_last_key_frame);
		}
	}
}

int64_t MediaTrack::GetTotalFrameCount() const
{
	return _total_frame_count;
}

int64_t MediaTrack::GetTotalFrameBytes() const
{
	return _total_frame_bytes;
}

// void MediaTrack::SetBitrate(int32_t bitrate)
// {
// 	_bitrate = bitrate;
// }

int32_t MediaTrack::GetBitrate() const
{
	if (_bitrate_conf > 0)
	{
		return _bitrate_conf;
	}

	return _bitrate;
}

void MediaTrack::SetBitrateByMeasured(int32_t bitrate)
{
	_bitrate = bitrate;
}

int32_t MediaTrack::GetBitrateByMeasured() const
{
	return _bitrate;
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
	_bitrate_last_second = bitrate;
}

int32_t MediaTrack::GetBitrateLastSecond() const
{
	return _bitrate_last_second;
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