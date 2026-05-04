//==============================================================================
//
//  TranscoderStreamInternal
//
//  Created by Keukhan
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#include "transcoder_stream_internal.h"
#include <modules/ffmpeg/compat.h>

#include "transcoder_private.h"


TranscoderStreamInternal::TranscoderStreamInternal()
{
}

TranscoderStreamInternal::~TranscoderStreamInternal()
{
}

ov::String TranscoderStreamInternal::ProfileToSerialize(const uint32_t track_id, const cfg::vhost::app::oprf::VideoProfile &profile)
{
	if (profile.IsBypass() == true)
	{
		return ov::String::FormatString("I=T%d,O=bypass", track_id);
	}

	auto unique_profile_name = ov::String::FormatString("I=%d,O=%s:%d:%d:%d:%.02f:%d:%d:%d:%d",
									track_id,
									profile.GetCodec().CStr(),
									profile.GetBitrate(),
									profile.GetWidth(),
									profile.GetHeight(),
									profile.GetFramerate(),
									profile.GetKeyFrameInterval(),
									profile.GetBFrames(),
									profile.GetSkipFrames(),
									profile.GetLookahead());

	if (profile.GetProfile().IsEmpty() == false)
	{
		unique_profile_name += ov::String::FormatString(":%s", profile.GetProfile().CStr());
	}
	
	if (profile.GetPreset().IsEmpty() == false)
	{
		unique_profile_name += ov::String::FormatString(":%s", profile.GetPreset().CStr());
	}

	if (profile.GetModules().IsEmpty() == false)
	{
		unique_profile_name += ov::String::FormatString(":%s", profile.GetModules().CStr());
	}

	if(profile.GetExtraOptions().IsEmpty() == false)
	{
		unique_profile_name += ov::String::FormatString(":%zu", profile.GetExtraOptions().Hash());
	}

	return unique_profile_name;
}

ov::String TranscoderStreamInternal::ProfileToSerialize(const uint32_t track_id, const cfg::vhost::app::oprf::ImageProfile &profile)
{
	return ov::String::FormatString("I=%d,O=%s:%.02f:%d:%d:%d",
									track_id,
									profile.GetCodec().CStr(),
									profile.GetFramerate(),
									profile.GetSkipFrames(),
									profile.GetWidth(),
									profile.GetHeight());
}

ov::String TranscoderStreamInternal::ProfileToSerialize(const uint32_t track_id, const cfg::vhost::app::oprf::AudioProfile &profile)
{
	if (profile.IsBypass() == true)
	{
		return ov::String::FormatString("I=%d,O=bypass", track_id);
	}

	return ov::String::FormatString("I=%d,O=%s:%d:%d:%d",
									track_id,
									profile.GetCodec().CStr(),
									profile.GetBitrate(),
									profile.GetSamplerate(),
									profile.GetChannel());
}

ov::String TranscoderStreamInternal::ProfileToSerialize(const uint32_t track_id, const cfg::vhost::app::oprf::SpeechToTextProfile &profile)
{
	return ov::String::FormatString("I=%d,O=%s:%s:%d:%d:%s:%s",
									track_id,
									profile.GetEngine().CStr(),
									profile.GetModel().CStr(),
									profile.GetInputTrackId(),
									profile.GetOutputTrackId(),
									profile.GetSourceLanguage().CStr(),
									profile.ShouldTranslate() ? "true" : "false");
}

ov::String TranscoderStreamInternal::ProfileToSerialize(const uint32_t track_id)
{
	return ov::String::FormatString("I=%d,O=bypass", track_id);
}

cmn::Timebase TranscoderStreamInternal::GetDefaultTimebaseByCodecId(cmn::MediaCodecId codec_id)
{
	cmn::Timebase timebase(1, 1000);

	switch (codec_id)
	{
		case cmn::MediaCodecId::H264:
		case cmn::MediaCodecId::H265:
		case cmn::MediaCodecId::Vp8:
		case cmn::MediaCodecId::Vp9:
		case cmn::MediaCodecId::Flv:
		case cmn::MediaCodecId::Jpeg:
		case cmn::MediaCodecId::Png:
			timebase.SetNum(1);
			timebase.SetDen(90000);
			break;
		case cmn::MediaCodecId::Aac:
		case cmn::MediaCodecId::Mp2:
		case cmn::MediaCodecId::Mp3:
		case cmn::MediaCodecId::Opus:
			timebase.SetNum(1);
			timebase.SetDen(48000);
			break;
		default:
			break;
	}

	return timebase;
}

MediaTrackId TranscoderStreamInternal::NewTrackId()
{
	return _last_track_index++;
}

