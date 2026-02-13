//==============================================================================
//
//  TranscoderStream
//
//  Created by Keukhan
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#include "transcoder_stream.h"

#include "config/config_manager.h"
#include "modules/transcode_webhook/transcode_webhook.h"
#include "orchestrator/orchestrator.h"
#include <monitoring/monitoring.h>

#include "transcoder_application.h"
#include "transcoder_private.h"
#include "transcoder_modules.h"

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
	_log_prefix = ov::String::FormatString("[%s(%u)]", _input_stream->GetUri().CStr(), _input_stream->GetId());

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
	if(GetState() != State::PREPARING)
	{
		logte("%s stream has already prepared", _log_prefix.CStr());		
		return false;
	}

	// Start async preparation to avoid blocking
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

	// Transcoder Webhook
	_output_profiles_cfg = RequestWebhook();
	if (_output_profiles_cfg == nullptr)
	{
		logtw("%s There is no output profiles", _log_prefix.CStr());
		SetState(State::ERROR);
		_prepare_thread_running = false;
		return;
	}

	// Create Ouput Streams & Notify to create a new stream on the media router.
	if (!StartInternal())
	{
		logte("%s Failed to create the stream", _log_prefix.CStr());
		SetState(State::ERROR);
		_prepare_thread_running = false;
		return;
	}

	// Create Decoders
	if(!PrepareInternal())
	{
		logte("%s Failed to prepare the stream", _log_prefix.CStr());
		SetState(State::ERROR);
		_prepare_thread_running = false;
		return;
	}

	logti("%s stream has been prepared", _log_prefix.CStr());
	SetState(State::STARTED);

	_prepare_thread_running = false;
}

bool TranscoderStream::Update(const std::shared_ptr<info::Stream> &stream)
{
	if(GetState() != State::STARTED)
	{
		logtt("%s stream is not started", _log_prefix.CStr());
		return false;
	}

	return UpdateInternal(stream);
}

bool TranscoderStream::Stop()
{
	if(GetState() == State::STOPPED)
	{
		return true;
	}

	logtt("%s Wait for stream thread to terminated", _log_prefix.CStr());

	// Wait for prepare thread to finish if it's running
	if (_prepare_thread.joinable())
	{
		logtt("%s Waiting for prepare thread to complete", _log_prefix.CStr());
		_prepare_thread.join();
		logtt("%s Prepare thread joined", _log_prefix.CStr());
	}

	RemoveDecoders();
	RemoveFilters();
	RemoveEncoders();

	// Delete all composite of components
	_link_input_to_outputs.clear();
	
	_link_input_to_decoder.clear();
	_link_decoder_to_filters.clear();
	_link_filter_to_encoder.clear();
	_link_encoder_to_outputs.clear();

	// Delete all last decoded frame information
	_last_decoded_frame_pts.clear();
	_last_decoded_frames.clear();

	// Notify to delete the stream created on the MediaRouter
	NotifyDeleteStreams();

	// Delete all output streams information
	_output_streams.clear();

	SetState(State::STOPPED);

	logti("%s stream has been stopped", _log_prefix.CStr());

	return true;
}

