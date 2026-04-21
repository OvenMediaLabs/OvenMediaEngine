//==============================================================================
//
//  TranscoderStream
//
//  Created by Keukhan
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#include "transcoder_stream.h"

#include <monitoring/monitoring.h>

#include "config/config_manager.h"
#include "modules/transcode_webhook/transcode_webhook.h"
#include "orchestrator/orchestrator.h"
#include "transcoder_application.h"
#include "transcoder_modules.h"
#include "transcoder_private.h"

#define UNUSED_VARIABLE(var) (void)var;
#define MAX_FILLER_FRAMES 100
#define FILLER_ENABLED true
#define NOTIFICATION_ENABLED true

// max initial media packet buffer size, for OOM protection
#define MAX_INITIAL_MEDIA_PACKET_BUFFER_SIZE 10000

std::shared_ptr<TranscoderStream> TranscoderStream::Create(const info::Application &application_info, const std::shared_ptr<info::Stream> &org_stream_info, TranscodeApplication *parent)
{
	auto stream = std::make_shared<TranscoderStream>(application_info, org_stream_info, parent);
	if (stream == nullptr)
	{
		return nullptr;
	}

	return stream;
}

TranscoderStream::TranscoderStream(const info::Application &application_info, const std::shared_ptr<info::Stream> &stream, TranscodeApplication *parent)
	: _parent(parent), _application_info(application_info), _input_stream(stream)
{
	_log_prefix			 = ov::String::FormatString("[%s(%u)]", _input_stream->GetUri().CStr(), _input_stream->GetId());

	// default output profiles configuration
	_output_profiles_cfg = &(_application_info.GetConfig().GetOutputProfiles());

	logtt("%s Trying to create transcode stream", _log_prefix.CStr());
}

TranscoderStream::~TranscoderStream()
{
	Stop();
}

info::stream_id_t TranscoderStream::GetStreamId()
{
	if (_input_stream != nullptr)
	{
		return _input_stream->GetId();
	}

	return 0;
}

bool TranscoderStream::Start()
{
	if (GetState() != State::CREATED)
	{
		return false;
	}

	SetState(State::PREPARING);

	logti("%s stream has been started", _log_prefix.CStr());

	return true;
}

bool TranscoderStream::Prepare(const std::shared_ptr<info::Stream> &stream)
{
	if (GetState() != State::PREPARING)
	{
		logte("%s Stream is not in preparing state", _log_prefix.CStr());
		return false;
	}

	// Prevent multiple calls to Prepare() from multiple threads
	if (_prepare_thread_running.exchange(true))
	{
		logtw("%s Prepare thread is already running", _log_prefix.CStr());
		return false;
	}

	try
	{
		_prepare_thread = std::thread(&TranscoderStream::PrepareAsync, this);
		// Don't detach - keep the thread joinable so we can wait for it in Stop()
	}
	catch (const std::system_error &e)
	{
		logte("%s Failed to create prepare thread: %s", _log_prefix.CStr(), e.what());
		SetState(State::ERROR);
		_prepare_thread_running = false;
		return false;
	}

	logtd("%s stream preparation started asynchronously", _log_prefix.CStr());

	return true;
}

void TranscoderStream::PrepareAsync()
{
	logtd("%s Async preparation started", _log_prefix.CStr());

	if (GetState() != State::PREPARING)
	{
		logte("%s Stream is not in preparing state", _log_prefix.CStr());
		_prepare_thread_running = false;
		return;
	}

	// Transcoder Webhook
	_output_profiles_cfg = RequestWebhook();
	if (_output_profiles_cfg == nullptr)
	{
		logte("%s There is no output profiles", _log_prefix.CStr());
		SetState(State::ERROR);
		_prepare_thread_running = false;
		return;
	}

	// Create Ouput Streams & Notify to create a new stream to the media router
	if (!StartInternal())
	{
		logte("%s Failed to create the stream", _log_prefix.CStr());
		SetState(State::ERROR);
		_prepare_thread_running = false;
		return;
	}

	if (!PrepareInternal())
	{
		logte("%s Failed to prepare the stream", _log_prefix.CStr());
		SetState(State::ERROR);
		_prepare_thread_running = false;
		return;
	}

	SetState(State::STARTED);
	_prepare_thread_running = false;

	logti("%s stream has been prepared", _log_prefix.CStr());
}

bool TranscoderStream::Update(const std::shared_ptr<info::Stream> &stream)
{
	if (GetState() != State::STARTED)
	{
		logtt("%s stream is not started", _log_prefix.CStr());
		return false;
	}

	return UpdateInternal(stream);
}

bool TranscoderStream::Stop()
{
	if (_prepare_thread.joinable())
	{
		logtt("%s Waiting for prepare thread to complete", _log_prefix.CStr());
		_prepare_thread.join();
		logtt("%s Prepare thread joined", _log_prefix.CStr());
	}

	if (GetState() == State::STOPPED)
	{
		return true;
	}

	RemoveDecoders();
	RemoveFilters();
	RemoveEncoders();

	// Delete all composite components
	_composite.Clear();

	// Delete all last decoded frame information
	RemoveLastDecodedFrame();

	// Notify to delete the stream created on the MediaRouter
	NotifyDeleteStreams();

	// Delete all output streams information
	RemoveOutputStreams();

	SetState(State::STOPPED);

	logti("%s stream has been stopped", _log_prefix.CStr());

	return true;
}

bool TranscoderStream::PauseEncoders(cmn::MediaCodecId codec_id)
{
	bool found = false;
	std::shared_lock<std::shared_mutex> lock(_encoder_map_mutex);
	for (auto &[encoder_id, filter_encoder_pair] : _encoders)
	{
		auto &encoder = filter_encoder_pair.second;
		if (encoder && encoder->GetCodecID() == codec_id)
		{
			encoder->Pause();
			found = true;
		}
	}
	return found;
}

bool TranscoderStream::ResumeEncoders(cmn::MediaCodecId codec_id)
{
	bool found = false;
	std::shared_lock<std::shared_mutex> lock(_encoder_map_mutex);
	for (auto &[encoder_id, filter_encoder_pair] : _encoders)
	{
		auto &encoder = filter_encoder_pair.second;
		if (encoder && encoder->GetCodecID() == codec_id)
		{
			encoder->Resume();
			found = true;
		}
	}
	return found;
}

bool TranscoderStream::IsEncoderPaused(cmn::MediaCodecId codec_id)
{
	std::shared_lock<std::shared_mutex> lock(_encoder_map_mutex);
	for (auto &[encoder_id, filter_encoder_pair] : _encoders)
	{
		auto &encoder = filter_encoder_pair.second;
		if (encoder && encoder->GetCodecID() == codec_id)
		{
			return encoder->IsPaused();
		}
	}
	return false;
}

ov::String TranscoderStream::GetInputStreamName() const
{
	return _input_stream ? _input_stream->GetName() : "";
}

std::vector<TranscodeEncoder::EncoderInfo> TranscoderStream::GetEncoderInfoList(cmn::MediaCodecId codec_id)
{
	std::vector<TranscodeEncoder::EncoderInfo> result;
	std::shared_lock<std::shared_mutex> lock(_encoder_map_mutex);
	for (auto &[encoder_id, filter_encoder_pair] : _encoders)
	{
		auto &encoder = filter_encoder_pair.second;
		if (encoder && encoder->GetCodecID() == codec_id)
		{
			result.push_back(encoder->GetInfo());
		}
	}
	return result;
}

const cfg::vhost::app::oprf::OutputProfiles *TranscoderStream::RequestWebhook()
{
	// Measure response time of transcode webhook
	ov::StopWatch response_time;
	response_time.Start();

	TranscodeWebhook webhook(_application_info);
	auto policy = webhook.RequestOutputProfiles(*GetInputStream(), _remote_output_profiles);

	if (policy == TranscodeWebhook::Policy::DeleteStream)
	{
		logtw("%s Delete a stream by transcode webhook", _log_prefix.CStr());
		ocst::Orchestrator::GetInstance()->TerminateStream(_application_info.GetVHostAppName(), GetInputStream()->GetName());
		return nullptr;
	}
	else if (policy == TranscodeWebhook::Policy::CreateStream)
	{
		Json::StreamWriterBuilder builder;
		builder["indentation"] = "";
		builder["emitUTF8"]	   = true;

		logti("%s Using external output profiles by webhook. Response time: %" PRId64 " ms", _log_prefix.CStr(), response_time.Elapsed());
		logti("%s OutputProfile: %s", _log_prefix.CStr(), Json::writeString(builder, _remote_output_profiles.ToJson()).c_str());
		return &_remote_output_profiles;
	}
	else if (policy == TranscodeWebhook::Policy::UseLocalProfiles)
	{
		Json::StreamWriterBuilder builder;
		builder["indentation"] = "";
		builder["emitUTF8"]	   = true;

		logti("%s Using local output profiles by webhook. Response time: %" PRId64 " ms", _log_prefix.CStr(), response_time.Elapsed());
		logtt("%s OutputProfile: %s", _log_prefix.CStr(), Json::writeString(builder, _application_info.GetConfig().GetOutputProfiles().ToJson()).c_str());
		return &(_application_info.GetConfig().GetOutputProfiles());
	}

	return nullptr;
}

bool TranscoderStream::StartInternal()
{
	// If the application is created by Dynamic, make it bypass in Default Stream.
	if (_application_info.IsDynamicApp() == true)
	{
		if (CreateOutputStreamDynamic() == 0)
		{
			logte("No output stream generated");
			return false;
		}
	}
	else
	{
		if (CreateOutputStreams() == 0)
		{
			logte("No output stream generated");
			return false;
		}
	}

	// Notify to create a new stream on the media router.
	NotifyCreateStreams();

	return true;
}