std::shared_ptr<MediaTrack> TranscoderStreamInternal::CreateOutputTrack(
	const std::shared_ptr<MediaTrack> &input_track, 
	const cfg::vhost::app::oprf::VideoProfile &profile
	)
{
	auto output_track = std::make_shared<MediaTrack>();
	if (output_track == nullptr)
	{
		return nullptr;
	}

	output_track->SetBypassByConfig(profile.IsBypass());
	output_track->SetMediaType(cmn::MediaType::Video);
	output_track->SetId(NewTrackId());
	output_track->SetPublicName(input_track->GetPublicName());
	output_track->SetVariantName(profile.GetName());
	output_track->SetLanguage(input_track->GetLanguage());
	output_track->SetCharacteristics(input_track->GetCharacteristics());
	output_track->SetOriginBitstream(input_track->GetOriginBitstream());

	bool is_bypass = IsMatchesBypassCondition(input_track, profile);
	if (is_bypass == true)
	{
		output_track->SetBypass(true);

		output_track->SetCodecId(input_track->GetCodecId());
		output_track->SetCodecModules(input_track->GetCodecModules());
		output_track->SetCodecModuleId(input_track->GetCodecModuleId());
		output_track->SetResolution(input_track->GetResolution());
		output_track->SetMaxResolution(input_track->GetMaxResolution());
		output_track->SetMaxFrameRate(input_track->GetMaxFrameRate());
		output_track->SetTimeBase(input_track->GetTimeBase());
		output_track->SetDecoderConfigurationRecord(input_track->GetDecoderConfigurationRecord());
	}
	else
	{
		output_track->SetBypass(false);

		auto codec_id = cmn::GetCodecIdByName(profile.GetCodec());
		if (cmn::IsVideoCodec(codec_id) == false)
		{
			logtw("%s codec is not supported video codec", profile.GetCodec().CStr());
			return nullptr;
		}

		output_track->SetCodecId(codec_id);
		output_track->SetTimeBase(GetDefaultTimebaseByCodecId(codec_id));

		cmn::Resolution resolution;
		resolution.width  = profile.GetWidth();
		resolution.height = profile.GetHeight();
		output_track->SetResolutionByConfig(resolution);
		output_track->SetResolution(resolution);

		output_track->SetPreset(profile.GetPreset());
		output_track->SetThreadCount(profile.GetThreadCount());
		output_track->SetBFrames(profile.GetBFrames());
		output_track->SetProfile(profile.GetProfile());
		output_track->SetBitrateByConfig(profile.GetBitrate());
		output_track->SetFrameRateByConfig(profile.GetFramerate());
		output_track->SetKeyFrameIntervalByConfig(profile.GetKeyFrameInterval());
		output_track->SetKeyFrameIntervalTypeByConfig(cmn::GetKeyFrameIntervalTypeByName(profile.GetKeyFrameIntervalType()));
		output_track->SetSkipFramesByConfig(profile.GetSkipFrames());
		output_track->SetLookaheadByConfig(profile.GetLookahead());
		output_track->SetExtraEncoderOptionsByConfig(profile.GetExtraOptions());
		output_track->SetCodecModules(profile.GetModules());

		// Used when the encoding profile codec name includes the module name. This specification will soon be deprecated.
		// ex) <Codec>h264_nvenc</Codec>
		// ex) <Codec>h264_openh264</Codec>
		auto module_id = cmn::GetCodecModuleIdByName(profile.GetCodec());
		if (module_id != cmn::MediaCodecModuleId::None)
		{
			if (output_track->GetCodecModules().IsEmpty() == false)
			{
				output_track->SetCodecModules(ov::String::FormatString("%s,%s", cmn::GetCodecModuleIdString(module_id), output_track->GetCodecModules().CStr()));
			}
			else
			{
				output_track->SetCodecModules(cmn::GetCodecModuleIdString(module_id));
			}
		}

		ApplySkipFrames(output_track, input_track);
	}

	return output_track;
}

std::shared_ptr<MediaTrack> TranscoderStreamInternal::CreateOutputTrack(const std::shared_ptr<MediaTrack> &input_track, const cfg::vhost::app::oprf::AudioProfile &profile)
{
	auto output_track = std::make_shared<MediaTrack>();
	if (output_track == nullptr)
	{
		return nullptr;
	}

	bool is_parsed;
	profile.IsBypass(&is_parsed);
	if (is_parsed == true)
	{
		output_track->SetBypassByConfig(profile.IsBypass());
	}

	output_track->SetMediaType(cmn::MediaType::Audio);
	output_track->SetId(NewTrackId());

	ov::String public_name = ov::String::FormatString("%s_%d", input_track->GetPublicName().CStr(), output_track->GetId());
	output_track->SetPublicName(public_name);
	output_track->SetVariantName(profile.GetName());
	output_track->SetLanguage(input_track->GetLanguage());
	output_track->SetCharacteristics(input_track->GetCharacteristics());
	output_track->SetOriginBitstream(input_track->GetOriginBitstream());

	bool is_bypass = IsMatchesBypassCondition(input_track, profile);
	if (is_bypass == true)
	{
		output_track->SetBypass(true);

		output_track->SetCodecId(input_track->GetCodecId());
		output_track->SetBitrateByConfig(input_track->GetBitrateByConfig());
		output_track->SetCodecModules(input_track->GetCodecModules());
		output_track->SetCodecModuleId(input_track->GetCodecModuleId());
		output_track->SetChannel(input_track->GetChannel());
		output_track->SetSampleFormat(input_track->GetSample().GetFormat());
		output_track->SetTimeBase(input_track->GetTimeBase());
		output_track->SetSampleRate(input_track->GetSampleRate());
		output_track->SetDecoderConfigurationRecord(input_track->GetDecoderConfigurationRecord());
	}
	else
	{
		output_track->SetBypass(false);

		auto codec_id = cmn::GetCodecIdByName(profile.GetCodec());
		if (cmn::IsAudioCodec(codec_id) == false)
		{
			logtw("%s codec is not supported audio codec", profile.GetCodec().CStr());
			return nullptr;
		}

		output_track->SetCodecId(codec_id);
		output_track->SetBitrateByConfig(profile.GetBitrate());
		output_track->SetCodecModules(profile.GetModules());
		output_track->SetChannelLayout(profile.GetChannel() == 1 ? cmn::AudioChannel::Layout::LayoutMono : cmn::AudioChannel::Layout::LayoutStereo);
		// Sample Format will be decided by the encoder
		output_track->SetSampleFormat(cmn::AudioSample::Format::None);
		output_track->SetSampleRate(profile.GetSamplerate());
		if (output_track->GetCodecId() == cmn::MediaCodecId::Opus)
		{
			if (output_track->GetSampleRate() != 48000)
			{
				output_track->SetSampleRate(48000);
				logtw("OPUS codec only supports 48000Hz samplerate. change the samplerate to 48000Hz");
			}
		}

		if (output_track->GetSampleRate() > 0)
		{
			output_track->SetTimeBase(1, output_track->GetSampleRate());
		}
	}

	return output_track;
}