const cfg::vhost::app::oprf::OutputProfiles *TranscoderStream::RequestWebhook()
{
	// Measure response time of transcode webhook
	ov::StopWatch response_time;
	response_time.Start();

	TranscodeWebhook webhook(_application_info);
	auto policy = webhook.RequestOutputProfiles(*_input_stream, _remote_output_profiles);

	if (policy == TranscodeWebhook::Policy::DeleteStream)
	{
		logtw("%s Delete a stream by transcode webhook", _log_prefix.CStr());
		ocst::Orchestrator::GetInstance()->TerminateStream(_application_info.GetVHostAppName(), _input_stream->GetName());
		return nullptr;
	}
	else if (policy == TranscodeWebhook::Policy::CreateStream)
	{
		Json::StreamWriterBuilder builder;
		builder["indentation"] = "";
		builder["emitUTF8"]	   = true;

		logti("%s Using external output profiles by webhook. Response time: %lld ms", _log_prefix.CStr(), response_time.Elapsed());
		logti("%s OutputProfile: %s", _log_prefix.CStr(), Json::writeString(builder, _remote_output_profiles.ToJson()).c_str());
		return &_remote_output_profiles;
	}
	else if (policy == TranscodeWebhook::Policy::UseLocalProfiles)
	{
		Json::StreamWriterBuilder builder;
		builder["indentation"] = "";
		builder["emitUTF8"]	   = true;

		logti("%s Using local output profiles by webhook. Response time: %lld ms", _log_prefix.CStr(), response_time.Elapsed());
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
	if (BuildComposite() == 0)
	{
		logte("%s Failed to create components", _log_prefix.CStr());

		return false;
	}

	if (CreateDecoders() == false)
	{
		logte("%s Failed to create decoders", _log_prefix.CStr());

		return false;
	}

	StoreTracks(_input_stream);

	return true;
}

bool TranscoderStream::UpdateInternal(const std::shared_ptr<info::Stream> &stream)
{
	// Check if smooth stream transition is possible
	// [Rule]
	// - The number of tracks per media type should not exceed one.
	// - The input track should not change.
	_is_updating = true;

	if (CanSeamlessTransition(stream) == true)
	{
		logtt("%s This stream support seamless transitions", _log_prefix.CStr());
		FlushBuffers();

		RemoveDecoders();
		RemoveFilters();
		RemoveSpecificEncoders();

		CreateDecoders();
	}
	else
	{
		logtw("%s This stream does not support seamless transitions. Renewing all", _log_prefix.CStr());

		FlushBuffers();

		RemoveDecoders();
		RemoveFilters();
		RemoveEncoders();

		CreateDecoders();

		UpdateMsidOfOutputStreams(stream->GetMsid());
		NotifyUpdateStreams();

		StoreTracks(stream);
	}

	_is_updating = false;

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
	std::unique_lock<std::shared_mutex> encoder_lock(_encoder_map_mutex);
	auto encoders = _encoders;
	encoder_lock.unlock();

	for (auto &[id, object] : encoders)
	{
		auto filter = object.first;
		auto encoder = object.second;

		if (encoder->GetRefTrack()->GetMediaType() != cmn::MediaType::Video)
		{
			continue;
		}
		if (encoder->GetRefTrack()->GetCodecModuleId() != cmn::MediaCodecModuleId::NVENC)
		{
			continue;
		}

		_encoders.erase(id);

		if (filter != nullptr)
		{
			filter->Stop();
			filter.reset();
		}

		if (encoder != nullptr)
		{
			encoder->Stop();
			encoder.reset();
		}
	}
}

void TranscoderStream::RemoveEncoders()
{
	std::unique_lock<std::shared_mutex> encoder_lock(_encoder_map_mutex);
	
	auto encoders = _encoders;
	_encoders.clear();
	
	encoder_lock.unlock();

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
	if (_input_stream)
	{
		return _input_stream->GetTrack(track_id);
	}

	return nullptr;
}

std::shared_ptr<MediaTrack> TranscoderStream::GetInputTrackByOutputTrackId(MediaTrackId track_id)
{
	for (const auto &[key, composite] : _composite_map)
	{
		auto output_tracks = composite->GetOutputTracks();
		for (const auto &[output_stream, output_track] : output_tracks)
		{
			if (output_track->GetId() == track_id)
			{
				return composite->GetInputTrack();
			}
		}
	}

	return nullptr;
}

std::shared_ptr<info::Stream> TranscoderStream::GetInputStream()
{
	return _input_stream;
}

std::shared_ptr<info::Stream> TranscoderStream::GetOutputStreamByTrackId(MediaTrackId output_track_id)
{
	for (auto &iter : _output_streams)
	{
		auto stream = iter.second;
		if (stream->GetTrack(output_track_id) != nullptr)
		{
			return stream;
		}
	}

	return nullptr;
}

bool TranscoderStream::CanSeamlessTransition(const std::shared_ptr<info::Stream> &input_stream)
{
	auto new_tracks = input_stream->GetTracks();

	// Check if the number and type of original tracks are different.
	if(CompareTracksForSeamlessTransition(new_tracks, GetStoredTracks()) == false)
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
		if (_initial_media_packet_buffer.IsEmpty() == false)
		{
			SendBufferedPackets();
		}

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
	logtt("SendBufferedPackets - BufferSize (%u)", _initial_media_packet_buffer.Size());
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

		AddComposite("dynamic", _input_stream, input_track, output_stream, output_track);
	}

	// Add to Output Stream List. The key is the output stream name.
	_output_streams.insert(std::make_pair(output_stream->GetName(), output_stream)); 

	logti("%s Output stream(dynamic) has been created. [%s/%s(%u)]",
		  _log_prefix.CStr(),
		  _application_info.GetVHostAppName().CStr(), output_stream->GetName().CStr(), output_stream->GetId());

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

		_output_streams.insert(std::make_pair(output_stream->GetName(), output_stream));

		logti("%s Output stream has been created. [%s/%s(%u)]", _log_prefix.CStr(), _application_info.GetVHostAppName().CStr(), output_stream->GetName().CStr(), output_stream->GetId());
	}

	auto cfg_media_option_subtitles = GetOutputProfilesCfg()->GetMediaOptions().GetSubtitle();
	
	cfg::vhost::app::oprf::OutputProfile cfg_new_output_profile;
	if (cfg_media_option_subtitles.IsEnabled())
	{
		auto new_output_profile_name = ov::String::FormatString("TranscriptionProcess_%s", _input_stream->GetName().CStr());
		cfg_new_output_profile.SetInternal(true);
		cfg_new_output_profile.SetName(new_output_profile_name);
		cfg_new_output_profile.SetOutputStreamName(new_output_profile_name);

		cfg::vhost::app::oprf::Encodes encodes;
		int i = 0;
		for (const auto &subtitle_rendition : cfg_media_option_subtitles.GetRenditions())
		{
			bool enabled = false;
			auto cfg_transcription = subtitle_rendition.GetTranscription(&enabled);
			if (enabled)
			{
				// Make SpeechToTextProfile
				auto name = ov::String::FormatString("SpeechToText_%d", i);
				auto engine = cfg_transcription.GetEngine();
				auto model = cfg_transcription.GetModel();
				auto model_path = ov::GetFilePath(model, cfg::ConfigManager::GetInstance()->GetConfigPath());
				// check if the model file exists
				if (access(model_path.CStr(), F_OK) != 0)
				{
					logte("The transcription model file does not exist. model(%s)", model_path.CStr());
					continue;
				}

				auto input_audio_track = _input_stream->GetMediaTrackByOrder(cmn::MediaType::Audio, cfg_transcription.GetAudioIndexHint());
				auto output_subtitle_track = _input_stream->GetTrackByLabel(subtitle_rendition.GetLabel());

				if (input_audio_track == nullptr || output_subtitle_track == nullptr)
				{
					logte("Could not find input audio track or output subtitle track for transcription. AudioIndexHint(%d), SubtitleLabel(%s)", cfg_transcription.GetAudioIndexHint(), subtitle_rendition.GetLabel().CStr());
					continue;
				}

				cfg::vhost::app::oprf::SpeechToTextProfile speech_to_text_profile(name,
																				engine,
																				model_path,
																				input_audio_track->GetId(),
																				output_subtitle_track->GetId());
				speech_to_text_profile.SetSourceLanguage(cfg_transcription.GetSourceLanguage());
				speech_to_text_profile.SetTranslation(cfg_transcription.GetTranslation());
				speech_to_text_profile.SetOutputTrackLabel(subtitle_rendition.GetLabel());

				encodes.AddSpeechToTextProfiles(speech_to_text_profile);
			}

			cfg_new_output_profile.SetEncodes(encodes);

			i++;
		}

		if (cfg_new_output_profile.GetEncodes().GetSpeechToTextProfileList().size() > 0)
		{
			auto output_stream = CreateOutputStream(cfg_new_output_profile);
			if (output_stream == nullptr)
			{
				logte("%s Could not create output stream for transcription. name:%s", _log_prefix.CStr(), cfg_new_output_profile.GetName().CStr());

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
				_output_streams.insert(std::make_pair(output_stream->GetName(), output_stream));

				logti("%s Output stream(transcription) has been created. [%s/%s(%u)]", _log_prefix.CStr(), _application_info.GetVHostAppName().CStr(), output_stream->GetName().CStr(), output_stream->GetId());
			}
		}
	}

	return _output_streams.size();
}