bool TranscoderStream::PrepareInternal()
{
	if (!_composite.Build())
	{
		logte("%s Failed to create components", _log_prefix.CStr());

		return false;
	}
	else
	{
		logtd("%s Components have been created successfully. %s", _log_prefix.CStr(), _composite.GetInfoString().CStr());
	}

	if (CreateDecoders() == false)
	{
		logte("%s Failed to create decoders", _log_prefix.CStr());

		return false;
	}

	// Store track information for later use in transcoding and seamless transition.
	StoreTracks(GetInputStream());

	return true;
}

bool TranscoderStream::UpdateInternal(const std::shared_ptr<info::Stream> &stream)
{
	logtd("%s Trying to update a stream", _log_prefix.CStr());

	if (CanSeamlessTransition(stream) == true)
	{
		logtt("%s This stream support seamless transitions", _log_prefix.CStr());

		FlushBuffers();

		{
			// Restrict transcoding while all decoders/filters/encoders are being generated
			std::unique_lock<std::shared_mutex> pipeline_lock(_pipeline_mutex);

			// For tracks created as bypass, update the track data to match the input track.
			UpdatePassthroughOutputTracks(stream);

			RemoveDecoders();
			RemoveFilters();
			RemoveSpecificEncoders();

			CreateDecoders();
		}

		logti("%s stream has been updated", _log_prefix.CStr());
	}
	else
	{
		logtw("%s This stream does not support seamless transitions. Renewing all", _log_prefix.CStr());

		FlushBuffers();
	
		{
			// Restrict transcoding while all decoders/filters/encoders are being generated			
			std::unique_lock<std::shared_mutex> pipeline_lock(_pipeline_mutex);

			// When the entire stream is changed, update the MSID.
			UpdateMsidOfOutputStreams(stream->GetMsid());
			// For tracks created as bypass, update the track data to match the input track.
			UpdatePassthroughOutputTracks(stream);

			RemoveDecoders();
			RemoveFilters();
			RemoveEncoders();

			CreateDecoders();
		}

		logti("%s stream has been updated", _log_prefix.CStr());

		NotifyUpdateStreams();

		StoreTracks(stream);
	}

	return true;
}

void TranscoderStream::FlushBuffers()
{
	for (auto &[id, object] : _encoders)
	{
		auto filter = object.first;
		if (filter != nullptr)
		{
			filter->Flush();
		}

		auto encoder = object.second;
		if (encoder != nullptr)
		{
			encoder->Flush();
		}
	}
}

void TranscoderStream::RemoveDecoders()
{
	std::unique_lock<std::shared_mutex> decoder_lock(_decoder_map_mutex);

	auto decoders = _decoders;
	_decoders.clear();

	decoder_lock.unlock();

	for (auto &[id, object] : decoders)
	{
		if (object != nullptr)
		{
			object->Stop();
			object.reset();
		}
	}
}

void TranscoderStream::RemoveFilters()
{
	std::unique_lock<std::shared_mutex> filter_lock(_filter_map_mutex);

	auto filters = _filters;
	_filters.clear();

	filter_lock.unlock();

	for (auto &[id, object] : filters)
	{
		if (object != nullptr)
		{
			object->Stop();
			object.reset();
		}
	}
}

// In a scheduled stream, when the video input changes and the decoder is reinitialized,
//  the NVIDIA encoder must also be reinitialized. If not, video corruption may occur.
void TranscoderStream::RemoveSpecificEncoders()
{
	// Collect specific encoders to remove under read lock
	std::vector<MediaTrackId> ids_to_remove;
	{
		std::shared_lock<std::shared_mutex> read_lock(_encoder_map_mutex);
		for (auto &[id, object] : _encoders)
		{
			auto &encoder = object.second;
			if (encoder->GetRefTrack()->GetMediaType() != cmn::MediaType::Video)
			{
				continue;
			}

			if (encoder->GetRefTrack()->GetCodecModuleId() != cmn::MediaCodecModuleId::NVENC)
			{
				continue;
			}

			ids_to_remove.push_back(id);
		}
	}

	// Extract matching entries under write lock
	decltype(_encoders) removed;
	{
		std::unique_lock<std::shared_mutex> write_lock(_encoder_map_mutex);
		for (auto id : ids_to_remove)
		{
			auto it = _encoders.find(id);
			if (it != _encoders.end())
			{
				removed.insert(_encoders.extract(it));
			}
		}
	}

	// Stop components outside the lock (Stop() may block)
	for (auto &[id, object] : removed)
	{
		UNUSED_VARIABLE(id)

		if (auto &filter = object.first)
		{
			filter->Stop();
			filter.reset();
		}

		if (auto &encoder = object.second)
		{
			encoder->Stop();
			encoder.reset();
		}
	}
}

void TranscoderStream::RemoveEncoders()
{
	std::unique_lock<std::shared_mutex> read_lock(_encoder_map_mutex);

	auto encoders = _encoders;
	_encoders.clear();

	read_lock.unlock();

	for (auto &[id, object] : encoders)
	{
		auto filter = object.first;
		if (filter != nullptr)
		{
			filter->Stop();
			filter.reset();
		}

		auto encoder = object.second;
		if (encoder != nullptr)
		{
			encoder->Stop();
			encoder.reset();
		}
	}
}

std::shared_ptr<MediaTrack> TranscoderStream::GetInputTrack(MediaTrackId track_id)
{
	if (_input_stream == nullptr)
	{
		return nullptr;
	}

	return _input_stream->GetTrack(track_id);
}

std::shared_ptr<info::Stream> TranscoderStream::GetInputStream()
{
	return _input_stream;
}

std::shared_ptr<info::Stream> TranscoderStream::GetOutputStreamByTrackId(MediaTrackId output_track_id)
{
	std::shared_lock<std::shared_mutex> read_lock(_output_stream_mutex);
	for (auto &[stream_name, output_stream] : _output_streams)
	{
		UNUSED_VARIABLE(stream_name)

		if (output_stream->GetTrack(output_track_id) != nullptr)
		{
			return output_stream;
		}
	}

	return nullptr;
}

bool TranscoderStream::CanSeamlessTransition(const std::shared_ptr<info::Stream> &input_stream)
{
	auto new_tracks = input_stream->GetTracks();

	// Check if the number and type of original tracks are different.
	if (CompareTracksForSeamlessTransition(new_tracks, GetStoredTracks()) == false)
	{
		logtw("%s The input track has changed. It does not support smooth transitions.", _log_prefix.CStr());
		return false;
	}

	return true;
}

bool TranscoderStream::Push(std::shared_ptr<MediaPacket> packet)
{
	if (GetState() == State::STARTED)
	{
		SendBufferedPackets();

		ProcessPacket(std::move(packet));
	}
	else if (GetState() == State::CREATED || GetState() == State::PREPARING)
	{
		BufferMediaPacketUntilReadyToPlay(packet);
	}
	else if (GetState() == State::ERROR)
	{
		return false;
	}

	// State::STOPPED : Do nothing

	return true;
}

void TranscoderStream::BufferMediaPacketUntilReadyToPlay(const std::shared_ptr<MediaPacket> &media_packet)
{
	if (_initial_media_packet_buffer.Size() >= MAX_INITIAL_MEDIA_PACKET_BUFFER_SIZE)
	{
		// Drop the oldest packet, for OOM protection
		_initial_media_packet_buffer.Dequeue(0);
	}

	_initial_media_packet_buffer.Enqueue(media_packet);
}

bool TranscoderStream::SendBufferedPackets()
{
	logtt("SendBufferedPackets - BufferSize (%lu)", _initial_media_packet_buffer.Size());

	while (_initial_media_packet_buffer.IsEmpty() == false)
	{
		auto buffered_media_packet = _initial_media_packet_buffer.Dequeue();
		if (buffered_media_packet.has_value() == false)
		{
			continue;
		}

		auto media_packet = buffered_media_packet.value();

		ProcessPacket(std::move(media_packet));
	}

	return true;
}

// Dynamically generated applications are created by default with BYPASS profiles.
size_t TranscoderStream::CreateOutputStreamDynamic()
{
	auto output_stream = std::make_shared<info::Stream>(_application_info, StreamSourceType::Transcoder);
	if (output_stream == nullptr)
	{
		return 0;
	}

	output_stream->SetName(_input_stream->GetName());
	output_stream->SetMediaSource(_input_stream->GetUUID());
	output_stream->LinkInputStream(_input_stream);

	for (auto &[input_track_id, input_track] : _input_stream->GetTracks())
	{
		UNUSED_VARIABLE(input_track_id)

		auto output_track = input_track->Clone();
		if (output_track == nullptr)
		{
			continue;
		}

		output_track->SetBypass(true);
		output_track->SetId(NewTrackId());

		output_stream->AddTrack(output_track);

		auto signature = ov::String::FormatString("dynamic");
		_composite.AddComposite(_input_stream, input_track, signature, output_stream, output_track);
	}

	// Add to Output Stream List. The key is the output stream name.
	{
		std::unique_lock<std::shared_mutex> lock(_output_stream_mutex);
		_output_streams.insert(std::make_pair(output_stream->GetName(), output_stream));
	}

	logti("%s Output stream(dynamic) has been created. [%s(%u)]",
		  _log_prefix.CStr(), output_stream->GetUri().CStr(), output_stream->GetId());

	std::shared_lock<std::shared_mutex> lock(_output_stream_mutex);
	return _output_streams.size();
}