std::shared_ptr<MediaTrack> TranscoderStreamInternal::CreateOutputTrack(const std::shared_ptr<MediaTrack> &input_track, const cfg::vhost::app::oprf::ImageProfile &profile)
{
	auto output_track = std::make_shared<MediaTrack>();
	if (output_track == nullptr)
	{
		return nullptr;
	}

	output_track->SetPublicName(input_track->GetPublicName());
	output_track->SetVariantName(profile.GetName());
	output_track->SetLanguage(input_track->GetLanguage());
	output_track->SetCharacteristics(input_track->GetCharacteristics());
	output_track->SetOriginBitstream(input_track->GetOriginBitstream());

	output_track->SetMediaType(cmn::MediaType::Video);
	output_track->SetId(NewTrackId());
	output_track->SetBypass(false);

	auto codec_id = cmn::GetCodecIdByName(profile.GetCodec());
	if (cmn::IsImageCodec(codec_id) == false)
	{
		logtw("%s codec is not supported image codec", profile.GetCodec().CStr());
		return nullptr;
	}
	output_track->SetCodecId(codec_id);
	output_track->SetTimeBase(GetDefaultTimebaseByCodecId(codec_id));
	output_track->SetCodecModules(profile.GetModules());

	cmn::Resolution resolution;
	resolution.width  = profile.GetWidth();
	resolution.height = profile.GetHeight();
	output_track->SetResolutionByConfig(resolution);
	output_track->SetResolution(resolution);
	output_track->SetFrameRateByConfig(profile.GetFramerate());
	output_track->SetSkipFramesByConfig(profile.GetSkipFrames());

	// Github Issue : #1417
	// Set any value for quick validation of the output track.
	// If the validation of OutputTrack is delayed, the Stream Prepare event occurs late in Publisher.
	// The bitrate of an image doesn’t mean much anyway.
	output_track->SetBitrateByConfig(0);
	output_track->SetBitrateByMeasured(1000000);

	ApplySkipFrames(output_track, input_track);

	return output_track;
}

std::shared_ptr<MediaTrack> TranscoderStreamInternal::CreateOutputTrackDataType(const std::shared_ptr<MediaTrack> &input_track)
{
	auto output_track = std::make_shared<MediaTrack>();
	if (output_track == nullptr)
	{
		return nullptr;
	}

	output_track->SetMediaType(input_track->GetMediaType());
	output_track->SetId(NewTrackId());
	output_track->SetVariantName(input_track->GetVariantName());
	output_track->SetPublicName(input_track->GetPublicName());
	output_track->SetLanguage(input_track->GetLanguage());
	output_track->SetCharacteristics(input_track->GetCharacteristics());
	output_track->SetDefault(input_track->IsDefault());
	output_track->SetAutoSelect(input_track->IsAutoSelect());
	output_track->SetForced(input_track->IsForced());
	output_track->SetBypass(true);
	output_track->SetCodecId(input_track->GetCodecId());
	output_track->SetCodecModules("");
	output_track->SetCodecModuleId(input_track->GetCodecModuleId());
	output_track->SetOriginBitstream(input_track->GetOriginBitstream());
	output_track->SetMaxResolution(input_track->GetMaxResolution());
	output_track->SetMaxFrameRate(input_track->GetMaxFrameRate());
	output_track->SetResolution(input_track->GetResolution());
	output_track->SetFrameRateByMeasured(input_track->GetFrameRate());
	output_track->SetTimeBase(input_track->GetTimeBase());

	return output_track;
}