std::shared_ptr<info::Stream> TranscoderStream::CreateOutputStream(const cfg::vhost::app::oprf::OutputProfile &cfg_output_profile)
{
	if (cfg_output_profile.GetOutputStreamName().IsEmpty())
	{
		return nullptr;
	}

	auto output_stream = std::make_shared<info::Stream>(_application_info, StreamSourceType::Transcoder);
	if (output_stream == nullptr)
	{
		return nullptr;
	}

	// It helps modules to recognize origin stream from provider
	output_stream->LinkInputStream(_input_stream);
	output_stream->SetMediaSource(_input_stream->GetUUID());
	output_stream->SetOutputProfileName(cfg_output_profile.GetName());

	// Create a output stream name.
	auto name = cfg_output_profile.GetOutputStreamName();
	if (::strstr(name.CStr(), "${OriginStreamName}") != nullptr)
	{
		name = name.Replace("${OriginStreamName}", _input_stream->GetName());
	}
	output_stream->SetName(name);

	// Create a Output Track
	for (auto &[input_track_id, input_track] : _input_stream->GetTracks())
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

					AddComposite(ProfileToSerialize(input_track_id, profile), _input_stream, input_track, output_stream, output_track);
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

					AddComposite(ProfileToSerialize(input_track_id, profile), _input_stream, input_track, output_stream, output_track);
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

					AddComposite(ProfileToSerialize(input_track_id, profile), _input_stream, input_track, output_stream, output_track);
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
					AddComposite(ProfileToSerialize(input_track_id, profile), _input_stream, input_track, output_stream, output_track);
				}
			}
			break;
			
			// If there is a data type track in the input stream, it must be created equally in all output streams.
			case cmn::MediaType::Data: 
			case cmn::MediaType::Subtitle:{
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

					AddComposite(ProfileToSerialize(input_track_id), _input_stream, input_track, output_stream, output_track);
			}
			break;
			default: {
				logte("[%s] Unsupported media type of input track. type(%d)", _log_prefix.CStr(), input_track->GetMediaType());
				continue;
			}
		}
	}

	// Playlist
	bool is_parsed = false;
	auto cfg_playlists = cfg_output_profile.GetPlaylists(&is_parsed);
	if (is_parsed)
	{
		for (const auto &cfg_playlist : cfg_playlists)
		{
			auto playlist_info = cfg_playlist.GetPlaylistInfo();

			// Create renditions with RenditionTemplate
			auto rendition_templates = cfg_playlist.GetRenditionTemplates();
			auto tracks = output_stream->GetTracks();

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
							
							auto rendition = std::make_shared<info::Rendition>(rendition_name, video_track->GetVariantName(), audio_track->GetVariantName());
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
						auto rendition = std::make_shared<info::Rendition>(rendition_name, video_track->GetVariantName(), "");
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

						auto rendition = std::make_shared<info::Rendition>(rendition_name, "", audio_track->GetVariantName());
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

ov::String TranscoderStream::MakeRenditionName(const ov::String &name_template, const std::shared_ptr<info::Playlist> &playlist_info, const std::shared_ptr<MediaTrack> &video_track, const std::shared_ptr<MediaTrack> &audio_track)
{
	ov::String rendition_name = name_template;

	if (video_track != nullptr)
	{
		rendition_name = rendition_name.Replace("${Width}", ov::String::FormatString("%d", video_track->GetWidth()).CStr());
		rendition_name = rendition_name.Replace("${Height}", ov::String::FormatString("%d", video_track->GetHeight()).CStr());
		rendition_name = rendition_name.Replace("${Bitrate}", ov::String::FormatString("%d", video_track->GetBitrate()).CStr());
		rendition_name = rendition_name.Replace("${Framerate}", ov::String::FormatString("%d", video_track->GetFrameRate()).CStr());
	}

	if (audio_track != nullptr)
	{
		rendition_name = rendition_name.Replace("${Samplerate}", ov::String::FormatString("%d", audio_track->GetSampleRate()).CStr());
		rendition_name = rendition_name.Replace("${Channel}", ov::String::FormatString("%u", audio_track->GetChannel().GetCounts()).CStr());
	}

	// Check if the rendition name is duplicated
	uint32_t rendition_index = 0;
	ov::String unique_rendition_name = rendition_name;
	while (playlist_info->GetRendition(unique_rendition_name) != nullptr)
	{
		unique_rendition_name = ov::String::FormatString("%s_%d", rendition_name.CStr(), ++rendition_index);
	}

	return unique_rendition_name;
}

size_t TranscoderStream::BuildComposite()
{
	for (auto &[key, composite] : _composite_map)
	{
		UNUSED_VARIABLE(key)
		auto input_track 	= composite->GetInputTrack();

		auto input_track_id = input_track->GetId();
		auto decoder_id 	= input_track->GetId();

		auto filter_id 		= composite->GetId();
		auto encoder_id 	= composite->GetId();

		for (auto &[output_stream, output_track] : composite->GetOutputTracks())
		{
			auto output_stream_and_track = std::make_pair(output_stream, output_track->GetId());

			// Bypass Flow: InputTrack -> OutputTrack
			if (output_track->IsBypass() == true)
			{
				_link_input_to_outputs[input_track_id].push_back(output_stream_and_track);
			}
			// Transcoding Flow: InputTrack -> Decoder -> Filter -> Encoder -> OutputTrack
			else
			{
				// Decoding: InputTrack(1) -> Decoder(1)
				_link_input_to_decoder[input_track_id] = decoder_id;

				// Rescale/Resample Filtering: Decoder(1) -> Filter (N)
				auto &filter_ids = _link_decoder_to_filters[decoder_id];
				if (std::find(filter_ids.begin(), filter_ids.end(), filter_id) == filter_ids.end())
				{
					filter_ids.push_back(filter_id);
				}

				// Encoding: Filter(1) -> Encoder (1)
				_link_filter_to_encoder[filter_id] = encoder_id;

				// Flushing: Encoder(1) -> OutputTrack (N)
				_link_encoder_to_outputs[encoder_id].push_back(output_stream_and_track);
			}
		}
	}

	logtt("%s", GetInfoStringComposite().CStr());

	return _link_input_to_outputs.size() + _link_input_to_decoder.size();
}

// LOG for DEBUG
ov::String TranscoderStream::GetInfoStringComposite()
{
	ov::String debug_log = "Component composition list\n";

	for (auto &[input_track_id, input_track] : _input_stream->GetTracks())
	{
		bool matched = false;
		debug_log.AppendFormat("* Input(%s/%u) : %s\n",
							   _input_stream->GetName().CStr(),
							   input_track_id,
							   input_track->GetInfoString().CStr());

		// Bypass Stream
		if (_link_input_to_outputs.find(input_track_id) != _link_input_to_outputs.end())
		{
			matched = true;
			auto &output_tracks = _link_input_to_outputs[input_track_id];
			for (auto &[stream, track_id] : output_tracks)
			{
				auto output_track = stream->GetTrack(track_id);

				debug_log.AppendFormat("  + Output(%s/%d) : Passthrough, %s\n",
									   stream->GetName().CStr(),
									   track_id,
									   output_track->GetInfoString().CStr());
			}
		}

		// Transcoding Stream
		if (_link_input_to_decoder.find(input_track_id) != _link_input_to_decoder.end())
		{
			matched = true;

			auto decoder_id = _link_input_to_decoder[input_track_id];
			debug_log.AppendFormat("  + Decoder(%u)\n", decoder_id);

			if (_link_decoder_to_filters.find(decoder_id) != _link_decoder_to_filters.end())
			{
				auto &filter_ids = _link_decoder_to_filters[decoder_id];
				for (auto &filter_id : filter_ids)
				{
					debug_log.AppendFormat("    + Filter(%u)\n", filter_id);

					if (_link_filter_to_encoder.find(filter_id) != _link_filter_to_encoder.end())
					{
						auto encoder_id = _link_filter_to_encoder[filter_id];
						debug_log.AppendFormat("      + Encoder(%d)\n", encoder_id);

						if (_link_encoder_to_outputs.find(encoder_id) != _link_encoder_to_outputs.end())
						{
							auto &output_tracks = _link_encoder_to_outputs[encoder_id];
							for (auto &[stream, track_id] : output_tracks)
							{
								auto output_track = stream->GetTrack(track_id);

								debug_log.AppendFormat("        + Output(%s/%u) : %s\n",
													   stream->GetName().CStr(),
													   track_id,
													   output_track->GetInfoString().CStr());
							}
						}
					}
				}
			}
		}

		if(matched == false)
		{
			debug_log.AppendFormat("  + (No output)\n");
		}
	}

	return debug_log;
}

void TranscoderStream::AddComposite(
	ov::String serialized_profile,
	std::shared_ptr<info::Stream> input_stream,	std::shared_ptr<MediaTrack> input_track,
	std::shared_ptr<info::Stream> output_stream, std::shared_ptr<MediaTrack> output_track)
{
	auto key = std::make_pair(serialized_profile, input_track->GetMediaType());

	if (_composite_map.find(key) == _composite_map.end())
	{
		auto composite = std::make_shared<CompositeContext>(_last_composite_id++);
		composite->SetInput(input_stream, input_track);

		_composite_map[key] = composite;
	}

	_composite_map[key]->AddOutput(output_stream, output_track);
}

bool TranscoderStream::CreateDecoders()
{
	for (auto &[input_track_id, decoder_id] : _link_input_to_decoder)
	{
		// Get Input Track
		auto it = GetInputStream()->GetTracks().find(input_track_id);
		if (it == GetInputStream()->GetTracks().end())
		{
			continue;
		}
		auto &input_track = it->second;

		// Create Decoder
		if (CreateDecoder(decoder_id, GetInputStream(), input_track) == false)
		{
			logte("%s Failed to create decoder. Id(%d)<Codec(%s), Module(%s), Device(%u)>, InputTrack(%d)",
				  _log_prefix.CStr(), decoder_id, cmn::GetCodecIdString(input_track->GetCodecId()),
				  cmn::GetCodecModuleIdString(input_track->GetCodecModuleId()), input_track->GetCodecDeviceId(), input_track->GetId());

#if NOTIFICATION_ENABLED
			TranscoderAlerts::UpdateErrorWithoutCount(
				TranscoderAlerts::ErrorType::CREATION_ERROR_DECODER,
				nullptr,
				_input_stream,
				input_track,
				nullptr,
				nullptr);
#endif

			return false;
		}

		logti("%s Decoder has been created. Id(%d)<Codec(%s), Module(%s), Device(%u)>, InputTrack(%d)",
			  _log_prefix.CStr(), decoder_id, cmn::GetCodecIdString(input_track->GetCodecId()),
			  cmn::GetCodecModuleIdString(input_track->GetCodecModuleId()), input_track->GetCodecDeviceId(), input_track->GetId());
	}

	return true;
}

bool TranscoderStream::CreateDecoder(MediaTrackId decoder_id, std::shared_ptr<info::Stream> input_stream, std::shared_ptr<MediaTrack> input_track)
{
	if(GetDecoder(decoder_id) != nullptr)
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

	auto hwaccels_enable = GetOutputProfilesCfg()->GetHWAccels().GetDecoder().IsEnable() ||
						   GetOutputProfilesCfg()->IsHardwareAcceleration();  // Deprecated

	auto hwaccels_modules = GetOutputProfilesCfg()->GetHWAccels().GetDecoder().GetModules();

	// Get a list of available decoder candidates.
	auto candidates = TranscodeDecoder::GetCandidates(hwaccels_enable, hwaccels_modules, input_track);
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

	for (auto &[key, composite] : _composite_map)
	{
		UNUSED_VARIABLE(key)

		if ((uint32_t)track_id != composite->GetInputTrack()->GetId())
		{
			continue;
		}

		auto encoder_id = composite->GetId();
		auto output_tracks_it = _link_encoder_to_outputs.find(encoder_id);
		if (output_tracks_it == _link_encoder_to_outputs.end())
		{
			continue;
		}

		auto output_tracks = output_tracks_it->second;
		if (output_tracks.size() == 0)
		{
			continue;
		}

		for (auto &[output_stream, output_track_id] : output_tracks)
		{
			auto output_track = output_stream->GetTrack(output_track_id);

			if (CreateEncoder(encoder_id, output_stream, output_track) == false)
			{
				logte("%s Could not create encoder. Id(%d)<Codec:%s,Module:%s:%d>, OutputTrack(%d)", _log_prefix.CStr(),
					  encoder_id, cmn::GetCodecIdString(output_track->GetCodecId()), cmn::GetCodecModuleIdString(output_track->GetCodecModuleId()), output_track->GetCodecDeviceId(), output_track->GetId());

#if NOTIFICATION_ENABLED
				auto output_profile_ptr = GetOutputProfileByName(output_stream->GetOutputProfileName());
				auto output_profile		= (output_profile_ptr) ? std::make_shared<cfg::vhost::app::oprf::OutputProfile>(*output_profile_ptr) : nullptr;

				TranscoderAlerts::UpdateErrorWithoutCount(
					TranscoderAlerts::ErrorType::CREATION_ERROR_ENCODER,
					output_profile,
					_input_stream,
					_input_stream->GetTrack(track_id),
					output_stream,
					output_track);
#endif
				return false;
			}

			logti("%s Encoder has been created. Id(%d)<Codec:%s,Module:%s:%d>, OutputTrack(%d)", _log_prefix.CStr(),
				  encoder_id, cmn::GetCodecIdString(output_track->GetCodecId()), cmn::GetCodecModuleIdString(output_track->GetCodecModuleId()), output_track->GetCodecDeviceId(), output_track->GetId());
		}
	}

	return true;
}

#define UPDATE_OUTPUT_TRACK_CODEC_INFO(track, encoder)                      \
	do                                                                      \
	{                                                                       \
		track->SetCodecModuleId(encoder->GetModuleID());                    \
		track->SetCodecDeviceId(encoder->GetDeviceID());                    \
		track->SetOriginBitstream(encoder->GetBitstreamFormat());           \
		if (track->GetMediaType() == cmn::MediaType::Video)                 \
		{                                                                   \
			track->SetColorspace(encoder->GetSupportVideoFormat());         \
		}                                                                   \
		else if (track->GetMediaType() == cmn::MediaType::Audio)            \
		{                                                                   \
			track->GetSample().SetFormat(encoder->GetSupportAudioFormat()); \
		}                                                                   \
	} while (0)

bool TranscoderStream::CreateEncoder(MediaTrackId encoder_id, std::shared_ptr<info::Stream> output_stream, std::shared_ptr<MediaTrack> output_track)
{
	// Check if an identical encoder already exists.
	if (auto encoder = GetEncoder(encoder_id); encoder != nullptr)
	{
		logtt("%s Identical encoder already exists; reusing existing instance. Encoder(%d) -> OutputTrack(%d)", _log_prefix.CStr(), encoder_id, output_track->GetId());

		// This track reuses an identical encoder that was previously created.
		// No new encoder is created; only encoder-related information is updated on the track
		UPDATE_OUTPUT_TRACK_CODEC_INFO(output_track, encoder);

		return true;
	}

	// Get a list of available encoder candidates(modules)
	auto hwaccels_enable = GetOutputProfilesCfg()->GetHWAccels().GetEncoder().IsEnable() ||
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
			bind(&TranscoderStream::OnPostFilteredFrame, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
		if (post_filter == nullptr)
		{
			// Stop & Release Encoder
			encoder->Stop();
			encoder.reset();

			return false;
		}
	}

	SetEncoderWithFilter(encoder_id, post_filter, encoder);

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

std::shared_ptr<TranscodeFilter> TranscoderStream::GetPostFilter(MediaTrackId encoder_id)
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

	// 1. Get Decoder -> Filter List
	auto decoder_to_filters_it = _link_decoder_to_filters.find(decoder_id);
	if (decoder_to_filters_it == _link_decoder_to_filters.end())
	{ 
		logtw("%s Could not found filter list related to decoder", _log_prefix.CStr());

		return false;
	}

	// 2. Get Output Track of Encoders
	auto filter_ids = decoder_to_filters_it->second;
	for (auto &filter_id : filter_ids)
	{
		MediaTrackId encoder_id = _link_filter_to_encoder[filter_id];
		auto encoder = GetEncoder(encoder_id);
		if(encoder == nullptr)
		{
			logtw("%s Failed to create filter. could not found encoder. Encoder(%d), Filter(%d)", _log_prefix.CStr(), encoder_id, filter_id);

			continue;
		}

		auto decoder = GetDecoder(decoder_id);
		if(decoder == nullptr)
		{
			logtw("%s Failed to create filter. could not found decoder. Decoder(%d), Filter(%d)", _log_prefix.CStr(), decoder_id, filter_id);

			continue;
		}

		auto input_track = decoder->GetRefTrack();
		auto output_track = encoder->GetRefTrack();
		if(input_track == nullptr || output_track == nullptr)
		{
			logtw("%s Failed to create filter. could not found input or output track. Decoder(%d), Encoder(%d), Filter(%d)", _log_prefix.CStr(), decoder_id, encoder_id, filter_id);

			continue;
		}

		if (CreateFilter(filter_id, input_track, output_track) == false)
		{
			logte("%s Failed to create filter. InputTrack(%d), OutputTrack(%d), Filter(%d), Decoder(%d) <Codec:%s, Module:%s:%d>, Encoder(%d) <Codec:%s, Module:%s:%d>", _log_prefix.CStr(), 
				  input_track->GetId(),
				  output_track->GetId(),
				  filter_id,
				  decoder_id, cmn::GetCodecIdString(output_track->GetCodecId()), cmn::GetCodecModuleIdString(output_track->GetCodecModuleId()), output_track->GetCodecDeviceId(),
				  encoder_id, cmn::GetCodecIdString(output_track->GetCodecId()), cmn::GetCodecModuleIdString(output_track->GetCodecModuleId()), output_track->GetCodecDeviceId());

#if NOTIFICATION_ENABLED
			auto output_profile_ptr = GetOutputProfileByName(GetOutputStreamByTrackId(output_track->GetId())->GetOutputProfileName());
			auto output_profile		= (output_profile_ptr) ? std::make_shared<cfg::vhost::app::oprf::OutputProfile>(*output_profile_ptr) : nullptr;

			TranscoderAlerts::UpdateErrorWithoutCount(
				TranscoderAlerts::ErrorType::CREATION_ERROR_FILTER,
				output_profile,
				_input_stream,
				input_track,
				GetOutputStreamByTrackId(output_track->GetId()),
				output_track);
#endif
			return false;
		}

		logti("%s Filter has been created. Id(%d), %s", _log_prefix.CStr(), filter_id, GetFilter(filter_id)->GetDescription().CStr());
	}

	return true;
}

bool TranscoderStream::CreateFilter(MediaTrackId filter_id, std::shared_ptr<MediaTrack> input_track, std::shared_ptr<MediaTrack> output_track)
{
	if(GetFilter(filter_id) != nullptr)
	{
		logtt("%s filters that have already been created. Filter(%d)", _log_prefix.CStr(), filter_id);
		return true;
	}

	auto input_stream = GetInputStream();
	if(input_stream == nullptr)
	{
		logte("%s Could not found input stream", _log_prefix.CStr());
		return false;
	}

	auto output_stream = GetOutputStreamByTrackId(output_track->GetId());
	if(output_stream == nullptr)
	{
		logte("%s Could not found output stream", _log_prefix.CStr());
		return false;
	}

	auto filter = TranscodeFilter::Create(filter_id, input_stream, input_track, output_stream, output_track, bind(&TranscoderStream::OnPreFilteredFrame, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	if (filter == nullptr)
	{
		return false;
	}

	SetFilter(filter_id, filter);

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

	std::unique_lock<std::shared_mutex> lock(_format_change_mutex);

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
	if(CreateEncoders(buffer) == false)
	{
		SetState(State::ERROR);

		return;
	}

	// Create an filter. If there is an existing filter, reuse it
	if(CreateFilters(buffer) == false)
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

	auto &input_track = _input_stream->GetTrack(track_id);
	if (input_track == nullptr)
	{
		logte("Could not found output track. InputTrack(%u)", track_id);
		return;
	}

	switch (input_track->GetMediaType())
	{
		case cmn::MediaType::Video: 
		{
			input_track->SetWidth(buffer->GetWidth());
			input_track->SetHeight(buffer->GetHeight());
			input_track->SetColorspace(buffer->GetFormat<cmn::VideoPixelFormatId>());
		}
		break;
		case cmn::MediaType::Audio: 
		{
			input_track->SetSampleRate(buffer->GetSampleRate());
			input_track->SetChannel(buffer->GetChannels());
			input_track->GetSample().SetFormat(buffer->GetFormat<cmn::AudioSample::Format>());
		}
		break;
		default: 
		{
			logtt("%s Unsupported media type. InputTrack(%d)", _log_prefix.CStr(), track_id);
		}
		break;
	}
}

// Update Output Track
void TranscoderStream::UpdateOutputTrack(std::shared_ptr<MediaFrame> buffer)
{
	MediaTrackId input_track_id = buffer->GetTrackId();

	for (auto &[key, composite] : _composite_map)
	{
		UNUSED_VARIABLE(key);

		// Get Related Composite
		if (input_track_id != composite->GetInputTrack()->GetId())
		{
			continue;
		}

		auto &input_track = _input_stream->GetTrack(input_track_id);


		for (auto &[output_stream, output_track] : composite->GetOutputTracks())
		{
			UNUSED_VARIABLE(output_stream)

			logtt("%s Updated output track. InputTrack(%u) -> OutputTrack(%u)", _log_prefix.CStr(), input_track_id, output_track->GetId());

			// Case of Passthrough
			if (output_track->IsBypass() == true)
			{
				UpdateOutputTrackPassthrough(output_track, buffer);
			}
			// Case Of Transcode
			else
			{
				UpdateOutputTrackByDecodedFrame(output_track, input_track, buffer);
			}
		}
	}
}

void TranscoderStream::ProcessPacket(const std::shared_ptr<MediaPacket> &packet)
{
	if (packet == nullptr)
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
	MediaTrackId input_track_id = packet->GetTrackId();

	auto it = _link_input_to_outputs.find(input_track_id);
	if (it == _link_input_to_outputs.end())
	{
		return;
	}

	auto input_track = _input_stream->GetTrack(input_track_id);
	if (input_track == nullptr)
	{
		logte("%s Could not found input track. InputTrack(%d)", _log_prefix.CStr(), input_track_id);
		return;
	}

	for (auto &[output_stream, output_track_id] : it->second)
	{
		auto output_track = output_stream->GetTrack(output_track_id);
		if (output_track == nullptr)
		{
			logte("%s Could not found output track. OutputTrack(%d)", _log_prefix.CStr(), output_track_id);
			continue;
		}

		// Clone the packet and send it to the output stream.
		std::shared_ptr<MediaPacket> clone = nullptr;
		
		if (packet->GetBitstreamFormat() == cmn::BitstreamFormat::OVEN_EVENT)
		{
			auto event_packet = std::dynamic_pointer_cast<MediaEvent>(packet);
			if (event_packet == nullptr)
			{
				logte("%s Invalid event packet. InputTrack(%d)", _log_prefix.CStr(), input_track_id);
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
		clone->SetTrackId(output_track_id);

		SendFrame(output_stream, std::move(clone));
	}
}

void TranscoderStream::DecodePacket(const std::shared_ptr<MediaPacket> &packet)
{
	MediaTrackId input_track_id = packet->GetTrackId();

	auto it = _link_input_to_decoder.find(input_track_id);
	if (it == _link_input_to_decoder.end())
	{
		return;
	}

	auto decoder_id = it->second;
	auto decoder = GetDecoder(decoder_id);
	if (decoder == nullptr)
	{
		logte("%s Could not found decoder. Decoder(%d)", _log_prefix.CStr(), decoder_id);
		return;
	}
	decoder->SendBuffer(std::move(packet));
}

void TranscoderStream::OnDecodedFrame(TranscodeResult result, MediaTrackId decoder_id, std::shared_ptr<MediaFrame> decoded_frame)
{
	if (_is_updating == true)
	{
		logtt("%s Current state is updating format. suspend decoded frame. Decoder(%d)", _log_prefix.CStr(), decoder_id);
		return;
	}

	switch (result)
	{
		case TranscodeResult::DataError:
		{
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
			if (decoded_frame == nullptr)
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
			if (input_track == nullptr)
			{
				logte("%s Could not found input track. Decoder(%d)", _log_prefix.CStr(), decoder_id);
				return;
			}

			auto input_track_of_filter = GetInputTrackOfFilter(decoder_id);
			if (input_track_of_filter == nullptr)
			{
				break;
			}

			double input_expr = input_track->GetTimeBase().GetExpr();
			double filter_expr = input_track_of_filter->GetTimeBase().GetExpr();

			if(last_frame->GetPts()*filter_expr >= decoded_frame->GetPts()*input_expr)
			{
				break;
			}

			// The decoded frame pts should be modified to fit the Timebase of the filter input.
			last_frame->SetPts((int64_t)((double)decoded_frame->GetPts() * input_expr / filter_expr));

			// Record the timestamp of the last decoded frame. managed by microseconds.
			_last_decoded_frame_pts[decoder_id] = last_frame->GetPts() * filter_expr * 1000000.0;

			// logtt("%s Create filler frame because there is no decoding frame. Type(%s), Decoder(%u), FillerFrames(%d)"
			// 	, _log_prefix.CStr(), cmn::GetMediaTypeString(input_track->GetMediaType()), decoder_id, 1);

			// Send Temporary Frame to Filter
			SpreadToFilters(decoder_id, last_frame);
#endif // End of Filler Frame Generation
		}
		break;

		// It indicates output format is changed
		case TranscodeResult::FormatChanged: {
			if(decoded_frame == nullptr)
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

			if (_last_decoded_frame_pts.find(decoder_id) != _last_decoded_frame_pts.end())
			{
				auto last_decoded_frame_time_us = _last_decoded_frame_pts[decoder_id];
				auto last_decoded_frame_duration_us = _last_decoded_frame_duration[decoder_id];

				// Decoded frame PTS to microseconds
				int64_t curr_decoded_frame_time_us = (int64_t)((double)decoded_frame->GetPts() * input_track->GetTimeBase().GetExpr() * 1000000);

				// Calculate the time difference between the last decoded frame and the current decoded frame.
				int64_t hole_time_us = curr_decoded_frame_time_us - (last_decoded_frame_time_us + last_decoded_frame_duration_us);
				int64_t hole_time_tb = (int64_t)(floor((double)hole_time_us / input_track->GetTimeBase().GetExpr() / 1000000));

				int64_t duration_per_frame = -1LL;
				switch (input_track->GetMediaType())
				{
					case cmn::MediaType::Video:
						if(input_track->GetFrameRate() > 0 && input_track->GetTimeBase().GetTimescale() > 0)
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
					int64_t start_pts = decoded_frame->GetPts() - hole_time_tb;
					int64_t end_pts = decoded_frame->GetPts();
					int32_t needed_frames = hole_time_tb / duration_per_frame;
					int32_t created_filler = 0;

					logtt("%s Generate filler frame because time diffrence from last frame. Type(%s), needed(%d), last_pts(%lld), curr_pts(%lld), hole_time(%lld), hole_time_tb(%lld), frame_duration(%lld), start_pts(%lld), end_pts(%lld)",
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
						if(created_filler++ >= MAX_FILLER_FRAMES)
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
#endif // End of Filler Frame Generation	

			[[fallthrough]];
		}
		
		case TranscodeResult::DataReady: {
			if(decoded_frame == nullptr)
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
	auto input_track = GetInputTrack(decoder_id);

	auto scale_factor = input_track->GetTimeBase().GetExpr() * 1000000.0;
	_last_decoded_frame_pts[decoder_id] = static_cast<int64_t>(decoded_frame->GetPts() * scale_factor);
	_last_decoded_frame_duration[decoder_id] = static_cast<int64_t>(decoded_frame->GetDuration() * scale_factor);

	_last_decoded_frames[decoder_id] = decoded_frame->CloneFrame();
}

std::shared_ptr<MediaFrame> TranscoderStream::GetLastDecodedFrame(MediaTrackId decoder_id)
{
	if (_last_decoded_frames.find(decoder_id) != _last_decoded_frames.end())
	{
		auto frame = _last_decoded_frames[decoder_id]->CloneFrame();
		frame->SetTrackId(decoder_id);
		return frame;
	}

	return nullptr;
}

std::shared_ptr<MediaTrack> TranscoderStream::GetInputTrackOfFilter(MediaTrackId decoder_id)
{
	auto decoder_to_filter_map_it = _link_decoder_to_filters.find(decoder_id);
	if (decoder_to_filter_map_it == _link_decoder_to_filters.end())
	{
		return nullptr;
	}

	auto filter_ids = decoder_to_filter_map_it->second;
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

TranscodeResult TranscoderStream::PreFilterFrame(MediaTrackId filter_id, std::shared_ptr<MediaFrame> decoded_frame)
{
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

void TranscoderStream::OnPreFilteredFrame(TranscodeResult result, MediaTrackId filter_id, std::shared_ptr<MediaFrame> filtered_frame)
{
	if(_is_updating == true)
	{
		logtt("%s Current state is updating format. suspend filted frame. Filter(%d)", _log_prefix.CStr(), filter_id);
		return;
	}

	if (result != TranscodeResult::DataReady || filtered_frame == nullptr)
	{
#if NOTIFICATION_ENABLED
		auto it = _link_encoder_to_outputs.find(filter_id);
		if (it == _link_encoder_to_outputs.end() || it->second.size() == 0)
		{
			return;
		}

		auto output_stream	 = it->second.front().first;
		if (output_stream == nullptr)
		{
			return;
		}

		auto output_track_id = it->second.front().second;
		auto output_track = output_stream->GetTrack(output_track_id);
		if (output_track == nullptr)
		{
			return;
		}

		auto input_track = GetInputTrackByOutputTrackId(output_track_id);
		if (input_track == nullptr)
		{
			return;
		}

		TranscoderAlerts::UpdateErrorCountIfNeeded(TranscoderAlerts::ErrorType::FILTERING_ERROR, _input_stream, input_track, output_stream, output_track);
#endif
		return;
	}


	filtered_frame->SetTrackId(filter_id);

	PostFilterFrame(std::move(filtered_frame));
}

TranscodeResult TranscoderStream::PostFilterFrame(std::shared_ptr<MediaFrame> frame)
{
	auto filter_id = frame->GetTrackId();

	// Get Encoder ID form Filter ID
	auto filter_to_encoder_it = _link_filter_to_encoder.find(filter_id);
	if (filter_to_encoder_it == _link_filter_to_encoder.end())
	{
		return TranscodeResult::NoData;
	}
	auto encoder_id = filter_to_encoder_it->second;

	// If the encoder has a pre-encode filter, it is passed to the filter.
	auto post_filter = GetPostFilter(encoder_id);
	if (post_filter != nullptr)
	{
		post_filter->SendBuffer(std::move(frame));

		return TranscodeResult::DataReady;
	}
	
	OnPostFilteredFrame(TranscodeResult::DataReady, encoder_id, std::move(frame));
	
	return TranscodeResult::DataReady;
}

void TranscoderStream::OnPostFilteredFrame(TranscodeResult result, MediaTrackId encoder_id, std::shared_ptr<MediaFrame> filtered_frame)
{
	if (_is_updating == true)
	{
		logtt("%s Current state is updating format. suspend filtered frame. Encoder(%d)", _log_prefix.CStr(), encoder_id);
		return;
	}

	if (result != TranscodeResult::DataReady || filtered_frame == nullptr)
	{
#if NOTIFICATION_ENABLED
		auto it = _link_encoder_to_outputs.find(encoder_id);
		if (it == _link_encoder_to_outputs.end() || it->second.size() == 0)
		{
			return;
		}

		auto output_stream	 = it->second.front().first;
		if( output_stream == nullptr)
		{
			return;
		}

		auto output_track_id = it->second.front().second;
		auto output_track	 = output_stream->GetTrack(output_track_id);
		if (output_track == nullptr)
		{
			return;
		}

		auto input_track = GetInputTrackByOutputTrackId(output_track_id);
		if (input_track == nullptr)
		{
			return;
		}

		TranscoderAlerts::UpdateErrorCountIfNeeded(TranscoderAlerts::ErrorType::FILTERING_ERROR, _input_stream, input_track, output_stream, output_track);
#endif
		return;
	}

	filtered_frame->SetTrackId(encoder_id);

	EncodeFrame(std::move(filtered_frame));
}

TranscodeResult TranscoderStream::EncodeFrame(std::shared_ptr<const MediaFrame> frame)
{
	auto encoder_id = frame->GetTrackId();

	// Get Encoder
	auto encoder = GetEncoder(encoder_id);
	if (encoder == nullptr)
	{
		return TranscodeResult::NoData;
	}

	encoder->SendBuffer(std::move(frame));
	return TranscodeResult::DataReady;
}

// Callback is called from the encoder for packets that have been encoded.
void TranscoderStream::OnEncodedPacket(TranscodeResult result, MediaTrackId encoder_id, std::shared_ptr<MediaPacket> encoded_packet)
{
	if (result != TranscodeResult::DataReady)
	{
#if NOTIFICATION_ENABLED		
		auto it = _link_encoder_to_outputs.find(encoder_id);
		if (it == _link_encoder_to_outputs.end() || it->second.size() == 0)
		{
			return;
		}
		auto output_stream	 = it->second.front().first;
		if (output_stream == nullptr)
		{
			return;
		}

		auto output_track_id = it->second.front().second;
		auto output_track = output_stream->GetTrack(output_track_id);
		if (output_track == nullptr)
		{
			return;
		}

		auto input_track = GetInputTrackByOutputTrackId(output_track_id);
		if (input_track == nullptr)
		{
			return;
		}

		TranscoderAlerts::UpdateErrorCountIfNeeded(TranscoderAlerts::ErrorType::ENCODING_ERROR, _input_stream, input_track, output_stream, output_track);			
#endif
		return;
	}

	if (encoded_packet == nullptr)
	{
		return;
	}

	auto it = _link_encoder_to_outputs.find(encoder_id);
	if (it == _link_encoder_to_outputs.end())
	{
		return;
	}
	auto output_tracks = it->second;


	// The encoded packet is used in multiple tracks. 
	int32_t used_count = output_tracks.size();
	for (auto &[output_stream, output_track_id] : output_tracks)
	{
		std::shared_ptr<MediaPacket> packet = nullptr;
		
		// If the packet is used in multiple tracks, it is cloned. 
		packet = (used_count == 1) ? encoded_packet : encoded_packet->ClonePacket();
		if (packet == nullptr)
		{
			logte("%s Could not clone packet. Encoder(%d)", _log_prefix.CStr(), encoder_id);
			continue;
		}

		packet->SetTrackId(output_track_id);

		// Send the packet to MediaRouter
		SendFrame(output_stream, std::move(packet));

		used_count--;
	}
}

std::vector<std::shared_ptr<MediaTrack>> TranscoderStream::GetOutputTracksByEncoderId(MediaTrackId encoder_id)
{
	std::vector<std::shared_ptr<MediaTrack>> output_tracks;

	auto it = _link_encoder_to_outputs.find(encoder_id);
	if (it == _link_encoder_to_outputs.end())
	{
		return output_tracks;
	}

	for (auto &[output_stream, output_track_id] : it->second)
	{
		auto output_track = output_stream->GetTrack(output_track_id);
		if (output_track != nullptr)
		{
			output_tracks.push_back(output_track);
		}
	}

	return output_tracks;
}

void TranscoderStream::UpdateMsidOfOutputStreams(uint32_t msid)
{
	// Update info::Stream().msid of all output streams
	for (auto &[output_stream_name, output_stream] : _output_streams)
	{
		UNUSED_VARIABLE(output_stream_name)
		
		output_stream->SetMsid(msid);
	}
}

void TranscoderStream::SendFrame(std::shared_ptr<info::Stream> &stream, std::shared_ptr<MediaPacket> packet)
{
	packet->SetMsid(stream->GetMsid());

	// Send the packet to MediaRouter
	bool ret = _parent->SendFrame(stream, std::move(packet));
	if (ret == false)
	{
		logtw("%s Could not send frame to mediarouter. Stream(%s(%u)), OutputTrack(%u)",
			  _log_prefix.CStr(), stream->GetName().CStr(), stream->GetId(), packet->GetTrackId());
	}
}

void TranscoderStream::SpreadToFilters(MediaTrackId decoder_id, std::shared_ptr<MediaFrame> frame)
{
	auto filters = _link_decoder_to_filters.find(decoder_id);
	if (filters == _link_decoder_to_filters.end())
	{
		logtw("%s Could not found filter", _log_prefix.CStr());

		return;
	}
	
	auto filter_ids = filters->second;

	for (auto &filter_id : filter_ids)
	{
		// Clone only the frame information, share the raw data.
		auto frame_clone = frame->CloneFrame();
		if (frame_clone == nullptr)
		{
			logte("%s Failed to clone frame", _log_prefix.CStr());

			continue;
		}

		PreFilterFrame(filter_id, std::move(frame_clone));
	}
}


void TranscoderStream::NotifyCreateStreams()
{
	for (auto &[output_stream_name, output_stream] : _output_streams)
	{
		UNUSED_VARIABLE(output_stream_name)

		if (_parent->CreateStream(output_stream) == false)
		{
			logtw("%s Could not create stream. [%s/%s(%u)]",
				  _log_prefix.CStr(), _application_info.GetVHostAppName().CStr(), output_stream->GetName().CStr(), output_stream->GetId());
		}
	}
}

void TranscoderStream::NotifyDeleteStreams()
{
	for (auto &[output_stream_name, output_stream] : _output_streams)
	{
		UNUSED_VARIABLE(output_stream_name)

		if (_parent->DeleteStream(output_stream) == false)
		{
			logtw("%s Could not delete stream. %s/%s(%u)",
				  _log_prefix.CStr(), _application_info.GetVHostAppName().CStr(), output_stream->GetName().CStr(), output_stream->GetId());
		}
	}

	_output_streams.clear();
}

void TranscoderStream::NotifyUpdateStreams()
{
	for (auto &[output_stream_name, output_stream] : _output_streams)
	{
		UNUSED_VARIABLE(output_stream_name)

		if (_parent->UpdateStream(output_stream) == false)
		{
			logtw("%s Could not update stream. %s/%s(%u)",
				  _log_prefix.CStr(), _application_info.GetVHostAppName().CStr(), output_stream->GetName().CStr(), output_stream->GetId());
		}
	}
}
