//==============================================================================
//
//  TranscoderStream
//
//  Created by Keukhan
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <stdint.h>

#include <memory>
#include <queue>
#include <vector>

#include "base/info/application.h"
#include "base/info/stream.h"
#include "base/mediarouter/media_buffer.h"
#include "base/mediarouter/media_type.h"
#include "transcoder_context.h"
#include "transcoder_decoder.h"
#include "transcoder_encoder.h"
#include "transcoder_filter.h"
#include "transcoder_stream_internal.h"
#include "transcoder_events.h"
#include "transcoder_overlays.h"
#include "transcoder_alert.h"

class TranscodeApplication;

class TranscoderStream : public ov::EnableSharedFromThis<TranscoderStream>,
						 public TranscoderStreamInternal,
						 public TranscoderEvents,
						 public TranscoderOverlays,
						 public TranscoderAlerts
{
public:
	class CompositeContext
	{
	public:
		CompositeContext(MediaTrackId id)
			: _id(id)
		{
		}

		MediaTrackId GetId()
		{
			return _id;
		}

		void SetInput(std::shared_ptr<info::Stream> s, std::shared_ptr<MediaTrack> t)
		{
			_input_track = std::make_pair(s, t);
		}

		void AddOutput(std::shared_ptr<info::Stream> s, std::shared_ptr<MediaTrack> t)
		{
			_output_tracks.push_back(std::make_pair(s, t));
		}

		std::shared_ptr<info::Stream> &GetInputStream()
		{
			return _input_track.first;
		}

		std::shared_ptr<MediaTrack> &GetInputTrack()
		{
			return _input_track.second;
		}

		std::vector<std::pair<std::shared_ptr<info::Stream>, std::shared_ptr<MediaTrack>>> &GetOutputTracks()
		{
			return _output_tracks;
		}

	private:
		MediaTrackId _id;

		// Input Track
		std::pair<std::shared_ptr<info::Stream>, std::shared_ptr<MediaTrack>> _input_track;

		// Output Tracks
		std::vector<std::pair<std::shared_ptr<info::Stream>, std::shared_ptr<MediaTrack>>> _output_tracks;
	};

public:
	static std::shared_ptr<TranscoderStream> Create(const info::Application &application_info, const std::shared_ptr<info::Stream> &stream, TranscodeApplication *parent);

	TranscoderStream(const info::Application &application_info, const std::shared_ptr<info::Stream> &orig_stream, TranscodeApplication *parent);
	~TranscoderStream();

	info::stream_id_t GetStreamId();

	bool Start();
	bool Stop();
	bool Prepare(const std::shared_ptr<info::Stream> &stream);
	bool Update(const std::shared_ptr<info::Stream> &stream);
	bool Push(std::shared_ptr<MediaPacket> packet);

	// Notify event to mediarouter
	void NotifyCreateStreams();
	void NotifyDeleteStreams();
	void NotifyUpdateStreams();

private:
	// Create stream --> Start stream --> Stop stream --> Delete stream
	enum class State : uint8_t
	{
		CREATED = 0,
		PREPARING,
		STARTED,
		STOPPED,
		ERROR,
	};

	const char *GetStateString(State state)
	{
		switch (state)
		{
			case State::CREATED:
				return "CREATED";
			case State::PREPARING:
				return "PREPARING";
			case State::STARTED:
				return "STARTED";
			case State::STOPPED:
				return "STOPPED";
			case State::ERROR:
				return "ERROR";
		}
		return "UNKNOWN";
	}

	// Set the stream state
	void SetState(State state)
	{
		logt("Transcoder", "%s stream state changed: %s -> %s", _log_prefix.CStr(), GetStateString(_state), GetStateString(state));
		_state.exchange(state);
	}

	State GetState() const
	{
		return _state;
	}

	std::atomic<State> _state = State::CREATED;

private:
	ov::String _log_prefix;
	
	TranscodeApplication *_parent;

	std::shared_mutex _format_change_mutex;
	std::shared_mutex _decoder_map_mutex;
	std::shared_mutex _filter_map_mutex;
	std::shared_mutex _encoder_map_mutex;


	const info::Application _application_info;

	// Output profile settings. It is used as an external profile or local profile depending on the webhook result.
	const cfg::vhost::app::oprf::OutputProfiles *GetOutputProfilesCfg()
	{
		return _output_profiles_cfg;
	}

	const cfg::vhost::app::oprf::OutputProfile *GetOutputProfileByName(const ov::String &name)
	{
		if(GetOutputProfilesCfg() == nullptr)
		{
			return nullptr;
		}
		
		for (const auto &profile : GetOutputProfilesCfg()->GetOutputProfileList())
		{
			if (profile.GetName() == name)
			{
				return &profile;
			}
		}
		return nullptr;
	}

	const cfg::vhost::app::oprf::OutputProfiles *_output_profiles_cfg;
	// Output profile set from webhook
	cfg::vhost::app::oprf::OutputProfiles _remote_output_profiles;

	// Input Stream Info
	std::shared_ptr<info::Stream> _input_stream;

	// Output Stream Info
	// [OUTPUT_STREAM_NAME, OUTPUT_stream]
	std::map<ov::String, std::shared_ptr<info::Stream>> _output_streams;

	// Map of CompositeContext	
	// Purpose of reusing the same encoding profile.
	// 
	// [
	//   - MAP_ID
	// 	 - INPUT  -> STREAM and TRACK
	// 	 - OUTPUTS -> STREAM + TRACK
	// ]
	std::map<std::pair<ov::String, cmn::MediaType>, std::shared_ptr<CompositeContext>> _composite_map;
	std::atomic<MediaTrackId> _last_composite_id = 0;

	// This map is used only when the Passthrough options is enabled.
	// [INPUT_TRACK_ID,  OUTPUT_TRACK_ID]
	std::map<MediaTrackId, std::vector<std::pair<std::shared_ptr<info::Stream>, MediaTrackId>>> _link_input_to_outputs;

	// [INPUT_TRACK_ID, DECODER_ID]
	std::map<MediaTrackId, MediaTrackId> _link_input_to_decoder;

	// [DECODER_ID, FILTER_ID]
	std::map<MediaTrackId, std::vector<MediaTrackId>> _link_decoder_to_filters;

	// [FILTER_ID, ENCODER_ID]
	std::map<MediaTrackId, MediaTrackId> _link_filter_to_encoder;

	// [ENCODER_ID, OUTPUT_TRACK_ID]
	std::map<MediaTrackId, std::vector<std::pair<std::shared_ptr<info::Stream>, MediaTrackId>>> _link_encoder_to_outputs;

	// Decoder Component
	// [DECODER_ID, DECODER]
	std::map<MediaTrackId, std::shared_ptr<TranscodeDecoder>> _decoders;

	// Last decoded frame and timestamp
	// [DECODER_ID, MediaFrame]
	std::map<MediaTrackId, std::shared_ptr<MediaFrame>> _last_decoded_frames;
	// [DECODER_ID, Timestamp(microseconds)]
	std::map<MediaTrackId, int64_t> _last_decoded_frame_pts;
	std::map<MediaTrackId, int64_t> _last_decoded_frame_duration;

	// Filters
	// [FILTER_ID, FILTER]
	std::map<MediaTrackId, std::shared_ptr<TranscodeFilter>> _filters;

	// Encoder Component
	// [ENCODER_ID, [FILTER, ENCODER]]
	std::map<MediaTrackId, std::pair<std::shared_ptr<TranscodeFilter>, std::shared_ptr<TranscodeEncoder>>> _encoders;

private:
	std::shared_ptr<MediaTrack> GetInputTrack(MediaTrackId track_id);
	std::shared_ptr<MediaTrack> GetInputTrackByOutputTrackId(MediaTrackId output_track_id);
	std::shared_ptr<info::Stream> GetInputStream();
	std::shared_ptr<info::Stream> GetOutputStreamByTrackId(MediaTrackId output_track_id);

	const cfg::vhost::app::oprf::OutputProfiles* RequestWebhook();
	bool StartInternal();
	bool PrepareInternal();
	bool UpdateInternal(const std::shared_ptr<info::Stream> &stream);

	size_t CreateOutputStreamDynamic();
	size_t CreateOutputStreams();
	std::shared_ptr<info::Stream> CreateOutputStream(const cfg::vhost::app::oprf::OutputProfile &cfg_output_profile);

	size_t BuildComposite();
	// Store information for track mapping by stage
	void AddComposite(ov::String serialized_profile,
					  std::shared_ptr<info::Stream> input_stream, std::shared_ptr<MediaTrack> input_track,
					  std::shared_ptr<info::Stream> output_stream, std::shared_ptr<MediaTrack> output_track);
	ov::String GetInfoStringComposite();

	bool CreateDecoders();
	bool CreateDecoder(MediaTrackId decoder_id, std::shared_ptr<info::Stream> input_stream, std::shared_ptr<MediaTrack> input_track);
	std::shared_ptr<TranscodeDecoder> GetDecoder(MediaTrackId decoder_id);
	void SetDecoder(MediaTrackId decoder_id, std::shared_ptr<TranscodeDecoder> decoder);
	void RemoveDecoders();


	bool CreateFilters(std::shared_ptr<MediaFrame> buffer);
	bool CreateFilter(MediaTrackId filter_id, std::shared_ptr<MediaTrack> input_track, std::shared_ptr<MediaTrack> output_track);
	std::shared_ptr<TranscodeFilter> GetFilter(MediaTrackId filter_id);
	void SetFilter(MediaTrackId filter_id, std::shared_ptr<TranscodeFilter> filter);
	void RemoveFilters();

	std::shared_ptr<MediaTrack> GetInputTrackOfFilter(MediaTrackId decoder_id);

	bool CreateEncoders(std::shared_ptr<MediaFrame> buffer);
	bool CreateEncoder(MediaTrackId encoder_id, std::shared_ptr<info::Stream> output_stream, std::shared_ptr<MediaTrack> output_track);
	std::optional<std::pair<std::shared_ptr<TranscodeFilter>, std::shared_ptr<TranscodeEncoder>>> GetEncoderSet(MediaTrackId encoder_id);
	std::shared_ptr<TranscodeFilter> GetPostFilter(MediaTrackId encoder_id);
	std::shared_ptr<TranscodeEncoder> GetEncoder(MediaTrackId encoder_id);
	void SetEncoderWithFilter(MediaTrackId encoder_id, std::shared_ptr<TranscodeFilter> filter, std::shared_ptr<TranscodeEncoder> encoder);
	void RemoveEncoders();
	void RemoveSpecificEncoders();

	void ProcessPacket(const std::shared_ptr<MediaPacket> &packet);

	// Step 1: Decode (Decode a frame from given packets)
	void BypassPacket(const std::shared_ptr<MediaPacket> &packet);	
	void DecodePacket(const std::shared_ptr<MediaPacket> &packet);
	void OnDecodedFrame(TranscodeResult result, MediaTrackId decoder_id, std::shared_ptr<MediaFrame> decoded_frame);
	void SetLastDecodedFrame(MediaTrackId decoder_id, std::shared_ptr<MediaFrame> &decoded_frame);
	std::shared_ptr<MediaFrame> GetLastDecodedFrame(MediaTrackId decoder_id);

	// Called when formatting of decoded frames is analyzed or changed.
	void ChangeOutputFormat(std::shared_ptr<MediaFrame> buffer);
	void UpdateInputTrack(std::shared_ptr<MediaFrame> buffer);
	void UpdateOutputTrack(std::shared_ptr<MediaFrame> buffer);
	void UpdateMsidOfOutputStreams(uint32_t msid);
	bool CanSeamlessTransition(const std::shared_ptr<info::Stream> &stream);
	void FlushBuffers();

	// Step 2: Filter (resample/rescale the decoded frame)
	void SpreadToFilters(MediaTrackId decoder_id, std::shared_ptr<MediaFrame> frame);
	TranscodeResult PreFilterFrame(MediaTrackId track_id, std::shared_ptr<MediaFrame> frame);
	void OnPreFilteredFrame(TranscodeResult result, MediaTrackId filter_id, std::shared_ptr<MediaFrame> decoded_frame);

	TranscodeResult PostFilterFrame(std::shared_ptr<MediaFrame> frame);
	void OnPostFilteredFrame(TranscodeResult result, MediaTrackId filter_id, std::shared_ptr<MediaFrame> decoded_frame);

	// Step 3: Encode (Encode the filtered frame to packets)
	TranscodeResult EncodeFrame(std::shared_ptr<const MediaFrame> frame);
	void OnEncodedPacket(TranscodeResult result, MediaTrackId encoder_id, std::shared_ptr<MediaPacket> encoded_packet);

	std::vector<std::shared_ptr<MediaTrack>> GetOutputTracksByEncoderId(MediaTrackId encoder_id);

	// Send encoded packet to mediarouter via transcoder application
	void SendFrame(std::shared_ptr<info::Stream> &stream, std::shared_ptr<MediaPacket> packet);

	ov::String MakeRenditionName(const ov::String &name_template, const std::shared_ptr<info::Playlist> &playlist_info, const std::shared_ptr<MediaTrack> &video_track, const std::shared_ptr<MediaTrack> &audio_track);

private:
	// Async prepare handling
	void PrepareAsync();
	std::thread _prepare_thread;
	std::atomic<bool> _prepare_thread_running = false;
	
	// Initial buffer for ready to stream
	void BufferMediaPacketUntilReadyToPlay(const std::shared_ptr<MediaPacket> &media_packet);
	bool SendBufferedPackets();
	ov::Queue<std::shared_ptr<MediaPacket>> _initial_media_packet_buffer;

	std::atomic<bool> _is_updating = false;
};