std::shared_ptr<MediaTrack> TranscoderStreamInternal::CreateOutputTrack(const std::shared_ptr<MediaTrack> &input_track, const cfg::vhost::app::oprf::SpeechToTextProfile &profile)
{
	auto output_track = std::make_shared<MediaTrack>();
	if (output_track == nullptr)
	{
		return nullptr;
	}

	output_track->SetMediaType(cmn::MediaType::Subtitle);
	output_track->SetId(NewTrackId());
	output_track->SetVariantName(profile.GetOutputTrackLabel());
	output_track->SetPublicName(profile.GetOutputTrackLabel());
	output_track->SetLanguage(profile.GetSourceLanguage());
	
	output_track->SetCodecId(cmn::MediaCodecId::Whisper);
	output_track->SetCodecModules(profile.GetModules());
	
	output_track->SetOriginBitstream(input_track->GetOriginBitstream());
	output_track->SetTimeBase(input_track->GetTimeBase());

	// Set Speech-To-Text specific properties
	output_track->SetEngine(profile.GetEngine());
	output_track->SetModel(profile.GetModel());
	output_track->SetSourceLanguage(profile.GetSourceLanguage());
	output_track->SetTranslation(profile.ShouldTranslate());
	output_track->SetOutputLabel(profile.GetOutputTrackLabel());
	output_track->SetStepMs(profile.GetStepMs());
	output_track->SetLengthMs(profile.GetLengthMs());
	output_track->SetKeepMs(profile.GetKeepMs());
	output_track->SetSttEnabled(profile.IsSttEnabled());

	output_track->SetExtraInfo(ov::String::FormatString(
		"Engine(%s) Model(%s) AudioInput(%d) Language(%s)",
		profile.GetEngine().CStr(),
		profile.GetModel().CStr(),
		input_track->GetId(),
		profile.GetSourceLanguage().CStr()));

	// STT track is non-essential: encoder failure is not fatal and the stream continues without it.
	output_track->SetEssentialTrack(false);

	if (profile.GetEngine().LowerCaseString() == "whisper")
	{
		// Whisper only supports 16kHz mono audio input and float sample format.
		output_track->SetSampleRate(16000);
		output_track->SetTimeBase(1, 16000);
		output_track->SetChannelLayout(cmn::AudioChannel::Layout::LayoutMono);
		output_track->SetSampleFormat(cmn::AudioSample::Format::Flt);
	}
	else
	{
		logte("Unsupported Speech-To-Text engine: %s", profile.GetEngine().CStr());
		return nullptr;
	}

	return output_track;
}

bool TranscoderStreamInternal::IsMatchesBypassCondition(const std::shared_ptr<MediaTrack> &input_track, const cfg::vhost::app::oprf::VideoProfile &profile)
{
	bool is_parsed = false;
	uint32_t if_count;
	ov::String condition;

	if (profile.IsBypass() == true)
	{
		return true;
	}

	auto if_match = profile.GetBypassIfMatch(&is_parsed);
	if (is_parsed == false)
	{
		return false;
	}
	if_count = 0;

	condition = if_match.GetCodec(&is_parsed).UpperCaseString();
	if (is_parsed == true)
	{
		if ((condition == "EQ") && (cmn::GetCodecIdByName(profile.GetCodec()) != input_track->GetCodecId()))
		{
			return false;
		}

		if_count++;
	}

	condition = if_match.GetWidth(&is_parsed).UpperCaseString();
	auto input_resolution = input_track->GetResolution();
	if (is_parsed == true)
	{
		if ((condition == "EQ") && (input_resolution.width != profile.GetWidth()))
		{
			return false;
		}
		else if ((condition == "LTE") && input_resolution.width > profile.GetWidth())
		{
			return false;
		}
		else if (condition == "GTE" && (input_resolution.width < profile.GetWidth()))
		{
			return false;
		}

		if_count++;
	}

	condition = if_match.GetHeight(&is_parsed).UpperCaseString();
	if (is_parsed == true)
	{
		if ((condition == "EQ") && (input_resolution.height != profile.GetHeight()))
		{
			return false;
		}
		else if ((condition == "LTE") && input_resolution.height > profile.GetHeight())
		{
			return false;
		}
		else if (condition == "GTE" && (input_resolution.height < profile.GetHeight()))
		{
			return false;
		}

		if_count++;
	}

	condition = if_match.GetFramerate(&is_parsed).UpperCaseString();
	if (is_parsed == true)
	{
		if ((condition == "EQ") && (input_track->GetFrameRate() != profile.GetFramerate()))
		{
			return false;
		}
		else if ((condition == "LTE") && input_track->GetFrameRate() > profile.GetFramerate())
		{
			return false;
		}
		else if (condition == "GTE" && (input_track->GetFrameRate() < profile.GetFramerate()))
		{
			return false;
		}

		if_count++;
	}

	condition = if_match.GetSAR(&is_parsed).UpperCaseString();
	if (is_parsed == true)
	{
		if (condition == "EQ") 
		{
			float track_sar = (float)input_resolution.width / (float)input_resolution.height;
			float profile_sar = (float)profile.GetWidth() / (float)profile.GetHeight();

			if (track_sar != profile_sar)
			{
				return false;
			}
		}

		if_count++;
	}	

	return (if_count > 0) ? true : false;
}