size_t TranscoderStream::CreateOutputStreams()
{
	// Get the output profile to make the output stream
	auto cfg_output_profile_list = GetOutputProfilesCfg()->GetOutputProfileList();

	for (const auto &profile : cfg_output_profile_list)
	{
		auto output_stream = CreateOutputStream(profile);
		if (output_stream == nullptr)
		{
			logte("%s Could not create output stream. name:%s", _log_prefix.CStr(), profile.GetName().CStr());

#if NOTIFICATION_ENABLED
			TranscoderAlerts::UpdateErrorWithoutCount(
				TranscoderAlerts::ErrorType::CREATION_ERROR_PROFILE,
				std::make_shared<cfg::vhost::app::oprf::OutputProfile>(profile),
				_input_stream,
				nullptr,
				nullptr,
				nullptr);
#endif

			continue;
		}

		{
			std::unique_lock<std::shared_mutex> lock(_output_stream_mutex);
			_output_streams.insert(std::make_pair(output_stream->GetName(), output_stream));
		}

		logti("%s Output stream has been created. [%s(%u)]", _log_prefix.CStr(), output_stream->GetUri().CStr(), output_stream->GetId());
	}

	// STT (Speech-to-Text) output streams.
	// Config: <OutputProfiles><MediaOptions><STT><Rendition>
	// Legacy <Application><Subtitle><Rendition><Transcription> is deprecated and logs a warning; it is not used for STT stream creation.
	{
		auto new_output_profile_name = ov::String::FormatString("%s#stt", _input_stream->GetName().CStr());

		cfg::vhost::app::oprf::OutputProfile cfg_new_output_profile;
		cfg_new_output_profile.SetInternal(true);
		cfg_new_output_profile.SetName(new_output_profile_name);
		cfg_new_output_profile.SetOutputStreamName(new_output_profile_name);

		cfg::vhost::app::oprf::Encodes encodes;
		int i				= 0;

		// 1. New config: <MediaOptions><STT>
		const auto &cfg_stt = GetOutputProfilesCfg()->GetMediaOptions().GetStt();
		for (const auto &stt_rendition : cfg_stt.GetRenditions())
		{
			auto name				   = ov::String::FormatString("SpeechToText_%d", i);
			auto model_path			   = ov::GetFilePath(stt_rendition.GetModel(), cfg::ConfigManager::GetInstance()->GetConfigPath());

			auto input_audio_track	   = _input_stream->GetMediaTrackByOrder(cmn::MediaType::Audio, stt_rendition.GetInputAudioIndex());
			auto output_subtitle_track = _input_stream->GetTrackByLabel(stt_rendition.GetOutputSubtitleLabel());

			if (input_audio_track == nullptr || output_subtitle_track == nullptr)
			{
				logte("Could not find input audio track or output subtitle track for STT. InputAudioIndex(%d), OutputSubtitleLabel(%s)",
					  stt_rendition.GetInputAudioIndex(), stt_rendition.GetOutputSubtitleLabel().CStr());
				i++;
				continue;
			}

			cfg::vhost::app::oprf::SpeechToTextProfile speech_to_text_profile(name,
																			  stt_rendition.GetEngine(),
																			  model_path,
																			  input_audio_track->GetId(),
																			  output_subtitle_track->GetId());
			speech_to_text_profile.SetSourceLanguage(stt_rendition.GetSourceLanguage());
			speech_to_text_profile.SetTranslation(stt_rendition.GetTranslation());
			speech_to_text_profile.SetOutputTrackLabel(stt_rendition.GetOutputSubtitleLabel());
			speech_to_text_profile.SetStepMs(stt_rendition.GetStepMs());
			speech_to_text_profile.SetLengthMs(stt_rendition.GetLengthMs());
			speech_to_text_profile.SetKeepMs(stt_rendition.GetKeepMs());
			speech_to_text_profile.SetModules(stt_rendition.GetModules());
			speech_to_text_profile.SetSttEnabled(cfg_stt.IsEnabled());

			encodes.AddSpeechToTextProfiles(speech_to_text_profile);
			i++;
		}

		cfg_new_output_profile.SetEncodes(encodes);

		if (cfg_new_output_profile.GetEncodes().GetSpeechToTextProfileList().size() > 0)
		{
			auto output_stream = CreateOutputStream(cfg_new_output_profile);
			if (output_stream == nullptr)
			{
				logte("%s Could not create output stream for STT. name:%s", _log_prefix.CStr(), cfg_new_output_profile.GetName().CStr());

#if NOTIFICATION_ENABLED
				TranscoderAlerts::UpdateErrorWithoutCount(
					TranscoderAlerts::ErrorType::CREATION_ERROR_PROFILE,
					std::make_shared<cfg::vhost::app::oprf::OutputProfile>(cfg_new_output_profile),
					_input_stream,
					nullptr,
					nullptr,
					nullptr);
#endif
			}
			else
			{
				output_stream->SetInternal(true);
				{
					std::unique_lock<std::shared_mutex> lock(_output_stream_mutex);
					_output_streams.insert(std::make_pair(output_stream->GetName(), output_stream));
				}

				logti("%s Output stream(STT) has been created. [%s(%u)]", _log_prefix.CStr(), output_stream->GetUri().CStr(), output_stream->GetId());
			}
		}
	}

	std::shared_lock<std::shared_mutex> lock(_output_stream_mutex);
	return _output_streams.size();
}

std::shared_ptr<info::Stream> TranscoderStream::CreateOutputStream(const cfg::vhost::app::oprf::OutputProfile &cfg_output_profile)
{
	if (cfg_output_profile.GetOutputStreamName().IsEmpty())
	{
		return nullptr;
	}

	auto input_stream = GetInputStream();
	if (input_stream == nullptr)
	{
		return nullptr;
	}

	auto output_stream = std::make_shared<info::Stream>(_application_info, StreamSourceType::Transcoder);
	if (output_stream == nullptr)
	{
		return nullptr;
	}

	// It helps modules to recognize origin stream from provider
	output_stream->LinkInputStream(input_stream);
	output_stream->SetMediaSource(input_stream->GetUUID());
	output_stream->SetOutputProfileName(cfg_output_profile.GetName());

	// Create a output stream name.
	auto name = cfg_output_profile.GetOutputStreamName();
	if (::strstr(name.CStr(), "${OriginStreamName}") != nullptr)
	{
		name = name.Replace("${OriginStreamName}", input_stream->GetName());
	}
	output_stream->SetName(name);

	// Create a Output Track
	for (auto &[input_track_id, input_track] : input_stream->GetTracks())
	{
		switch (input_track->GetMediaType())
		{
			case cmn::MediaType::Video: {
				// Video Profile
				for (auto &profile : cfg_output_profile.GetEncodes().GetVideoProfileList())
				{
					auto output_track = CreateOutputTrack(input_track, profile);
					if (output_track == nullptr)
					{
						logte("[%s] Failed to create video track. Encoding options need to be checked. InputTrack(%d)", _log_prefix.CStr(), input_track_id);

						return nullptr;
					}

					output_stream->AddTrack(output_track);

					auto signature = ProfileToSerialize(input_track_id, profile);
					_composite.AddComposite(input_stream, input_track, signature, output_stream, output_track);
				}

				// Image Profile
				for (auto &profile : cfg_output_profile.GetEncodes().GetImageProfileList())
				{
					auto output_track = CreateOutputTrack(input_track, profile);
					if (output_track == nullptr)
					{
						logte("[%s] Failed to create image track. Encoding options need to be checked. InputTrack(%d)", _log_prefix.CStr(), input_track_id);

						return nullptr;
					}

					output_stream->AddTrack(output_track);

					auto signature = ProfileToSerialize(input_track_id, profile);
					_composite.AddComposite(input_stream, input_track, signature, output_stream, output_track);
				}
			}
			break;
			case cmn::MediaType::Audio: {
				// Audio Profile
				for (auto &profile : cfg_output_profile.GetEncodes().GetAudioProfileList())
				{
					auto output_track = CreateOutputTrack(input_track, profile);
					if (output_track == nullptr)
					{
						logte("[%s] Failed to create audio track. Encoding options need to be checked. InputTrack(%d)", _log_prefix.CStr(), input_track_id);

						return nullptr;
					}

					output_stream->AddTrack(output_track);

					auto signature = ProfileToSerialize(input_track_id, profile);
					_composite.AddComposite(input_stream, input_track, signature, output_stream, output_track);
				}

				// SpeechToText Profile
				for (auto &profile : cfg_output_profile.GetEncodes().GetSpeechToTextProfileList())
				{
					// Check if the input track is matched.
					if (static_cast<int32_t>(profile.GetInputTrackId()) != input_track_id)
					{
						continue;
					}

					auto output_track = CreateOutputTrack(input_track, profile);
					if (output_track == nullptr)
					{
						logte("[%s] Failed to create data track for transcription. Encoding options need to be checked. InputTrack(%d), SpeechToTextProfile(%s)", _log_prefix.CStr(), input_track_id, profile.GetName().CStr());

						return nullptr;
					}

					output_stream->AddTrack(output_track);

					auto signature = ProfileToSerialize(input_track_id, profile);
					_composite.AddComposite(input_stream, input_track, signature, output_stream, output_track);
				}
			}
			break;

			// If there is a data type track in the input stream, it must be created equally in all output streams.
			case cmn::MediaType::Data:
			case cmn::MediaType::Subtitle: {
				// Data or Subtitle must be duplicated even if there is no profile, unless
				if (cfg_output_profile.IsInternal() == true)
				{
					continue;
				}

				// Create a output track by cloning the input track.
				auto output_track = CreateOutputTrackDataType(input_track);
				if (output_track == nullptr)
				{
					logte("[%s] Failed to create data track. Encoding options need to be checked. InputTrack(%d)", _log_prefix.CStr(), input_track_id);

					return nullptr;
				}

				output_stream->AddTrack(output_track);

				auto signature = ProfileToSerialize(input_track_id);
				_composite.AddComposite(input_stream, input_track, signature, output_stream, output_track);
			}
			break;
			default: {
				logte("[%s] Unsupported media type of input track. type(%s)", _log_prefix.CStr(), cmn::GetMediaTypeString(input_track->GetMediaType()));
				continue;
			}
		}
	}

	// Playlist
	bool is_parsed	   = false;
	auto cfg_playlists = cfg_output_profile.GetPlaylists(&is_parsed);
	if (is_parsed)
	{
		for (const auto &cfg_playlist : cfg_playlists)
		{
			auto playlist_info		 = cfg_playlist.GetPlaylistInfo();

			// Create renditions with RenditionTemplate
			auto rendition_templates = cfg_playlist.GetRenditionTemplates();
			auto tracks				 = output_stream->GetTracks();

			for (const auto &rendition_template : rendition_templates)
			{
				bool has_video_template = false, has_audio_template = false;
				auto video_template = rendition_template.GetVideoTemplate(&has_video_template);
				auto audio_template = rendition_template.GetAudioTemplate(&has_audio_template);

				std::vector<std::shared_ptr<MediaTrack>> matched_video_tracks, matched_audio_tracks;

				if (has_video_template)
				{
					for (const auto &[track_id, track] : tracks)
					{
						if (video_template.IsMatched(track) == false)
						{
							continue;
						}

						// Separate the Track from the existing Variant group.
						matched_video_tracks.push_back(track);
					}
				}

				if (has_audio_template)
				{
					// Do not separate the Audio track group. (Used as Multilingual)
					for (const auto &[group_name, group] : output_stream->GetMediaTrackGroups())
					{
						auto track = group->GetFirstTrack();
						if (audio_template.IsMatched(track) == false)
						{
							continue;
						}

						matched_audio_tracks.push_back(track);
					}
				}

				if (matched_video_tracks.empty() && matched_audio_tracks.empty())
				{
					logtw("[%s] No matched tracks for the rendition template (%s).", _log_prefix.CStr(), rendition_template.GetName().CStr());
					continue;
				}

				if (matched_video_tracks.empty() == false && matched_audio_tracks.empty() == false)
				{
					for (const auto &video_track : matched_video_tracks)
					{
						for (const auto &audio_track : matched_audio_tracks)
						{
							// Make Rendition Name
							ov::String rendition_name = MakeRenditionName(rendition_template.GetName(), playlist_info, video_track, audio_track);

							auto rendition			  = std::make_shared<info::Rendition>(rendition_name, video_track->GetVariantName(), audio_track->GetVariantName());
							rendition->SetVideoIndexHint(video_track->GetGroupIndex());
							rendition->SetAudioIndexHint(audio_track->GetGroupIndex());

							playlist_info->AddRendition(rendition);

							logtd("[%s] Rendition(%s) has been created from template in Playlist(%s) : video(%s/%d), audio(%s%d)", _log_prefix.CStr(), rendition_name.CStr(), playlist_info->GetName().CStr(), video_track->GetPublicName().CStr(), video_track->GetGroupIndex(), audio_track->GetPublicName().CStr(), audio_track->GetGroupIndex());
						}
					}
				}
				else if (matched_video_tracks.empty() == false)
				{
					for (const auto &video_track : matched_video_tracks)
					{
						// Make Rendition Name
						ov::String rendition_name = MakeRenditionName(rendition_template.GetName(), playlist_info, video_track, nullptr);
						auto rendition			  = std::make_shared<info::Rendition>(rendition_name, video_track->GetVariantName(), "");
						rendition->SetVideoIndexHint(video_track->GetGroupIndex());

						playlist_info->AddRendition(rendition);

						logtd("[%s] Rendition(%s) has been created from template in Playlist(%s) : video(%s/%d)", _log_prefix.CStr(), rendition_name.CStr(), playlist_info->GetName().CStr(), video_track->GetPublicName().CStr(), video_track->GetGroupIndex());
					}
				}
				else if (matched_audio_tracks.empty() == false)
				{
					for (const auto &audio_track : matched_audio_tracks)
					{
						// Make Rendition Name
						ov::String rendition_name = MakeRenditionName(rendition_template.GetName(), playlist_info, nullptr, audio_track);

						auto rendition			  = std::make_shared<info::Rendition>(rendition_name, "", audio_track->GetVariantName());
						rendition->SetAudioIndexHint(audio_track->GetGroupIndex());

						playlist_info->AddRendition(rendition);

						logtd("[%s] Rendition(%s) has been created from template in Playlist(%s) : audio(%s/%d)", _log_prefix.CStr(), rendition_name.CStr(), playlist_info->GetName().CStr(), audio_track->GetPublicName().CStr(), audio_track->GetGroupIndex());
					}
				}
			}

			logtd("[%s] Playlist(%s) has been created", _log_prefix.CStr(), playlist_info->GetName().CStr());
			logti("[%s] %s", _log_prefix.CStr(), playlist_info->ToString().CStr());

			output_stream->AddPlaylist(playlist_info);
		}
	}

	return output_stream;
}

