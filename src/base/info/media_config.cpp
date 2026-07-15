//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#include "media_config.h"

#include "media_track.h"

std::shared_ptr<MediaConfig> MediaConfig::FromMediaTrack(const MediaTrack &track, uint32_t version, uint32_t msid)
{
	auto config = std::make_shared<MediaConfig>();

	config->_version = version;
	config->_msid = msid;

	config->_media_type = track.GetMediaType();
	config->_codec_id = track.GetCodecId();
	config->_time_base = track.GetTimeBase();

	config->_decoder_configuration_record = track.GetDecoderConfigurationRecord();

	config->_resolution = track.GetResolution();

	config->_sample = track.GetSample();
	config->_channel = track.GetChannel();
	config->_audio_samples_per_frame = track.GetAudioSamplesPerFrame();

	return config;
}

uint32_t MediaConfig::GetVersion() const
{
	return _version;
}

uint32_t MediaConfig::GetMsid() const
{
	return _msid;
}

cmn::MediaType MediaConfig::GetMediaType() const
{
	return _media_type;
}

cmn::MediaCodecId MediaConfig::GetCodecId() const
{
	return _codec_id;
}

const cmn::Timebase &MediaConfig::GetTimeBase() const
{
	return _time_base;
}

std::shared_ptr<DecoderConfigurationRecord> MediaConfig::GetDecoderConfigurationRecord() const
{
	return _decoder_configuration_record;
}

const cmn::Resolution &MediaConfig::GetResolution() const
{
	return _resolution;
}

const cmn::AudioSample &MediaConfig::GetSample() const
{
	return _sample;
}

const cmn::AudioChannel &MediaConfig::GetChannel() const
{
	return _channel;
}

int MediaConfig::GetAudioSamplesPerFrame() const
{
	return _audio_samples_per_frame;
}

bool MediaConfig::HasSameContent(const std::shared_ptr<const MediaConfig> &other) const
{
	if (other == nullptr)
	{
		return false;
	}

	if (_media_type != other->_media_type ||
		_codec_id != other->_codec_id ||
		_time_base != other->_time_base)
	{
		return false;
	}

	if (_media_type == cmn::MediaType::Video)
	{
		if ((_resolution == other->_resolution) == false)
		{
			return false;
		}
	}
	else if (_media_type == cmn::MediaType::Audio)
	{
		if (_sample.GetFormat() != other->_sample.GetFormat() ||
			_sample.GetRateNum() != other->_sample.GetRateNum() ||
			_channel.GetLayout() != other->_channel.GetLayout() ||
			_audio_samples_per_frame != other->_audio_samples_per_frame)
		{
			return false;
		}
	}

	auto other_record = other->_decoder_configuration_record;
	if (_decoder_configuration_record == nullptr || other_record == nullptr)
	{
		return _decoder_configuration_record == other_record;
	}

	return _decoder_configuration_record->Equals(other_record);
}

ov::String MediaConfig::GetInfoString() const
{
	ov::String info;

	info.AppendFormat("Version(%u) Msid(%u) Codec(%s) Timebase(%s) ",
					  _version, _msid, cmn::GetCodecIdString(_codec_id), _time_base.GetStringExpr().CStr());

	if (_media_type == cmn::MediaType::Video)
	{
		info.AppendFormat("Resolution(%dx%d) ", _resolution.width, _resolution.height);
	}
	else if (_media_type == cmn::MediaType::Audio)
	{
		info.AppendFormat("SampleRate(%d) Channel(%s) ", _sample.GetRateNum(), _channel.GetName());
	}

	info.AppendFormat("DCR(%s)", _decoder_configuration_record != nullptr ? "set" : "none");

	return info;
}