bool TranscoderStreamInternal::IsMatchesBypassCondition(const std::shared_ptr<MediaTrack> &input_track, const cfg::vhost::app::oprf::AudioProfile &profile)
{
	bool is_parsed = false;
	uint32_t if_count;

	ov::String condition;

	if (profile.IsBypass() == true)
	{
		return true;
	}

	auto if_match = profile.GetBypassIfMatch(&is_parsed);
	if (is_parsed == false)
	{
		return false;
	}
	if_count = 0;

	condition = if_match.GetCodec(&is_parsed).UpperCaseString();
	if (is_parsed == true)
	{
		if ((condition == "EQ") && (cmn::GetCodecIdByName(profile.GetCodec()) != input_track->GetCodecId()))
		{
			if (cmn::GetCodecIdByName(profile.GetCodec()) != input_track->GetCodecId())
			{
				return false;
			}
		}

		if_count++;
	}

	condition = if_match.GetSamplerate(&is_parsed).UpperCaseString();
	if (is_parsed == true)
	{
		if ((condition == "EQ") && (input_track->GetSampleRate() != profile.GetSamplerate()))
		{
			return false;
		}
		else if ((condition == "LTE") && (input_track->GetSampleRate() > profile.GetSamplerate()))
		{
			return false;
		}
		else if ((condition == "GTE") && (input_track->GetSampleRate() < profile.GetSamplerate()))
		{
			return false;
		}

		if_count++;
	}

	condition = if_match.GetChannel(&is_parsed).UpperCaseString();
	if (is_parsed == true)
	{
		if ((condition == "EQ") && ((const int)input_track->GetChannel().GetCounts() != profile.GetChannel()))
		{
			return false;
		}
		else if ((condition == "LTE") && ((const int)input_track->GetChannel().GetCounts() > profile.GetChannel()))
		{
			return false;
		}
		else if ((condition == "GTE") && ((const int)input_track->GetChannel().GetCounts() < profile.GetChannel()))
		{
			return false;
		}

		if_count++;
	}

	return (if_count > 0) ? true : false;
}


double TranscoderStreamInternal::MeasurementToRecommendFramerate(double framerate)
{
	double start_framerate = ::floor(framerate);
	if(start_framerate < 5.0f)
		start_framerate = 5.0f;
	
	// It is greater than the measured frame rate and is set to a value that is divisible by an integer in the timebase(90Hz).
	// In chunk-based protocols, the chunk length is made stable.
	double recommend_framerate = start_framerate;

	while (true)
	{
		if ((int)(90000 % (int64_t)recommend_framerate) == 0)
		{
			break;
		}

		recommend_framerate++;
	}

	return ::floor(recommend_framerate);
}

// Update the output track information based on the input track and the decoded frame from the decoded frame (bypass)
void TranscoderStreamInternal::UpdateOutputTrackPassthrough(const std::shared_ptr<MediaTrack> &output_track, const std::shared_ptr<MediaTrack> &input_track)
{
	output_track->SetCodecId(input_track->GetCodecId());
	output_track->SetCodecModules(input_track->GetCodecModules());
	output_track->SetCodecModuleId(input_track->GetCodecModuleId());

	output_track->SetFrameRateByConfig(input_track->GetFrameRateByConfig());
	output_track->SetFrameRateByMeasured(input_track->GetFrameRateByMeasured());
	output_track->SetBitrateByMeasured(input_track->GetBitrateByMeasured());
	output_track->SetBitrateByConfig(input_track->GetBitrateByConfig());
	output_track->SetTimeBase(input_track->GetTimeBase());
	output_track->SetDecoderConfigurationRecord(input_track->GetDecoderConfigurationRecord());

	if (output_track->GetMediaType() == cmn::MediaType::Video)
	{
		output_track->SetResolution(input_track->GetResolution());
		output_track->SetColorspace(input_track->GetColorspace());
	}
	else if (output_track->GetMediaType() == cmn::MediaType::Audio)
	{
		output_track->SetSampleRate(input_track->GetSampleRate());
		output_track->SetSampleFormat(input_track->GetSample().GetFormat());
		output_track->SetChannel(input_track->GetChannel());
	}
}

// Update the output track information based on the decoded frame from the decoder before creating the encoder. (encoding)
// If the user has not specified it, the output specification is automatically determined.
// Once determined for the first time, the specification is maintained until the stream ends.
void TranscoderStreamInternal::UpdateOutputTrackByDecodedFrame(const std::shared_ptr<MediaTrack> &output_track, const std::shared_ptr<MediaTrack> &input_track, std::shared_ptr<MediaFrame> buffer)
{
	if (output_track->GetMediaType() == cmn::MediaType::Video)
	{
		UpdateOutputVideoTrackByDecodedFrame(output_track, input_track, buffer);
	}
	else if (output_track->GetMediaType() == cmn::MediaType::Audio)
	{
		UpdateOutputAudioTrackByDecodedFrame(output_track, input_track, buffer);
	}
	else
	{
		// No update is needed for other media types.
	}
}

