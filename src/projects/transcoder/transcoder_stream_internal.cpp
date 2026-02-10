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

	bool is_parsed;
	profile.IsBypass(&is_parsed);
	if (is_parsed == true)
	{
		output_track->SetBypassByConfig(profile.IsBypass());
	}

	profile.GetBitrateString(&is_parsed);
	if (is_parsed == true)
	{
		output_track->SetBitrateByConfig(profile.GetBitrate());
	}

	profile.GetWidth(&is_parsed);
	if (is_parsed == true)
	{
		output_track->SetWidthByConfig(profile.GetWidth());
	}

	profile.GetHeight(&is_parsed);
	if (is_parsed == true)
	{
		output_track->SetHeightByConfig(profile.GetHeight());
	}

	profile.GetFramerate(&is_parsed);
	if (is_parsed == true)
	{
		output_track->SetFrameRateByConfig(profile.GetFramerate());
	}

	profile.GetKeyFrameInterval(&is_parsed);
	if (is_parsed == true)
	{
		output_track->SetKeyFrameIntervalByConfig(profile.GetKeyFrameInterval());
	}

	profile.GetKeyFrameIntervalType(&is_parsed);
	if (is_parsed == true)
	{
		output_track->SetKeyFrameIntervalTypeByConfig(cmn::GetKeyFrameIntervalTypeByName(profile.GetKeyFrameIntervalType()));
	}

	// If SkipFrames is enabled, it affects the output track's framerate and keyframe interval.
	profile.GetSkipFrames(&is_parsed);
	if (is_parsed == true)
	{
		auto skip_frames = profile.GetSkipFrames();

		// Set ouput framerate according to the skip frames.
		if (skip_frames >= 0)
		{
			output_track->SetSkipFramesByConfig(skip_frames);

			// Adjust the framerate according to the skip frames. 
			// When skipFrames is enabled, the user-set framerate is ignored, and the input framerate is adjusted by applying skipFrames.
			// Round adjusted_frame_rate to 2 decimal places
			auto adjusted_frame_rate = ::round(input_track->GetFrameRate() / (skip_frames + 1) * 100.0) / 100.0;

			logtt("Adjust the output framerate %.02f -> %.02f according to the skip frames %d",
				  input_track->GetFrameRate(),
				  adjusted_frame_rate,
				  skip_frames);
			output_track->SetFrameRateByConfig(adjusted_frame_rate);

#if 0 // This feature needs more consideration. It may confuse to users.
			// Set keyframe interval according to the skip frames.
			//  If the keyframe interval is not set, it will be automatically adjusted by the encoder.
			if (output_track->GetKeyFrameIntervalByConfig() > 0)
			{
				auto adjusted_key_frame_interval = ::round(output_track->GetKeyFrameIntervalByConfig() / (skip_frames + 1));
				logtt("Adjust the output key_frame_interval %.02f -> %.02f according to the skip frames %d",
					  output_track->GetKeyFrameIntervalByConfig(),
					  adjusted_key_frame_interval,
					  skip_frames);

				output_track->SetKeyFrameIntervalByConfig(adjusted_key_frame_interval);
			}
#endif
		}
	}

	profile.GetLookahead(&is_parsed);
	if (is_parsed == true)
	{
		output_track->SetLookaheadByConfig(profile.GetLookahead());
	}

	output_track->SetMediaType(cmn::MediaType::Video);
	output_track->SetId(NewTrackId());
	output_track->SetVariantName(profile.GetName());
	output_track->SetPublicName(input_track->GetPublicName());
	output_track->SetLanguage(input_track->GetLanguage());
	output_track->SetCharacteristics(input_track->GetCharacteristics());
	output_track->SetOriginBitstream(input_track->GetOriginBitstream());

	bool need_bypass = IsMatchesBypassCondition(input_track, profile);
	if (need_bypass == true)
	{
		output_track->SetBypass(true);

		output_track->SetCodecId(input_track->GetCodecId());
		output_track->SetCodecModules(input_track->GetCodecModules());
		output_track->SetCodecModuleId(input_track->GetCodecModuleId());
		output_track->SetMaxWidth(input_track->GetMaxWidth());
		output_track->SetMaxHeight(input_track->GetMaxHeight());
		output_track->SetWidth(input_track->GetWidth());
		output_track->SetHeight(input_track->GetHeight());
		output_track->SetTimeBase(input_track->GetTimeBase());
	}
	else
	{
		output_track->SetBypass(false);

		auto codec_id = cmn::GetCodecIdByName(profile.GetCodec());

		output_track->SetCodecId(codec_id);
		output_track->SetCodecModules(profile.GetModules());
		output_track->SetWidth(profile.GetWidth());
		output_track->SetHeight(profile.GetHeight());
		output_track->SetTimeBase(GetDefaultTimebaseByCodecId(codec_id));
		output_track->SetPreset(profile.GetPreset());
		output_track->SetThreadCount(profile.GetThreadCount());
		output_track->SetBFrames(profile.GetBFrames());
		output_track->SetProfile(profile.GetProfile());

		// Used when the encoding profile codec name includes the module name.
		// ex) <Codec>h264_nvenc</Codec>
		// ex) <Codec>h264_openh264</Codec>
		auto module_id = cmn::GetCodecModuleIdByName(profile.GetCodec());
		if(module_id != cmn::MediaCodecModuleId::None)
		{
			if(output_track->GetCodecModules().IsEmpty() == false)
			{
				output_track->SetCodecModules(ov::String::FormatString("%s,%s", cmn::GetCodecModuleIdString(module_id), output_track->GetCodecModules().CStr()));
			}
			else
			{
				output_track->SetCodecModules(cmn::GetCodecModuleIdString(module_id));
			}
		}
	}

	//  If the framerate is not set, it is set to the same value as the input.
	if(output_track->GetFrameRateByConfig() == 0)
	{
		output_track->SetFrameRateByConfig(input_track->GetFrameRateByConfig());
		output_track->SetFrameRateByMeasured(input_track->GetFrameRateByMeasured());
	}

	//  If the bitrate is not set, it is set to the same value as the input.
	if(output_track->GetBitrateByConfig() == 0)
	{
		output_track->SetBitrateByConfig(input_track->GetBitrateByConfig());
		output_track->SetBitrateByMeasured(input_track->GetBitrateByMeasured());
	}

	if (cmn::IsVideoCodec(output_track->GetCodecId()) == false)
	{
		logtw("%s codec is not supported video codec", profile.GetCodec().CStr());
		return nullptr;
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

	profile.GetBitrateString(&is_parsed);
	if (is_parsed == true)
	{
		output_track->SetBitrateByConfig(profile.GetBitrate());
	}

	output_track->SetMediaType(cmn::MediaType::Audio);
	output_track->SetId(NewTrackId());
	output_track->SetVariantName(profile.GetName());

	ov::String public_name = ov::String::FormatString("%s_%d", input_track->GetPublicName().CStr(), output_track->GetId());
	output_track->SetPublicName(public_name);
	output_track->SetLanguage(input_track->GetLanguage());
	output_track->SetCharacteristics(input_track->GetCharacteristics());
	output_track->SetOriginBitstream(input_track->GetOriginBitstream());

	bool need_bypass = IsMatchesBypassCondition(input_track, profile);
	if (need_bypass == true)
	{
		output_track->SetBypass(true);
		output_track->SetCodecId(input_track->GetCodecId());
		output_track->SetCodecModules(input_track->GetCodecModules());
		output_track->SetCodecModuleId(input_track->GetCodecModuleId());
		output_track->SetChannel(input_track->GetChannel());
		output_track->GetSample().SetFormat(input_track->GetSample().GetFormat());
		output_track->SetTimeBase(input_track->GetTimeBase());
		output_track->SetSampleRate(input_track->GetSampleRate());
		output_track->SetDecoderConfigurationRecord(input_track->GetDecoderConfigurationRecord());
	}
	else
	{
		output_track->SetBypass(false);
		output_track->SetCodecId(cmn::GetCodecIdByName(profile.GetCodec()));
		output_track->SetCodecModules(profile.GetModules());
		output_track->GetChannel().SetLayout(profile.GetChannel() == 1 ? cmn::AudioChannel::Layout::LayoutMono : cmn::AudioChannel::Layout::LayoutStereo);
		output_track->GetSample().SetFormat(input_track->GetSample().GetFormat());	// The sample format will change by the decoder event.
		output_track->SetSampleRate(profile.GetSamplerate());

		// Samplerate
		if (output_track->GetCodecId() == cmn::MediaCodecId::Opus)
		{
			if (output_track->GetSampleRate() != 48000)
			{
				output_track->SetSampleRate(48000);
				logtw("OPUS codec only supports 48000Hz samplerate. change the samplerate to 48000Hz");
			}
		}

		// Timebase
		if (output_track->GetSampleRate() == 0)
		{
			output_track->SetTimeBase(GetDefaultTimebaseByCodecId(output_track->GetCodecId()));
		}
		else
		{
			output_track->SetTimeBase(1, output_track->GetSampleRate());
		}
	}

	//  If the bitrate is not set, it is set to the same value as the input.
	if(output_track->GetBitrateByConfig() == 0)
	{
		output_track->SetBitrateByConfig(input_track->GetBitrateByConfig());
		output_track->SetBitrateByMeasured(input_track->GetBitrateByMeasured());
	}

	if (cmn::IsAudioCodec(output_track->GetCodecId()) == false)
	{
		logtw("%s codec is not supported audio codec", profile.GetCodec().CStr());
		return nullptr;
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

	bool is_parsed;
	profile.GetWidth(&is_parsed);
	if (is_parsed == true)
	{
		output_track->SetWidthByConfig(profile.GetWidth());
	}

	profile.GetHeight(&is_parsed);
	if (is_parsed == true)
	{
		output_track->SetHeightByConfig(profile.GetHeight());
	}

	profile.GetFramerate(&is_parsed);
	if (is_parsed == true)
	{
		output_track->SetFrameRateByConfig(profile.GetFramerate());
	}

	// If SkipFrames is enabled, it affects the output track's framerate and keyframe interval.
	profile.GetSkipFrames(&is_parsed);
	if (is_parsed == true)
	{
		auto skip_frames = profile.GetSkipFrames();

		// Set ouput framerate according to the skip frames.
		if (skip_frames >= 0)
		{
			output_track->SetSkipFramesByConfig(skip_frames);

			// Adjust the framerate according to the skip frames. 
			// When skipFrames is enabled, the user-set framerate is ignored, and the input framerate is adjusted by applying skipFrames.
			// Round adjusted_frame_rate to 2 decimal places
			auto adjusted_frame_rate = ::round(input_track->GetFrameRate() / (skip_frames + 1) * 100.0) / 100.0;

			logtt("Adjust the output framerate %.02f -> %.02f according to the skip frames %d",
				  input_track->GetFrameRate(),
				  adjusted_frame_rate,
				  skip_frames);
			output_track->SetFrameRateByConfig(adjusted_frame_rate);

#if 0 // This feature needs more consideration. It may confuse to users.
			// Set keyframe interval according to the skip frames.
			//  If the keyframe interval is not set, it will be automatically adjusted by the encoder.
			if (output_track->GetKeyFrameIntervalByConfig() > 0)
			{
				auto adjusted_key_frame_interval = ::round(output_track->GetKeyFrameIntervalByConfig() / (skip_frames + 1));
				logtt("Adjust the output key_frame_interval %.02f -> %.02f according to the skip frames %d",
					  output_track->GetKeyFrameIntervalByConfig(),
					  adjusted_key_frame_interval,
					  skip_frames);

				output_track->SetKeyFrameIntervalByConfig(adjusted_key_frame_interval);
			}
#endif
		}
	}

	output_track->SetPublicName(input_track->GetPublicName());
	output_track->SetLanguage(input_track->GetLanguage());
	output_track->SetCharacteristics(input_track->GetCharacteristics());
	output_track->SetVariantName(profile.GetName());
	output_track->SetOriginBitstream(input_track->GetOriginBitstream());

	output_track->SetMediaType(cmn::MediaType::Video);
	output_track->SetId(NewTrackId());
	output_track->SetBypass(false);
	output_track->SetCodecId(cmn::GetCodecIdByName(profile.GetCodec()));
	output_track->SetCodecModules(profile.GetModules());
	output_track->SetWidth(profile.GetWidth());
	output_track->SetHeight(profile.GetHeight());
	output_track->SetTimeBase(GetDefaultTimebaseByCodecId(output_track->GetCodecId()));

	// Github Issue : #1417
	// Set any value for quick validation of the output track. 
	// If the validation of OutputTrack is delayed, the Stream Prepare event occurs late in Publisher.
	// The bitrate of an image doesn’t mean much anyway.
	output_track->SetBitrateByConfig(0);
	output_track->SetBitrateByMeasured(1000000);
	
	//  If the framerate is not set, it is set to the same value as the input.
	if(output_track->GetFrameRateByConfig() == 0)
	{
		output_track->SetFrameRateByMeasured(input_track->GetFrameRate());
	}
	else
	{
		output_track->SetFrameRateByMeasured(output_track->GetFrameRateByConfig());
	}
	

	if (cmn::IsImageCodec(output_track->GetCodecId()) == false)
	{
		logtw("%s codec is not supported image codec", profile.GetCodec().CStr());
		return nullptr;
	}

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
	output_track->SetMaxWidth(input_track->GetMaxWidth());
	output_track->SetMaxHeight(input_track->GetMaxHeight());
	output_track->SetWidth(input_track->GetWidth());
	output_track->SetHeight(input_track->GetHeight());
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
	output_track->SetVariantName(ov::String::FormatString("SpeechToText_%d", output_track->GetId()));
	output_track->SetPublicName(ov::String::FormatString("SpeechToText_%d", output_track->GetId()));
	output_track->SetLanguage(profile.GetSourceLanguage());
	
	output_track->SetCodecId(cmn::MediaCodecId::Whisper);
	
	output_track->SetOriginBitstream(input_track->GetOriginBitstream());
	output_track->SetTimeBase(input_track->GetTimeBase());

	// Set Speech-To-Text specific properties
	output_track->SetEngine(profile.GetEngine());
	output_track->SetModel(profile.GetModel());
	output_track->SetSourceLanguage(profile.GetSourceLanguage());
	output_track->SetTranslation(profile.ShouldTranslate());
	output_track->SetOutputLabel(profile.GetOutputTrackLabel());

	if (profile.GetEngine().LowerCaseString() == "whisper")
	{
		// Whisper only supports 16kHz mono audio input and float sample format.
		output_track->SetSampleRate(16000);
		output_track->SetTimeBase(1, 16000);
		output_track->GetChannel().SetLayout(cmn::AudioChannel::Layout::LayoutMono);
		output_track->GetSample().SetFormat(cmn::AudioSample::Format::Flt);
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
	if (is_parsed == true)
	{
		if ((condition == "EQ") && (input_track->GetWidth() != profile.GetWidth()))
		{
			return false;
		}
		else if ((condition == "LTE") && input_track->GetWidth() > profile.GetWidth())
		{
			return false;
		}
		else if (condition == "GTE" && (input_track->GetWidth() < profile.GetWidth()))
		{
			return false;
		}

		if_count++;
	}

	condition = if_match.GetHeight(&is_parsed).UpperCaseString();
	if (is_parsed == true)
	{
		if ((condition == "EQ") && (input_track->GetHeight() != profile.GetHeight()))
		{
			return false;
		}
		else if ((condition == "LTE") && input_track->GetHeight() > profile.GetHeight())
		{
			return false;
		}
		else if (condition == "GTE" && (input_track->GetHeight() < profile.GetHeight()))
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
			float track_sar = (float)input_track->GetWidth() / (float)input_track->GetHeight();
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

void TranscoderStreamInternal::UpdateOutputTrackPassthrough(const std::shared_ptr<MediaTrack> &output_track, std::shared_ptr<MediaFrame> buffer)
{
	if (output_track->GetMediaType() == cmn::MediaType::Video)
	{
		output_track->SetWidth(buffer->GetWidth());
		output_track->SetHeight(buffer->GetHeight());
		output_track->SetColorspace(buffer->GetFormat<cmn::VideoPixelFormatId>());
	}
	else if (output_track->GetMediaType() == cmn::MediaType::Audio)
	{
		output_track->SetSampleRate(buffer->GetSampleRate());
		output_track->GetSample().SetFormat(buffer->GetFormat<cmn::AudioSample::Format>());
		output_track->SetChannel(buffer->GetChannels());
	}
}

void TranscoderStreamInternal::UpdateOutputTrackTranscode(const std::shared_ptr<MediaTrack> &output_track, const std::shared_ptr<MediaTrack> &input_track, std::shared_ptr<MediaFrame> buffer)
{
	if (output_track->GetMediaType() == cmn::MediaType::Video)
	{
		float aspect_ratio = (float)buffer->GetWidth() / (float)buffer->GetHeight();

		// Keep the original video resolution
		if (output_track->GetWidth() == 0 && output_track->GetHeight() == 0)
		{
			output_track->SetWidth(buffer->GetWidth());
			output_track->SetHeight(buffer->GetHeight());
		}
		// Width is automatically calculated as the original video ratio
		else if (output_track->GetWidth() == 0 && output_track->GetHeight() != 0)
		{
			int32_t width = (int32_t)((float)output_track->GetHeight() * aspect_ratio);
			width = (width % 2 == 0) ? width : width + 1;
			output_track->SetWidth(width);
		}
		// Heigh is automatically calculated as the original video ratio
		else if (output_track->GetWidth() != 0 && output_track->GetHeight() == 0)
		{
			int32_t height = (int32_t)((float)output_track->GetWidth() / aspect_ratio);
			height = (height % 2 == 0) ? height : height + 1;
			output_track->SetHeight(height);
		}

		// Set framerate of the output track
		if (output_track->GetFrameRateByConfig() == 0.0f)
		{
			output_track->SetFrameRateByMeasured(input_track->GetFrameRateByMeasured());
		}

		// To be compatible with all hardware. The encoding resolution must be a multiple of 4
		// In particular, Xilinx Media Accelerator must have a resolution specified in multiples of 4.
		if (output_track->GetWidth() % 4 != 0)
		{
			int32_t new_width = (output_track->GetWidth() / 4 + 1) * 4;

			logtt("The width of the output track is not a multiple of 4. change the width to %d -> %d", output_track->GetWidth(), new_width);

			output_track->SetWidth(new_width);
		}

		if (output_track->GetHeight() % 4 != 0)
		{
			int32_t new_height = (output_track->GetHeight() / 4 + 1) * 4;

			logtt("The height of the output track is not a multiple of 4. change the height to %d -> %d", output_track->GetHeight(), new_height);

			output_track->SetHeight(new_height);
		}
	}
	else if (output_track->GetMediaType() == cmn::MediaType::Audio)
	{
		if (output_track->GetSampleRate() == 0)
		{
			output_track->SetSampleRate(buffer->GetSampleRate());
			output_track->SetTimeBase(1, buffer->GetSampleRate());
		}

		if (output_track->GetChannel().GetLayout() == cmn::AudioChannel::Layout::LayoutUnknown)
		{
			output_track->SetChannel(buffer->GetChannels());
		}
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