void TranscoderStream::RemoveOutputStreams()
{
	std::unique_lock<std::shared_mutex> lock(_output_stream_mutex);
	_output_streams.clear();
}

ov::String TranscoderStream::MakeRenditionName(const ov::String &name_template, const std::shared_ptr<info::Playlist> &playlist_info, const std::shared_ptr<MediaTrack> &video_track, const std::shared_ptr<MediaTrack> &audio_track)
{
	ov::String rendition_name = name_template;

	if (video_track != nullptr)
	{
		auto resolution = video_track->GetResolution();
		rendition_name	= rendition_name.Replace("${Width}", ov::String::FormatString("%d", resolution.width).CStr());
		rendition_name	= rendition_name.Replace("${Height}", ov::String::FormatString("%d", resolution.height).CStr());
		rendition_name	= rendition_name.Replace("${Bitrate}", ov::String::FormatString("%d", video_track->GetBitrate()).CStr());
		// TODO: Check if there are cases where the rendition name includes decimal points. (e.g., 29.97fps)
		rendition_name	= rendition_name.Replace("${Framerate}", ov::String::FormatString("%.0f", video_track->GetFrameRate()).CStr());
	}

	if (audio_track != nullptr)
	{
		rendition_name = rendition_name.Replace("${Samplerate}", ov::String::FormatString("%d", audio_track->GetSampleRate()).CStr());
		rendition_name = rendition_name.Replace("${Channel}", ov::String::FormatString("%u", audio_track->GetChannel().GetCounts()).CStr());
	}

	// Check if the rendition name is duplicated
	uint32_t rendition_index		 = 0;
	ov::String unique_rendition_name = rendition_name;
	while (playlist_info->GetRendition(unique_rendition_name) != nullptr)
	{
		unique_rendition_name = ov::String::FormatString("%s_%d", rendition_name.CStr(), ++rendition_index);
	}

	return unique_rendition_name;
}

bool TranscoderStream::CreateDecoders()
{
	for (auto &[input_stream, input_track, decoder_id] : _composite.GetDecoderList())
	{
		// Create Decoder
		if (CreateDecoder(decoder_id, input_stream, input_track) == false)
		{
			logte("%s Failed to create decoder. Id(%d)<Codec(%s), Module(%s), Device(%u)>, InputTrack(%d)",
				  _log_prefix.CStr(), decoder_id, cmn::GetCodecIdString(input_track->GetCodecId()),
				  cmn::GetCodecModuleIdString(input_track->GetCodecModuleId()), input_track->GetCodecDeviceId(), input_track->GetId());

#if NOTIFICATION_ENABLED
			TranscoderAlerts::UpdateErrorWithoutCount(
				TranscoderAlerts::ErrorType::CREATION_ERROR_DECODER,
				nullptr,
				input_stream,
				input_track,
				nullptr,
				nullptr);
#endif

			return false;
		}

		logtd("%s Decoder has been created. Id(%d)<Codec(%s), Module(%s), Device(%u)>, InputTrack(%d)",
			  _log_prefix.CStr(), decoder_id, cmn::GetCodecIdString(input_track->GetCodecId()),
			  cmn::GetCodecModuleIdString(input_track->GetCodecModuleId()), input_track->GetCodecDeviceId(), input_track->GetId());
	}

	return true;
}