void TranscoderStreamInternal::UpdateOutputVideoTrackByDecodedFrame(const std::shared_ptr<MediaTrack> &output_track, const std::shared_ptr<MediaTrack> &input_track, std::shared_ptr<MediaFrame> buffer)
{
	const int32_t src_width	 = buffer->GetWidth();
	const int32_t src_height = buffer->GetHeight();

	if (src_width <= 0 || src_height <= 0)
	{
		logte("Invalid decoded frame size: %dx%d", src_width, src_height);
		return;
	}

	logtt("Input Resolution: %dx%d(conf) %dx%d(max), Output Resolution: %dx%d(conf) %dx%d(max)",
		  input_track->GetResolution().width, input_track->GetResolution().height,
		  input_track->GetMaxResolution().width, input_track->GetMaxResolution().height,
		  output_track->GetResolution().width, output_track->GetResolution().height,
		  output_track->GetMaxResolution().width, output_track->GetMaxResolution().height);

	// Update Output resolution
	auto output_resolution = output_track->GetResolution();
	if (output_resolution.width > 0 && output_resolution.height > 0 &&
		output_resolution.width % 4 == 0 && output_resolution.height % 4 == 0)
	{
		// The output resolution is already set. It should be maintained until the stream end.
		logtt("Resolution is not changed. %dx%d", output_resolution.width, output_resolution.height);
	}
	else
	{
		// If the output resolution is not set, it is set based on the original video resolution.
		auto new_output_resolution = output_resolution;

		if (output_resolution.width == 0 && output_resolution.height == 0)
		{
			// Used the original video resolution
			new_output_resolution.width	 = buffer->GetWidth();
			new_output_resolution.height = buffer->GetHeight();
		}
		else if (output_resolution.width == 0 && output_resolution.height != 0)
		{
			// Width is automatically calculated as the original video ratio
			const float aspect_ratio	= static_cast<float>(src_width) / static_cast<float>(src_height);
			const double scaled_width	= static_cast<double>(output_resolution.height) * static_cast<double>(aspect_ratio);
			int32_t width				= static_cast<int32_t>(std::lround(scaled_width));

			width						= (width % 2 == 0) ? width : width + 1;

			new_output_resolution.width = width;
		}
		else if (output_resolution.width != 0 && output_resolution.height == 0)
		{
			// Height is automatically calculated as the original video ratio
			const float aspect_ratio	 = static_cast<float>(src_width) / static_cast<float>(src_height);
			const double scaled_height	 = static_cast<double>(output_resolution.width) / static_cast<double>(aspect_ratio);
			int32_t height				 = static_cast<int32_t>(std::lround(scaled_height));
			height						 = (height % 2 == 0) ? height : height + 1;

			new_output_resolution.height = height;
		}

		new_output_resolution = GetAlignmentResolution(new_output_resolution);

		output_track->SetResolution(new_output_resolution);

		logtd("Id(%d), Resolution is changed %dx%d -> %dx%d",
			  output_track->GetId(), output_resolution.width, output_resolution.height,
			  new_output_resolution.width, new_output_resolution.height);
	}

	logtt("Input Timebase: %d/%d, Output Timebase: %d/%d",
		  input_track->GetTimeBase().GetNum(), input_track->GetTimeBase().GetDen(),
		  output_track->GetTimeBase().GetNum(), output_track->GetTimeBase().GetDen());

	// Update timebase of the output track
	auto output_timebase = output_track->GetTimeBase();
	if (output_timebase.GetNum() != 0 && output_timebase.GetDen() != 0)
	{
		// The timebase is already set. It should be maintained until the stream end.
		logtt("Timebase is not changed. %d/%d", output_timebase.GetNum(), output_timebase.GetDen());
	}
	else
	{
		// If the timebase is not set, it is set default timebase based on the codec.
		auto new_output_timebase = GetDefaultTimebaseByCodecId(output_track->GetCodecId());

		output_track->SetTimeBase(new_output_timebase);

		logtd("Id(%d), Timebase is changed. %d/%d -> %d/%d",
			  output_track->GetId(), output_timebase.GetNum(), output_timebase.GetDen(),
			  new_output_timebase.GetNum(), new_output_timebase.GetDen());
	}

	// Update framerate of the output track
	logtt("Input Framerate: %.02f(conf) %.02f(measure) %.02f(max), Output Framerate: %.02f(conf) %.02f(measure) %.02f(max)",
		  input_track->GetFrameRateByConfig(), input_track->GetFrameRateByMeasured(), input_track->GetMaxFrameRate(),
		  output_track->GetFrameRateByConfig(), output_track->GetFrameRateByMeasured(), output_track->GetMaxFrameRate());

	auto output_framerate = output_track->GetFrameRateByConfig();
	if (output_framerate > 0.0f)
	{
		// The framerate is already set. It should be maintained until the stream end.
		logtt("Framerate is not changed.  %.2f", output_framerate);
	}
	else
	{
		// If the framerate is not set, it is set based on the input video framerate.
		double new_output_framerate;
		if (input_track->GetFrameRateByConfig() > 0.0f)
		{
			new_output_framerate = input_track->GetFrameRateByConfig();
		}
		else if (buffer->GetDuration() > 0)
		{
			new_output_framerate = 1.0f / (input_track->GetTimeBase().GetExpr() * buffer->GetDuration());
		}
		else if (input_track->GetFrameRateByMeasured() > 0.0f)
		{
			new_output_framerate = input_track->GetFrameRateByMeasured();
		}
		else
		{
			new_output_framerate = 1.0f;
		}

		output_track->SetFrameRateByConfig(new_output_framerate);

		logtd("Id(%d), Framerate is changed. %.2f -> %.2f",
			  output_track->GetId(), output_framerate, new_output_framerate);
	}

	// Update Output bitrate
	logtt("Input Bitrate: %d(conf) %d(measure), Output Bitrate: %d(conf) %d(measure)",
		  input_track->GetBitrateByConfig(), input_track->GetBitrateByMeasured(), output_track->GetBitrateByConfig(), output_track->GetBitrateByMeasured());

	auto output_bitrate = output_track->GetBitrateByConfig();
	if (output_bitrate > 0)
	{
		// The bitrate is already set. It should be maintained until the stream end.
		logtt("Bitrate is not changed. %d", output_bitrate);
	}
	else
	{
		// If the bitrate is not set, it is set based on the input video bitrate.
		auto new_output_bitrate = input_track->GetBitrateByConfig();
		if(new_output_bitrate <= 0)
		{
			// If the input bitrate is not set, it is set based on the measured bitrate.
			new_output_bitrate = input_track->GetBitrateByMeasured();
		}

		output_track->SetBitrateByConfig(new_output_bitrate);

		logtd("Id(%d), Bitrate is changed. %d -> %d", output_track->GetId(), output_bitrate, new_output_bitrate);
	}
}

