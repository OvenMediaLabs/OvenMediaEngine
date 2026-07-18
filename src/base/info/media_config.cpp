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

void MediaConfigBuilder::SeedFromTrack(const MediaTrack &track)
{
	if (_seeded)
	{
		return;
	}
	_seeded = true;

	_media_type = track.GetMediaType();
	_codec_id = track.GetCodecId();
	_time_base = track.GetTimeBase();

	_decoder_configuration_record = track.GetDecoderConfigurationRecord();

	_resolution = track.GetResolution();

	_sample = track.GetSample();
	_channel = track.GetChannel();
	_audio_samples_per_frame = track.GetAudioSamplesPerFrame();

	_dirty = true;
}

cmn::MediaType MediaConfigBuilder::GetMediaType() const
{
	return _media_type;
}

cmn::MediaCodecId MediaConfigBuilder::GetCodecId() const
{
	return _codec_id;
}

void MediaConfigBuilder::SetCodecId(cmn::MediaCodecId codec_id)
{
	if (_codec_id == codec_id)
	{
		return;
	}
	_codec_id = codec_id;
	_dirty = true;
}

const cmn::Timebase &MediaConfigBuilder::GetTimeBase() const
{
	return _time_base;
}

void MediaConfigBuilder::SetTimeBase(const cmn::Timebase &time_base)
{
	if (_time_base == time_base)
	{
		return;
	}
	_time_base = time_base;
	_dirty = true;
}

bool MediaConfigBuilder::IsValidTimeBase() const
{
	return _time_base.IsValid();
}

std::shared_ptr<DecoderConfigurationRecord> MediaConfigBuilder::GetDecoderConfigurationRecord() const
{
	return _decoder_configuration_record;
}

void MediaConfigBuilder::SetDecoderConfigurationRecord(const std::shared_ptr<DecoderConfigurationRecord> &dcr)
{
	if (_decoder_configuration_record == dcr)
	{
		return;
	}

	// A content-equal record is the same generation; keep the current object so
	// re-sent identical sequence headers do not trigger a rebuild
	if (_decoder_configuration_record != nullptr && dcr != nullptr &&
		_decoder_configuration_record->Equals(dcr))
	{
		return;
	}

	_decoder_configuration_record = dcr;
	_dirty = true;
}

const cmn::Resolution &MediaConfigBuilder::GetResolution() const
{
	return _resolution;
}

void MediaConfigBuilder::SetResolution(int32_t width, int32_t height)
{
	SetResolution(cmn::Resolution{width, height});
}

void MediaConfigBuilder::SetResolution(const cmn::Resolution &resolution)
{
	if (_resolution == resolution)
	{
		return;
	}
	_resolution = resolution;
	_dirty = true;
}

bool MediaConfigBuilder::IsValidResolution() const
{
	return (_resolution.width > 0) && (_resolution.height > 0);
}

int32_t MediaConfigBuilder::GetSampleRate() const
{
	return ov::ToUnderlyingType(_sample.GetRate());
}

void MediaConfigBuilder::SetSampleRate(int32_t sample_rate)
{
	if (GetSampleRate() == sample_rate)
	{
		return;
	}
	_sample.SetRate(static_cast<cmn::AudioSample::Rate>(sample_rate));
	_dirty = true;
}

void MediaConfigBuilder::SetSampleFormat(cmn::AudioSample::Format format)
{
	if (_sample.GetFormat() == format)
	{
		return;
	}
	_sample.SetFormat(format);
	_dirty = true;
}

const cmn::AudioSample &MediaConfigBuilder::GetSample() const
{
	return _sample;
}

const cmn::AudioChannel &MediaConfigBuilder::GetChannel() const
{
	return _channel;
}

void MediaConfigBuilder::SetChannelLayout(cmn::AudioChannel::Layout layout)
{
	if (_channel.GetLayout() == layout)
	{
		return;
	}
	_channel.SetLayout(layout);
	_dirty = true;
}

bool MediaConfigBuilder::IsValidChannel() const
{
	return _channel.IsValid();
}

void MediaConfigBuilder::SetAudioSamplesPerFrame(int nbsamples)
{
	if (_audio_samples_per_frame == nbsamples)
	{
		return;
	}
	_audio_samples_per_frame = nbsamples;
	_dirty = true;
}

bool MediaConfigBuilder::IsComplete() const
{
	switch (_codec_id)
	{
		case cmn::MediaCodecId::H264:
		case cmn::MediaCodecId::H265:
		case cmn::MediaCodecId::Av1:
			return IsValidResolution() && IsValidTimeBase() && (_decoder_configuration_record != nullptr);

		case cmn::MediaCodecId::Vp8:
		case cmn::MediaCodecId::Vp9:
		case cmn::MediaCodecId::Flv:
		case cmn::MediaCodecId::Jpeg:
		case cmn::MediaCodecId::Png:
		case cmn::MediaCodecId::Webp:
			return IsValidResolution() && IsValidTimeBase();

		case cmn::MediaCodecId::Aac:
			return IsValidTimeBase() && IsValidChannel() && (GetSampleRate() > 0) && (_decoder_configuration_record != nullptr);

		case cmn::MediaCodecId::Opus:
			return IsValidTimeBase() && IsValidChannel() && (_sample.GetRate() == cmn::AudioSample::Rate::R48000);

		case cmn::MediaCodecId::Mp2:
		case cmn::MediaCodecId::Mp3:
			return IsValidTimeBase() && IsValidChannel();

		default:
			return false;
	}
}

bool MediaConfigBuilder::IsDirty() const
{
	return _dirty;
}

void MediaConfigBuilder::ClearDirty()
{
	_dirty = false;
}

std::shared_ptr<const MediaConfig> MediaConfigBuilder::Build(uint32_t version, uint32_t msid) const
{
	auto config = std::make_shared<MediaConfig>();

	config->_version = version;
	config->_msid = msid;

	config->_media_type = _media_type;
	config->_codec_id = _codec_id;
	config->_time_base = _time_base;

	config->_decoder_configuration_record = _decoder_configuration_record;

	config->_resolution = _resolution;

	config->_sample = _sample;
	config->_channel = _channel;
	config->_audio_samples_per_frame = _audio_samples_per_frame;

	return config;
}