bool TranscoderStream::CreateDecoder(MediaTrackId decoder_id, std::shared_ptr<info::Stream> input_stream, std::shared_ptr<MediaTrack> input_track)
{
	if (GetDecoder(decoder_id) != nullptr)
	{
		logtw("%s Decoder already exists. InputTrack(%d), Decoder(%d)", _log_prefix.CStr(), input_track->GetId(), decoder_id);
		return true;
	}

	// Set the keyframe decode only flag for the decoder.
	if (input_track->GetMediaType() == cmn::MediaType::Video &&
		GetOutputProfilesCfg()->GetDecodes().IsOnlyKeyframes() == true)
	{
		input_track->SetKeyframeDecodeOnly(IsKeyframeOnlyDecodable(_output_streams));
	}

	// Set the thread count for the decoder.
	input_track->SetThreadCount(GetOutputProfilesCfg()->GetDecodes().GetThreadCount());

	auto hwaccels_enable  = GetOutputProfilesCfg()->GetHWAccels().GetDecoder().IsEnable() ||
							GetOutputProfilesCfg()->IsHardwareAcceleration();  // Deprecated

	auto hwaccels_modules = GetOutputProfilesCfg()->GetHWAccels().GetDecoder().GetModules();

	// Get a list of available decoder candidates.
	auto candidates		  = TranscodeDecoder::GetCandidates(hwaccels_enable, hwaccels_modules, input_track);
	if (candidates == nullptr)
	{
		logte("%s Decoder candidates are not found. InputTrack(%u)", _log_prefix.CStr(), input_track->GetId());
		return false;
	}

	// Create a decoder
	auto decoder = TranscodeDecoder::Create(
		decoder_id,
		input_stream,
		input_track,
		candidates,
		bind(&TranscoderStream::OnDecodedFrame, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	if (decoder == nullptr)
	{
		return false;
	}

	SetDecoder(decoder_id, decoder);

	return true;
}

std::shared_ptr<TranscodeDecoder> TranscoderStream::GetDecoder(MediaTrackId decoder_id)
{
	std::shared_lock<std::shared_mutex> decoder_lock(_decoder_map_mutex);
	if (_decoders.find(decoder_id) == _decoders.end())
	{
		return nullptr;
	}

	return _decoders[decoder_id];
}

void TranscoderStream::SetDecoder(MediaTrackId decoder_id, std::shared_ptr<TranscodeDecoder> decoder)
{
	std::unique_lock<std::shared_mutex> decoder_lock(_decoder_map_mutex);

	_decoders[decoder_id] = decoder;
}

bool TranscoderStream::CreateEncoders(std::shared_ptr<MediaFrame> buffer)
{
	MediaTrackId track_id = buffer->GetTrackId();

	for (auto &[output_stream, output_track, encoder_id] : _composite.GetEncoderListByDecoderId(track_id))
	{
		// Probe encoder properties before full creation so we can act on them even if Configure fails.
		auto probe = TranscodeEncoder::Instantiate(
			output_track->GetCodecId(), cmn::MediaCodecModuleId::DEFAULT, *output_stream);
		const bool is_input_only = probe && probe->IsInputOnly();

		if (CreateEncoder(encoder_id, output_stream, output_track) == false)
		{
			// Non-essential track: encoder failure is not fatal, stream continues without it.
			if (output_track->IsEssentialTrack() == false)
			{
				logtw("%s Could not create encoder for non-essential track — disabled for this stream. Id(%d), OutputTrack(%d)", _log_prefix.CStr(),
					  encoder_id, output_track->GetId());
				// CodecStatus=Failed is already set. For input-only encoders, notify MediaRouter
				// to re-check stream readiness (they never push packets into the pipeline).
				if (is_input_only)
				{
					_parent->UpdateStream(output_stream);
				}
				continue;
			}

			logte("%s Could not create encoder. Id(%d)<Codec:%s,Module:%s:%d>, OutputTrack(%d)", _log_prefix.CStr(),
				  encoder_id, cmn::GetCodecIdString(output_track->GetCodecId()), cmn::GetCodecModuleIdString(output_track->GetCodecModuleId()), output_track->GetCodecDeviceId(), output_track->GetId());

#if NOTIFICATION_ENABLED
			auto output_profile_ptr = GetOutputProfileByName(output_stream->GetOutputProfileName());
			auto output_profile		= (output_profile_ptr) ? std::make_shared<cfg::vhost::app::oprf::OutputProfile>(*output_profile_ptr) : nullptr;

			TranscoderAlerts::UpdateErrorWithoutCount(
				TranscoderAlerts::ErrorType::CREATION_ERROR_ENCODER,
				output_profile,
				GetInputStream(),
				GetInputStream()->GetTrack(track_id),
				output_stream,
				output_track);
#endif

			return false;
		}

		// Input-only encoders never push packets into the pipeline, so OutboundWorkerThread
		// will never trigger IsStreamReady. Notify MediaRouter explicitly after init.
		if (is_input_only)
		{
			_parent->UpdateStream(output_stream);
		}
	}

	return true;
}

#define UPDATE_OUTPUT_TRACK_CODEC_INFO(track, encoder)                \
	do                                                                \
	{                                                                 \
		track->SetCodecModuleId(encoder->GetModuleID());              \
		track->SetCodecDeviceId(encoder->GetDeviceID());              \
		track->SetOriginBitstream(encoder->GetBitstreamFormat());     \
		if (track->GetMediaType() == cmn::MediaType::Video)           \
		{                                                             \
			track->SetColorspace(encoder->GetSupportVideoFormat());   \
		}                                                             \
		else if (track->GetMediaType() == cmn::MediaType::Audio)      \
		{                                                             \
			track->SetSampleFormat(encoder->GetSupportAudioFormat()); \
		}                                                             \
	} while (0)

bool TranscoderStream::CreateEncoder(MediaTrackId encoder_id, std::shared_ptr<info::Stream> output_stream, std::shared_ptr<MediaTrack> output_track)
{
	bool is_recreated = false;

	// Check if an identical encoder already exists.
	if (auto encoder = GetEncoder(encoder_id); encoder != nullptr)
	{
		if (encoder->GetModuleID() == cmn::MediaCodecModuleId::NVENC)
		{
			logtd("%s Identical encoder already exists. but, it will be recreated because the encoder is %s. Encoder(%d) -> OutputTrack(%d)", _log_prefix.CStr(), cmn::GetCodecModuleIdString(encoder->GetModuleID()), encoder_id, output_track->GetId());
			encoder->Stop();
			encoder.reset();

			is_recreated = true;
		}
		else
		{
			logtd("%s Identical encoder already exists; reusing existing instance. Encoder(%d) -> OutputTrack(%d)", _log_prefix.CStr(), encoder_id, output_track->GetId());

			// This track reuses an identical encoder that was previously created.
			// No new encoder is created; only encoder-related information is updated on the track
			UPDATE_OUTPUT_TRACK_CODEC_INFO(output_track, encoder);

			return true;
		}
	}

	// Get a list of available encoder candidates(modules)
	auto hwaccels_enable  = GetOutputProfilesCfg()->GetHWAccels().GetEncoder().IsEnable() ||
							GetOutputProfilesCfg()->IsHardwareAcceleration();  // Deprecated
	auto hwaccels_modules = GetOutputProfilesCfg()->GetHWAccels().GetEncoder().GetModules();

	auto candidates		  = TranscodeEncoder::GetCandidates(hwaccels_enable, hwaccels_modules, output_track);
	if (candidates == nullptr)
	{
		logte("%s Encoder candidates are not found. OutputTrack(%d)", _log_prefix.CStr(), output_track->GetId());
		return false;
	}

	// Create Encoder
	auto encoder = TranscodeEncoder::Create(
		encoder_id, output_stream, output_track, candidates,
		bind(&TranscoderStream::OnEncodedPacket, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	if (encoder == nullptr)
	{
		return false;
	}

	// Set the codec module id and device id used by the encoder to the output track.
	// Although the encoder updates the output track information when it is created, we update it again here just in case.
	UPDATE_OUTPUT_TRACK_CODEC_INFO(output_track, encoder);

	// Create a paired post-processing filter only for audio.
	// It is used to fill in any dropped audio so that the bitstream is generated continuously.
	std::shared_ptr<TranscodeFilter> post_filter = nullptr;
	if (output_track->GetMediaType() == cmn::MediaType::Audio)
	{
		post_filter = TranscodeFilter::Create(
			encoder_id, output_stream, output_track,
			bind(&TranscoderStream::OnEncoderFilterdFrame, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
		if (post_filter == nullptr)
		{
			// Stop & Release Encoder
			encoder->Stop();
			encoder.reset();

			return false;
		}
	}

	SetEncoderWithFilter(encoder_id, post_filter, encoder);

	ov::String description = ov::String::FormatString("Id(%d)<Codec:%s,Module:%s:%d>, OutputTrack(%d)",
													  encoder_id, cmn::GetCodecIdString(output_track->GetCodecId()),
													  cmn::GetCodecModuleIdString(output_track->GetCodecModuleId()),
													  output_track->GetCodecDeviceId(),
													  output_track->GetId());
	switch (output_track->GetMediaType())
	{
		case cmn::MediaType::Video:
			description += ov::String::FormatString(", Size(%s), Fps(%.2f), KetInt(%s/%.2f)",
													output_track->GetResolution().ToString().CStr(),
													output_track->GetFrameRate(),
													cmn::GetKeyFrameIntervalTypeToString(output_track->GetKeyFrameIntervalTypeByConfig()),
													output_track->GetKeyFrameIntervalByConfig());
			break;
		case cmn::MediaType::Audio:
			description += ov::String::FormatString(", SampleRate(%d), Channel(%d)", output_track->GetSampleRate(), output_track->GetChannel().GetCounts());
			break;
		default:
			break;
	}

	if (is_recreated)
	{
		logtd("%s Encoder has been recreated. %s", _log_prefix.CStr(), description.CStr());
	}
	else
	{
		logtd("%s Encoder has been created. %s", _log_prefix.CStr(), description.CStr());
	}

	return true;
}

std::optional<std::pair<std::shared_ptr<TranscodeFilter>, std::shared_ptr<TranscodeEncoder>>> TranscoderStream::GetEncoderSet(MediaTrackId encoder_id)
{
	std::shared_lock<std::shared_mutex> encoder_lock(_encoder_map_mutex);
	if (_encoders.find(encoder_id) == _encoders.end())
	{
		return std::nullopt;
	}

	return _encoders[encoder_id];
}

std::shared_ptr<TranscodeFilter> TranscoderStream::GetEncoderFilter(MediaTrackId encoder_id)
{
	std::shared_lock<std::shared_mutex> encoder_lock(_encoder_map_mutex);
	if (_encoders.find(encoder_id) == _encoders.end())
	{
		return nullptr;
	}

	return _encoders[encoder_id].first;
}

std::shared_ptr<TranscodeEncoder> TranscoderStream::GetEncoder(MediaTrackId encoder_id)
{
	std::shared_lock<std::shared_mutex> encoder_lock(_encoder_map_mutex);
	if (_encoders.find(encoder_id) == _encoders.end())
	{
		return nullptr;
	}

	return _encoders[encoder_id].second;
}

void TranscoderStream::SetEncoderWithFilter(MediaTrackId encoder_id, std::shared_ptr<TranscodeFilter> filter, std::shared_ptr<TranscodeEncoder> encoder)
{
	std::unique_lock<std::shared_mutex> encoder_lock(_encoder_map_mutex);

	_encoders[encoder_id] = std::make_pair(filter, encoder);
}

bool TranscoderStream::CreateFilters(std::shared_ptr<MediaFrame> buffer)
{
	MediaTrackId decoder_id = buffer->GetTrackId();

	for (auto &[input_stream, input_track, output_stream, output_track, filter_id] : _composite.GetFilterListByDecoderId(decoder_id))
	{
		if (!CreateFilter(filter_id, input_stream, input_track, output_stream, output_track))
		{
			logte("%s Failed to create filter. Id(%d), Decoder(%d)<Codec:%s, Module:%s:%d>, Encoder(%d)<Codec:%s, Module:%s:%d>, InputTrack(%d), OutputTrack(%d)", _log_prefix.CStr(),
				  filter_id, decoder_id, cmn::GetCodecIdString(input_track->GetCodecId()), cmn::GetCodecModuleIdString(input_track->GetCodecModuleId()), input_track->GetCodecDeviceId(),
				  output_stream->GetId(), cmn::GetCodecIdString(output_track->GetCodecId()), cmn::GetCodecModuleIdString(output_track->GetCodecModuleId()), output_track->GetCodecDeviceId(),
				  input_track->GetId(), output_track->GetId());

#if NOTIFICATION_ENABLED
			auto output_profile_ptr = GetOutputProfileByName(output_stream->GetOutputProfileName());
			auto output_profile		= (output_profile_ptr) ? std::make_shared<cfg::vhost::app::oprf::OutputProfile>(*output_profile_ptr) : nullptr;

			TranscoderAlerts::UpdateErrorWithoutCount(
				TranscoderAlerts::ErrorType::CREATION_ERROR_FILTER,
				output_profile,
				input_stream,
				input_track,
				output_stream,
				output_track);
#endif
			return false;
		}
	}

	return true;
}

bool TranscoderStream::CreateFilter(MediaTrackId filter_id, std::shared_ptr<info::Stream> input_stream, std::shared_ptr<MediaTrack> input_track, std::shared_ptr<info::Stream> output_stream, std::shared_ptr<MediaTrack> output_track)
{
	if (GetFilter(filter_id) != nullptr)
	{
		logtt("%s filters that have already been created. Filter(%d)", _log_prefix.CStr(), filter_id);
		return true;
	}

	// If the encoder paired with this filter failed to initialize (e.g. non-essential Whisper encoder),
	// skip filter creation to avoid wasting CPU/memory on resampling that will never be consumed.
	auto encoder_id = _composite.GetEncoderIdByFilterId(filter_id);
	if (encoder_id.has_value() && GetEncoder(encoder_id.value()) == nullptr)
	{
		logtd("%s Skip filter creation because paired encoder(%d) does not exist. Filter(%d)",
			  _log_prefix.CStr(), encoder_id.value(), filter_id);
		return true;
	}

	auto filter = TranscodeFilter::Create(filter_id, input_stream, input_track, output_stream, output_track, bind(&TranscoderStream::OnFilteredFrame, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	if (filter == nullptr)
	{
		return false;
	}

	SetFilter(filter_id, filter);

	logtd("%s Filter has been created. Id(%d), %s", _log_prefix.CStr(), filter_id, filter->GetDescription().CStr());

	return true;
}

std::shared_ptr<TranscodeFilter> TranscoderStream::GetFilter(MediaTrackId filter_id)
{
	std::shared_lock<std::shared_mutex> lock(_filter_map_mutex);
	if (_filters.find(filter_id) == _filters.end())
	{
		return nullptr;
	}

	return _filters[filter_id];
}

void TranscoderStream::SetFilter(MediaTrackId filter_id, std::shared_ptr<TranscodeFilter> filter)
{
	std::unique_lock<std::shared_mutex> lock(_filter_map_mutex);

	_filters[filter_id] = filter;
}

// Function called when codec information is extracted or changed from the decoder
void TranscoderStream::ChangeOutputFormat(std::shared_ptr<MediaFrame> buffer)
{
	logtt("%s Changed output format. InputTrack(%u)", _log_prefix.CStr(), buffer->GetTrackId());

	if (buffer == nullptr)
	{
		logte("%s Invalid media buffer", _log_prefix.CStr());
		return;
	}

	// Update Track of Input Stream
	UpdateInputTrack(buffer);

	// Update Track of Output Stream
	UpdateOutputTrack(buffer);

	// Create an encoder. If there is an existing encoder, reuse it
	if (CreateEncoders(buffer) == false)
	{
		SetState(State::ERROR);

		return;
	}

	// Create an filter. If there is an existing filter, reuse it
	if (CreateFilters(buffer) == false)
	{
		SetState(State::ERROR);

		return;
	}
}

// Information of the input track is updated by the decoded frame
void TranscoderStream::UpdateInputTrack(std::shared_ptr<MediaFrame> buffer)
{
	MediaTrackId track_id = buffer->GetTrackId();

	logtt("%s Updated input track. InputTrack(%u)", _log_prefix.CStr(), track_id);

	auto input_track = _input_stream->GetTrack(track_id);
	if (input_track == nullptr)
	{
		logte("Could not found output track. InputTrack(%u)", track_id);
		return;
	}

	switch (input_track->GetMediaType())
	{
		case cmn::MediaType::Video: {
			input_track->SetResolution(buffer->GetWidth(), buffer->GetHeight());
			input_track->SetColorspace(buffer->GetFormat<cmn::VideoPixelFormatId>());
		}
		break;
		case cmn::MediaType::Audio: {
			input_track->SetSampleRate(buffer->GetSampleRate());
			input_track->SetChannel(buffer->GetChannels());
			input_track->SetSampleFormat(buffer->GetFormat<cmn::AudioSample::Format>());
		}
		break;
		default: {
			logtt("%s Unsupported media type. InputTrack(%d)", _log_prefix.CStr(), track_id);
		}
		break;
	}
}

void TranscoderStream::UpdateOutputTrack(std::shared_ptr<MediaFrame> buffer)
{
	MediaTrackId input_track_id = buffer->GetTrackId();

	for (auto &[input_stream, input_track, output_stream, output_track] : _composite.GetInputOutputListByDecoderId(input_track_id))
	{
		UNUSED_VARIABLE(input_stream);
		UNUSED_VARIABLE(output_stream);

		logtt("%s Updated output track. InputTrack(%u) -> OutputTrack(%u)", _log_prefix.CStr(), input_track->GetId(), output_track->GetId());

		if (output_track->IsBypass())
		{
			logtw("%s Invalid output track. Bypass track cannot be connected to decoder. OutputTrack(%d)", _log_prefix.CStr(), output_track->GetId());
			continue;
		}

		UpdateOutputTrackByDecodedFrame(output_track, input_track, buffer);
	}
}

void TranscoderStream::UpdatePassthroughOutputTracks(const std::shared_ptr<info::Stream> &stream)
{
	for (auto it : stream->GetTracks())
	{
		for (auto &[input_stream, input_track, output_stream, output_track] : _composite.GetBypassOutputListByInputTrackId(it.first))
		{
			UNUSED_VARIABLE(input_stream);
			UNUSED_VARIABLE(input_track);
			UNUSED_VARIABLE(output_stream);

			if (output_track->IsBypass() == false)
			{
				logtw("%s Invalid output track. Only bypass track can be updated. OutputTrack(%d)", _log_prefix.CStr(), output_track->GetId());
				continue;
			}

			UpdateOutputTrackPassthrough(output_track, input_track);
		}
	}
}

void TranscoderStream::ProcessPacket(const std::shared_ptr<MediaPacket> &packet)
{
	if (!packet)
	{
		return;
	}

	if (_input_stream->GetMsid() != packet->GetMsid() && packet->GetMediaType() != cmn::MediaType::Data)
	{
		return;
	}

	BypassPacket(packet);

	DecodePacket(packet);
}

void TranscoderStream::BypassPacket(const std::shared_ptr<MediaPacket> &packet)
{
	for (auto &[input_stream, input_track, output_stream, output_track] : _composite.GetBypassOutputListByInputTrackId(packet->GetTrackId()))
	{
		(void)input_stream;

		logtt("%s Bypass packet. InputTrack(%d) -> OutputTrack(%d)", _log_prefix.CStr(), input_track->GetId(), output_track->GetId());

		// Clone the packet and send it to the output stream.
		std::shared_ptr<MediaPacket> clone = nullptr;

		if (packet->GetBitstreamFormat() == cmn::BitstreamFormat::OVEN_EVENT)
		{
			auto event_packet = std::dynamic_pointer_cast<MediaEvent>(packet);
			if (event_packet == nullptr)
			{
				logte("%s Invalid event packet. InputTrack(%d)", _log_prefix.CStr(), input_track->GetId());
				continue;
			}
			clone = event_packet->Clone();
		}
		else
		{
			clone = packet->ClonePacket();
		}

		double scale = input_track->GetTimeBase().GetExpr() / output_track->GetTimeBase().GetExpr();
		clone->SetPts(static_cast<int64_t>((double)clone->GetPts() * scale));
		clone->SetDts(static_cast<int64_t>((double)clone->GetDts() * scale));
		clone->SetTrackId(output_track->GetId());

		SendFrame(output_stream, std::move(clone));
	}
}

void TranscoderStream::DecodePacket(const std::shared_ptr<MediaPacket> &packet)
{
	auto decoder_id = _composite.GetDecoderIdByInputTrackId(packet->GetTrackId());
	if (decoder_id == std::nullopt)
	{
		return;
	}

	std::shared_lock<std::shared_mutex> pipeline_lock(_pipeline_mutex, std::try_to_lock);
	if (!pipeline_lock.owns_lock())
	{
		logtt("%s Failed to acquire pipeline lock. drop the frame. decoderId(%u) ", _log_prefix.CStr(), decoder_id.value());
		return;
	}

	auto decoder = GetDecoder(decoder_id.value());
	if (!decoder)
	{
		logte("%s Could not found decoder. Decoder(%d)", _log_prefix.CStr(), decoder_id.value());
		return;
	}

	decoder->SendBuffer(packet);
}

void TranscoderStream::OnDecodedFrame(TranscodeResult result, MediaTrackId decoder_id, std::shared_ptr<MediaFrame> decoded_frame)
{
	switch (result)
	{
		case TranscodeResult::DataError: {
#if NOTIFICATION_ENABLED
			auto input_track = GetInputTrack(decoder_id);
			if (input_track == nullptr)
			{
				logte("%s Could not found input track. Decoder(%d)", _log_prefix.CStr(), decoder_id);
				return;
			}

			TranscoderAlerts::UpdateErrorCountIfNeeded(TranscoderAlerts::ErrorType::DECODING_ERROR, _input_stream, input_track, nullptr, nullptr);
#endif
		}
		break;
		case TranscodeResult::NoData: {
#if FILLER_ENABLED
			if (!decoded_frame)
			{
				return;
			}
			///////////////////////////////////////////////////////////////////
			// Generate a filler frame (Part 1). * Using previously decoded frame
			///////////////////////////////////////////////////////////////////
			// - It is mainly used in Persistent Stream.
			// - When the input stream is switched, decoding fails until a KeyFrame is received.
			//   If the keyframe interval is longer than the buffered length of the player, buffering occurs in the player.
			//   Therefore, the number of frames in which decoding fails is replaced with the last decoded frame and used as a filler frame.
			auto last_frame = GetLastDecodedFrame(decoder_id);
			if (last_frame == nullptr)
			{
				break;
			}

			auto input_track = GetInputTrack(decoder_id);
			if (!input_track)
			{
				logte("%s Could not found input track. Decoder(%d)", _log_prefix.CStr(), decoder_id);
				return;
			}

			auto input_track_of_filter = GetInputTrackOfFilter(decoder_id);
			if (!input_track_of_filter)
			{
				break;
			}

			double input_expr  = input_track->GetTimeBase().GetExpr();
			double filter_expr = input_track_of_filter->GetTimeBase().GetExpr();

			if (last_frame->GetPts() * filter_expr >= decoded_frame->GetPts() * input_expr)
			{
				break;
			}

			// The decoded frame pts should be modified to fit the Timebase of the filter input.
			last_frame->SetPts((int64_t)((double)decoded_frame->GetPts() * input_expr / filter_expr));

			// Record the timestamp of the last decoded frame. managed by microseconds.
			{
				std::unique_lock<std::shared_mutex> lock(_last_decoded_frame_mutex);
				_last_decoded_frame_pts[decoder_id] = last_frame->GetPts() * filter_expr * 1000000.0;
			}

			// logtt("%s Create filler frame because there is no decoding frame. Type(%s), Decoder(%u), FillerFrames(%d)"
			// 	, _log_prefix.CStr(), cmn::GetMediaTypeString(input_track->GetMediaType()), decoder_id, 1);

			// Send Temporary Frame to Filter
			SpreadToFilters(decoder_id, last_frame);
#endif	// End of Filler Frame Generation
		}
		break;

		// It indicates output format is changed
		case TranscodeResult::FormatChanged: {
			if (!decoded_frame)
			{
				return;
			}

			// Re-create filter and encoder using the format of decoded frame
			ChangeOutputFormat(decoded_frame);

#if FILLER_ENABLED
			///////////////////////////////////////////////////////////////////
			// Generate a filler frame (Part 2). * Using latest decoded frame
			///////////////////////////////////////////////////////////////////
			// - It is mainly used in Schedule stream.
			// - When the input stream is changed, an empty section occurs in sequential frames. There is a problem with the A/V sync in the player.
			//   If there is a section where the frame is empty, may be out of sync in the player.
			//   Therefore, the filler frame is inserted in the hole of the sequential frame.
			auto input_track = GetInputTrack(decoder_id);
			if (!input_track)
			{
				logte("Could not found input track. Decoder(%d)", decoder_id);
				return;
			}

			int64_t last_decoded_frame_time_us	   = 0;
			int64_t last_decoded_frame_duration_us = 0;
			bool _has_last_decoded_frame_pts	   = false;
			{
				std::shared_lock<std::shared_mutex> lock(_last_decoded_frame_mutex);
				if (_last_decoded_frame_pts.find(decoder_id) != _last_decoded_frame_pts.end())
				{
					last_decoded_frame_time_us	   = _last_decoded_frame_pts[decoder_id];
					last_decoded_frame_duration_us = _last_decoded_frame_duration[decoder_id];
					_has_last_decoded_frame_pts	   = true;
				}
			}
			if (_has_last_decoded_frame_pts)
			{
				// Decoded frame PTS to microseconds
				int64_t curr_decoded_frame_time_us = (int64_t)((double)decoded_frame->GetPts() * input_track->GetTimeBase().GetExpr() * 1000000);

				// Calculate the time difference between the last decoded frame and the current decoded frame.
				int64_t hole_time_us			   = curr_decoded_frame_time_us - (last_decoded_frame_time_us + last_decoded_frame_duration_us);
				int64_t hole_time_tb			   = (int64_t)(floor((double)hole_time_us / input_track->GetTimeBase().GetExpr() / 1000000));

				int64_t duration_per_frame		   = -1LL;
				switch (input_track->GetMediaType())
				{
					case cmn::MediaType::Video:
						if (input_track->GetFrameRate() > 0 && input_track->GetTimeBase().GetTimescale() > 0)
						{
							duration_per_frame = static_cast<int64_t>(input_track->GetTimeBase().GetTimescale() / input_track->GetFrameRate());
						}
						break;
					case cmn::MediaType::Audio:
						duration_per_frame = decoded_frame->GetNbSamples();
						break;
					default:
						break;
				}

				// If the time difference is greater than 0, it means that there is a hole between with the last frame and the current frame.
				if (duration_per_frame > 0 && hole_time_tb >= duration_per_frame)
				{
					int64_t start_pts	   = decoded_frame->GetPts() - hole_time_tb;
					int64_t end_pts		   = decoded_frame->GetPts();
					int32_t needed_frames  = hole_time_tb / duration_per_frame;
					int32_t created_filler = 0;

					logtt("%s Generate filler frame because time diffrence from last frame. Type(%s), needed(%d), last_pts(%" PRId64 "), curr_pts(%" PRId64 "), hole_time(%" PRId64 "), hole_time_tb(%" PRId64 "), frame_duration(%" PRId64 "), start_pts(%" PRId64 "), end_pts(%" PRId64 ")",
						  _log_prefix.CStr(), cmn::GetMediaTypeString(input_track->GetMediaType()), needed_frames, last_decoded_frame_time_us, curr_decoded_frame_time_us, hole_time_us, hole_time_tb, duration_per_frame, start_pts, end_pts);

					for (int64_t filler_pts = start_pts; filler_pts < end_pts; filler_pts += duration_per_frame)
					{
						std::shared_ptr<MediaFrame> clone_frame = decoded_frame->CloneFrame(true);
						if (!clone_frame)
						{
							continue;
						}
						clone_frame->SetPts(filler_pts);
						clone_frame->SetDuration(duration_per_frame);

						if (input_track->GetMediaType() == cmn::MediaType::Audio)
						{
							if (end_pts - filler_pts < duration_per_frame)
							{
								int32_t remain_samples = end_pts - filler_pts;
								clone_frame->SetDuration(remain_samples);

								// There is no problem making the Samples smaller since the cloned frame is larger than the remaining samples.
								// To do this properly, It need to reallocate audio buffers of MediaFrame.
								clone_frame->SetNbSamples(remain_samples);
							}
							clone_frame->FillZeroData();
						}

						SpreadToFilters(decoder_id, clone_frame);

						// Prevent infinite loop
						if (created_filler++ >= MAX_FILLER_FRAMES)
						{
							break;
						}
					}
				}
				else
				{
					if (duration_per_frame <= 0)
					{
						logtw("%s Could not create filler frame. track(%d), timebase(%s), framerate(%.2f), samples(%d)",
							  _log_prefix.CStr(), input_track->GetId(), input_track->GetTimeBase().GetStringExpr().CStr(), input_track->GetFrameRate(), decoded_frame->GetNbSamples());
					}
				}
			}
#endif	// End of Filler Frame Generation

			[[fallthrough]];
		}

		case TranscodeResult::DataReady: {
			if (!decoded_frame)
			{
				return;
			}

			// The last decoded frame is kept and used as a filling frame in the blank section.
			SetLastDecodedFrame(decoder_id, decoded_frame);

			// Send Decoded Frame to Filter
			SpreadToFilters(decoder_id, decoded_frame);
		}
		break;
		default:
			// An error occurred
			// There is no frame to process
			break;
	}
}

void TranscoderStream::SetLastDecodedFrame(MediaTrackId decoder_id, std::shared_ptr<MediaFrame> &decoded_frame)
{
	auto input_track  = GetInputTrack(decoder_id);

	auto scale_factor = input_track->GetTimeBase().GetExpr() * 1000000.0;
	auto pts		  = static_cast<int64_t>(decoded_frame->GetPts() * scale_factor);
	auto duration	  = static_cast<int64_t>(decoded_frame->GetDuration() * scale_factor);
	auto cloned		  = decoded_frame->CloneFrame();

	std::unique_lock<std::shared_mutex> lock(_last_decoded_frame_mutex);
	_last_decoded_frame_pts[decoder_id]		 = pts;
	_last_decoded_frame_duration[decoder_id] = duration;
	_last_decoded_frames[decoder_id]		 = std::move(cloned);
}

std::shared_ptr<MediaFrame> TranscoderStream::GetLastDecodedFrame(MediaTrackId decoder_id)
{
	std::shared_lock<std::shared_mutex> lock(_last_decoded_frame_mutex);
	if (_last_decoded_frames.find(decoder_id) != _last_decoded_frames.end())
	{
		auto frame = _last_decoded_frames[decoder_id]->CloneFrame();
		frame->SetTrackId(decoder_id);
		return frame;
	}

	return nullptr;
}

void TranscoderStream::RemoveLastDecodedFrame()
{
	std::unique_lock<std::shared_mutex> lock(_last_decoded_frame_mutex);
	_last_decoded_frames.clear();
	_last_decoded_frame_pts.clear();
	_last_decoded_frame_duration.clear();
}

std::shared_ptr<MediaTrack> TranscoderStream::GetInputTrackOfFilter(MediaTrackId decoder_id)
{
	auto filter_ids = _composite.GetFilterIdsByDecoderId(decoder_id);
	if (filter_ids.size() == 0)
	{
		return nullptr;
	}

	auto filter = GetFilter(filter_ids[0]);
	if (filter == nullptr)
	{
		return nullptr;
	}

	return filter->GetInputTrack();
}

TranscodeResult TranscoderStream::FilterFrame(MediaTrackId filter_id, std::shared_ptr<MediaFrame> decoded_frame)
{
	// Shared lock: allows concurrent callbacks but does not block during pipeline updates.
	std::shared_lock<std::shared_mutex> pipeline_lock(_pipeline_mutex, std::try_to_lock);
	if (!pipeline_lock.owns_lock())
	{
		logtt("%s Failed to acquire pipeline lock. FilterId(%d)", _log_prefix.CStr(), filter_id);
		return TranscodeResult::DataReady;
	}

	auto filter = GetFilter(filter_id);
	if (filter == nullptr)
	{
		return TranscodeResult::NoData;
	}

	if (filter->SendBuffer(std::move(decoded_frame)) == false)
	{
		return TranscodeResult::DataError;
	}

	return TranscodeResult::DataReady;
}

void TranscoderStream::OnFilteredFrame(TranscodeResult result, MediaTrackId filter_id, std::shared_ptr<MediaFrame> filtered_frame)
{
	if (result != TranscodeResult::DataReady || !filtered_frame)
	{
#if NOTIFICATION_ENABLED
		auto opt = _composite.GetInputOutputByFilterId(filter_id);
		if (!opt.has_value())
		{
			return;
		}

		auto &[input_stream, input_track, output_stream, output_track] = opt.value();
		TranscoderAlerts::UpdateErrorCountIfNeeded(TranscoderAlerts::ErrorType::FILTERING_ERROR, input_stream, input_track, output_stream, output_track);
#endif
		return;
	}

	filtered_frame->SetTrackId(filter_id);

	EncoderFilterFrame(std::move(filtered_frame));
}

TranscodeResult TranscoderStream::EncoderFilterFrame(std::shared_ptr<MediaFrame> frame)
{
	auto filter_id	= frame->GetTrackId();

	// Get Encoder ID from Filter ID
	auto encoder_id = _composite.GetEncoderIdByFilterId(filter_id);
	if (encoder_id == std::nullopt)
	{
		return TranscodeResult::NoData;
	}

	// Shared lock: allows concurrent callbacks but does not block during pipeline updates.
	std::shared_lock<std::shared_mutex> pipeline_lock(_pipeline_mutex, std::try_to_lock);
	if (!pipeline_lock.owns_lock())
	{
		logtt("%s Failed to acquire pipeline lock. drop the frame. filterId(%u) ", _log_prefix.CStr(), filter_id);
		return TranscodeResult::NoData;
	}

	// If the encoder has a pre-encode filter, it is passed to the filter.
	auto encoder_filter = GetEncoderFilter(encoder_id.value());
	if (encoder_filter != nullptr)
	{
		encoder_filter->SendBuffer(std::move(frame));
		return TranscodeResult::DataReady;
	}

	pipeline_lock.unlock();

	// If there is no post-filter, it is sent directly to the encoder.
	OnEncoderFilterdFrame(TranscodeResult::DataReady, encoder_id.value(), std::move(frame));

	return TranscodeResult::DataReady;
}

void TranscoderStream::OnEncoderFilterdFrame(TranscodeResult result, MediaTrackId encoder_id, std::shared_ptr<MediaFrame> filtered_frame)
{
	if (result != TranscodeResult::DataReady || !filtered_frame)
	{
#if NOTIFICATION_ENABLED
		auto opts = _composite.GetInputOutputByEncoderId(encoder_id);
		if (!opts.has_value())
		{
			return;
		}

		auto &[input_stream, input_track, output_stream, output_track] = opts.value();
		TranscoderAlerts::UpdateErrorCountIfNeeded(TranscoderAlerts::ErrorType::FILTERING_ERROR, input_stream, input_track, output_stream, output_track);
#endif
		return;
	}

	filtered_frame->SetTrackId(encoder_id);

	EncodeFrame(std::move(filtered_frame));
}

TranscodeResult TranscoderStream::EncodeFrame(std::shared_ptr<const MediaFrame> frame)
{
	auto encoder_id = frame->GetTrackId();

	std::shared_lock<std::shared_mutex> pipeline_lock(_pipeline_mutex, std::try_to_lock);
	if (!pipeline_lock.owns_lock())
	{
		logtt("%s Failed to acquire pipeline lock. drop the frame. encoderId(%u) ", _log_prefix.CStr(), encoder_id);

		return TranscodeResult::NoData;
	}

	// Get Encoder
	auto encoder = GetEncoder(encoder_id);
	if (encoder == nullptr)
	{
		return TranscodeResult::NoData;
	}

	encoder->SendBuffer(std::move(frame));

	return TranscodeResult::DataReady;
}

void TranscoderStream::OnEncodedPacket(TranscodeResult result, MediaTrackId encoder_id, std::shared_ptr<MediaPacket> encoded_packet)
{
	if (result != TranscodeResult::DataReady)
	{
#if NOTIFICATION_ENABLED
		auto opts = _composite.GetInputOutputByEncoderId(encoder_id);
		if (!opts.has_value())
		{
			return;
		}

		auto &[input_stream, input_track, output_stream, output_track] = opts.value();
		TranscoderAlerts::UpdateErrorCountIfNeeded(TranscoderAlerts::ErrorType::ENCODING_ERROR, _input_stream, input_track, output_stream, output_track);
#endif
		return;
	}

	if (!encoded_packet)
	{
		return;
	}

	auto output_list   = _composite.GetOutputListByEncoderId(encoder_id);
	int32_t used_count = output_list.size();
	for (auto &[output_stream, output_track] : output_list)
	{
		std::shared_ptr<MediaPacket> packet = nullptr;

		// If the packet is used in multiple tracks, it is cloned.
		packet								= (used_count == 1) ? encoded_packet : encoded_packet->ClonePacket();
		if (!packet)
		{
			logte("%s Could not clone packet. Encoder(%d)", _log_prefix.CStr(), encoder_id);
			continue;
		}

		packet->SetTrackId(output_track->GetId());

		// Send the packet to MediaRouter
		SendFrame(output_stream, std::move(packet));

		used_count--;
	}
}

void TranscoderStream::UpdateMsidOfOutputStreams(uint32_t msid)
{
	std::shared_lock<std::shared_mutex> lock(_output_stream_mutex);
	for (auto &[output_stream_name, output_stream] : _output_streams)
	{
		UNUSED_VARIABLE(output_stream_name)

		output_stream->SetMsid(msid);
	}
}

void TranscoderStream::SendFrame(std::shared_ptr<info::Stream> &stream, std::shared_ptr<MediaPacket> packet)
{
	packet->SetMsid(stream->GetMsid());

	if (!(_parent->SendFrame(stream, std::move(packet))))
	{
		logtw("%s Could not send frame to mediarouter. Stream(%s(%u)), OutputTrack(%u)",
			  _log_prefix.CStr(), stream->GetName().CStr(), stream->GetId(), packet->GetTrackId());
	}
}

void TranscoderStream::SpreadToFilters(MediaTrackId decoder_id, std::shared_ptr<MediaFrame> frame)
{
	for (auto &filter_id : _composite.GetFilterIdsByDecoderId(decoder_id))
	{
		// Skip clone entirely if the filter does not exist (e.g. paired encoder failed to init).
		if (GetFilter(filter_id) == nullptr)
		{
			continue;
		}

		auto frame_clone = frame->CloneFrame();
		if (!frame_clone)
		{
			logte("%s Failed to clone frame", _log_prefix.CStr());

			continue;
		}

		FilterFrame(filter_id, std::move(frame_clone));
	}
}

void TranscoderStream::NotifyCreateStreams()
{
	std::shared_lock<std::shared_mutex> lock(_output_stream_mutex);
	for (auto &[output_stream_name, output_stream] : _output_streams)
	{
		UNUSED_VARIABLE(output_stream_name)

		if (_parent->CreateStream(output_stream) == false)
		{
			logtw("%s Could not create stream. %s(%u)", _log_prefix.CStr(), output_stream->GetUri().CStr(), output_stream->GetId());
		}
	}
}

void TranscoderStream::NotifyDeleteStreams()
{
	std::shared_lock<std::shared_mutex> lock(_output_stream_mutex);
	for (auto &[output_stream_name, output_stream] : _output_streams)
	{
		UNUSED_VARIABLE(output_stream_name)

		if (_parent->DeleteStream(output_stream) == false)
		{
			logtw("%s Could not delete stream. %s(%u)", _log_prefix.CStr(), output_stream->GetUri().CStr(), output_stream->GetId());
		}
	}
}

void TranscoderStream::NotifyUpdateStreams()
{
	std::shared_lock<std::shared_mutex> lock(_output_stream_mutex);
	for (auto &[output_stream_name, output_stream] : _output_streams)
	{
		UNUSED_VARIABLE(output_stream_name)

		if (_parent->UpdateStream(output_stream) == false)
		{
			logtw("%s Could not update stream. %s(%u)", _log_prefix.CStr(), output_stream->GetUri().CStr(), output_stream->GetId());
		}
	}
}