void TranscoderStreamInternal::UpdateOutputAudioTrackByDecodedFrame(const std::shared_ptr<MediaTrack> &output_track, const std::shared_ptr<MediaTrack> &input_track, std::shared_ptr<MediaFrame> buffer)
{
 	if (buffer->GetSampleRate() <= 0)
 	{
 		logte("Invalid decoded frame sample rate: %d", buffer->GetSampleRate());
 		return;
 	}
	
 	if (buffer->GetChannels().GetLayout() == cmn::AudioChannel::Layout::LayoutUnknown)
 	{
 		logte("Invalid decoded frame channel layout: %d", static_cast<int>(buffer->GetChannels().GetLayout()));
 		return;
 	}

	// Update sample rate of the output track
	logtt("Input SampleRate: %d, Output SampleRate: %d",
		  input_track->GetSampleRate(), output_track->GetSampleRate());

	auto output_samplerate = output_track->GetSampleRate();
	if (output_samplerate > 0)
	{
		logtt("SampleRate is not changed. %d", output_samplerate);
	}
	else
	{
		auto new_output_samplerate = buffer->GetSampleRate();

		logtd("Id(%d), SampleRate is changed. %d -> %d",
			  output_track->GetId(), output_samplerate, new_output_samplerate);

		output_track->SetSampleRate(new_output_samplerate);
	}

	// Update timebase of the output track
	logtt("Input Timebase: %d/%d, Output Timebase: %d/%d",
		  input_track->GetTimeBase().GetNum(), input_track->GetTimeBase().GetDen(), output_track->GetTimeBase().GetNum(), output_track->GetTimeBase().GetDen());

	auto output_timebase = output_track->GetTimeBase();
	if (output_timebase.GetNum() != 0 && output_timebase.GetDen() != 0)
	{
		logtt("Timebase is not changed. %d/%d", output_timebase.GetNum(), output_timebase.GetDen());
	}
	else
	{
		cmn::Timebase new_output_timebase;

		new_output_timebase.Set(1, buffer->GetSampleRate());

		output_track->SetTimeBase(new_output_timebase);

		logtd("Id(%d), Timebase is changed. %d/%d -> %d/%d",
			  output_track->GetId(), output_timebase.GetNum(), output_timebase.GetDen(), new_output_timebase.GetNum(), new_output_timebase.GetDen());
	}

	// Update channel layout of the output track
	logtt("Input Channel Layout: %s, Output Channel Layout: %s",
		  cmn::AudioChannel::GetLayoutName(input_track->GetChannel().GetLayout()), cmn::AudioChannel::GetLayoutName(output_track->GetChannel().GetLayout()));

	if (output_track->GetChannel().GetLayout() != cmn::AudioChannel::Layout::LayoutUnknown)
	{
		logtt("Id(%d), Channel layout is not changed. %s",
			  output_track->GetId(), cmn::AudioChannel::GetLayoutName(output_track->GetChannel().GetLayout()));
	}
	else
	{
		logtd("Id(%d), Channel layout is changed. %s -> %s",
			  output_track->GetId(),
			  cmn::AudioChannel::GetLayoutName(output_track->GetChannel().GetLayout()),
			  cmn::AudioChannel::GetLayoutName(buffer->GetChannels().GetLayout()));

		output_track->SetChannel(buffer->GetChannels());
	}

	// Update Output bitrate
	logtt("Input Bitrate: %d(conf) %d(measure), Output Bitrate: %d(conf) %d(measure)",
		  input_track->GetBitrateByConfig(), input_track->GetBitrateByMeasured(), output_track->GetBitrateByConfig(), output_track->GetBitrateByMeasured());

	auto output_bitrate = output_track->GetBitrateByConfig();
	if (output_bitrate > 0)
	{
		// The bitrate is already set. It should be maintained until the stream end.
		logtt("Bitrate is not changed. %d", output_bitrate);
	}
	else
	{
		// If the bitrate is not set, it is set based on the input audio bitrate.
		auto new_output_bitrate = input_track->GetBitrateByConfig();
		if(new_output_bitrate <= 0)
		{
			// If the input bitrate is not set, it is set based on the measured bitrate.
			new_output_bitrate = input_track->GetBitrateByMeasured();
		}

		output_track->SetBitrateByConfig(new_output_bitrate);

		logtd("Id(%d), Bitrate is changed. %d -> %d",
			  output_track->GetId(), output_bitrate, new_output_bitrate);
	}
}

bool TranscoderStreamInternal::StoreTracks(std::shared_ptr<info::Stream> stream)
{
	_store_tracks.clear();
	
	for (auto &[track_id, track] : stream->GetTracks())
	{
		auto clone = track->Clone();
		_store_tracks[track_id] = clone;
	}	

	return true;
}

std::map<int32_t, std::shared_ptr<MediaTrack>>& TranscoderStreamInternal::GetStoredTracks()
{
	return _store_tracks;
}

bool TranscoderStreamInternal::CompareTracksForSeamlessTransition(std::map<int32_t, std::shared_ptr<MediaTrack>> prev_tracks, std::map<int32_t, std::shared_ptr<MediaTrack>> new_tracks)
{
	// #1 Check the number of tracks
	if (prev_tracks.size() != new_tracks.size())
	{
		return false;
	}

	// #2 Check type of tracks
	for (auto &[track_id, prev_track] : prev_tracks)
	{
		if (new_tracks.find(track_id) == new_tracks.end())
		{
			return false;
		}

		if(prev_track->GetMediaType() != new_tracks[track_id]->GetMediaType())
		{
			return false;
		}
	}

	return true;
}

bool TranscoderStreamInternal::IsKeyframeOnlyDecodable(const std::map<ov::String, std::shared_ptr<info::Stream>> &streams)
{
	uint32_t video, video_bypass, audio, audio_bypass, image, data;

	GetCountByEncodingType(streams, video, video_bypass, audio, audio_bypass, image, data);

	// logtt("Video:%u, Video(Bypass):%u, Audio:%u, Audio(Bypass):%u, Image:%u, Data:%u",
	// 	  video, video_bypass, audio, audio_bypass, image, data);

	if (video == 0 && image > 0)
	{
		return true;
	}

	return false;
}

void TranscoderStreamInternal::GetCountByEncodingType(
	const std::map<ov::String, std::shared_ptr<info::Stream>> &streams,
	uint32_t &video, uint32_t &video_bypass, uint32_t &audio, uint32_t &audio_bypass, uint32_t &image, uint32_t &data)
{
	video = 0;
	video_bypass = 0;
	audio = 0;
	audio_bypass = 0;
	image = 0;
	data = 0;

	for (auto &[stream_name, stream] : streams)
	{
		for (auto &[track_id, track] : stream->GetTracks())
		{
			switch (track->GetMediaType())
			{
				case cmn::MediaType::Video:
					if (cmn::IsImageCodec(track->GetCodecId()) == true)
					{
						image++;
					}
					else if (cmn::IsVideoCodec(track->GetCodecId()) == true)
					{
						if (track->IsBypass() == true)
						{
							video_bypass++;
						}
						else
						{
							video++;
						}
					}
					break;
				case cmn::MediaType::Audio:
					if (track->IsBypass() == true)
					{
						audio_bypass++;
					}
					else
					{
						audio++;
					}
					break;
				case cmn::MediaType::Data:
					data++;
					break;
				default:
					break;
			}
		}
	}
}

// To be compatible with all hardware. The encoding resolution must be a multiple
// In particular, Xilinx Media Accelerator must have a resolution specified in multiples of 4.
cmn::Resolution TranscoderStreamInternal::GetAlignmentResolution(const cmn::Resolution &resolution)
{
	auto aligned = resolution;

	if (aligned.width % 4 != 0)
	{
		int32_t new_width = (aligned.width / 4 + 1) * 4;

		logtw("The width of the output track is not a multiple of 4. change the width to %d -> %d",
			  aligned.width, new_width);

		aligned.width = new_width;
	}

	if (aligned.height % 4 != 0)
	{
		int32_t new_height = (aligned.height / 4 + 1) * 4;

		logtw("The height of the output track is not a multiple of 4. change the height to %d -> %d",
			  aligned.height, new_height);

		aligned.height = new_height;
	}

	return aligned;
}

void TranscoderStreamInternal::ApplySkipFrames(const std::shared_ptr<MediaTrack> &output_track, const std::shared_ptr<MediaTrack> &input_track)
{
	int32_t skip_frames = output_track->GetSkipFramesByConfig();
	if (skip_frames < 0)
	{
		return;
	}

	// When skipFrames is enabled, the user-set framerate is ignored, and the input framerate is adjusted by applying skipFrames.
	constexpr double precision	   = 100.0;
	auto adjusted_output_framerate = std::round(input_track->GetFrameRate() / (skip_frames + 1.0) * precision) / precision;

	output_track->SetFrameRateByConfig(adjusted_output_framerate);

	logtd("Adjust the output framerate %.02f -> %.02f according to the skip frames %d", input_track->GetFrameRate(), adjusted_output_framerate, skip_frames);
}
