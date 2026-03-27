//==============================================================================
//
//  TranscoderComposite
//
//  Created by Keukhan
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <stdint.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "base/info/stream.h"
#include "base/mediarouter/media_type.h"

// Represents a single encoding unit: one input track paired with one or more output tracks.
class CompositeContext
{
public:
	using StreamTrack = std::pair<std::shared_ptr<info::Stream>, std::shared_ptr<MediaTrack>>;

	CompositeContext(MediaTrackId id) : _id(id) {}

	MediaTrackId GetId()
	{
		return _id;
	}

	void SetInput(std::shared_ptr<info::Stream> stream, std::shared_ptr<MediaTrack> track)
	{
		_input = {stream, track};
	}

	void AddOutput(std::shared_ptr<info::Stream> stream, std::shared_ptr<MediaTrack> track)
	{
		_outputs.emplace_back(stream, track);
	}

	StreamTrack &GetInput()
	{
		return _input;
	}	

	std::vector<StreamTrack> &GetOutputs()
	{
		return _outputs;
	}

public:	
	MediaTrackId GetInputTrackId()
	{
		return _input.second->GetId();
	}

	MediaTrackId GetDecoderId()
	{
		return _input.second->GetId();
	}

	MediaTrackId GetFilterId()
	{
		return _id;
	}

	MediaTrackId GetEncoderId()
	{
		return _id;
	}

private:
	MediaTrackId _id;

	StreamTrack _input;
	std::vector<StreamTrack> _outputs;
};

// Manages all CompositeContext instances and the derived pipeline link maps.
//
// Pipeline flow (non-bypass):
//   Input → [_input_to_decoder] → Decoder
//         → [_decoder_to_filters] → Filter(s)
//         → [_filter_to_encoder] → Encoder
//         → [_encoder_to_outputs] → Output(s)
//
// Bypass flow:
//   Input → [_input_to_outputs] → Output(s)
class CompositeMap
{
public:
	// ─── Type aliases ────────────────────────────────────────────────────────
	// (stream, component_id)
	using StreamNo			= std::pair<std::shared_ptr<info::Stream>, MediaTrackId>;
	// (stream, track)
	using StreamTrack		= std::pair<std::shared_ptr<info::Stream>, std::shared_ptr<MediaTrack>>;
	// (stream, track, component_id)
	using StreamTrackNo		= std::tuple<std::shared_ptr<info::Stream>, std::shared_ptr<MediaTrack>, MediaTrackId>;
	// (input_stream, input_track, output_stream, output_track)
	using StreamTrackPair	= std::tuple<std::shared_ptr<info::Stream>, std::shared_ptr<MediaTrack>,
										 std::shared_ptr<info::Stream>, std::shared_ptr<MediaTrack>>;
	// (input_stream, input_track, output_stream, output_track, component_id)
	using StreamTrackPairNo = std::tuple<std::shared_ptr<info::Stream>, std::shared_ptr<MediaTrack>,
										 std::shared_ptr<info::Stream>, std::shared_ptr<MediaTrack>, MediaTrackId>;

public:
	// ─── Lifecycle ───────────────────────────────────────────────────────────

	// Groups the (input → output) pair under a composite keyed by serialized profile.
	void AddComposite(
		std::shared_ptr<info::Stream> input_stream, std::shared_ptr<MediaTrack> input_track,
		ov::String serialized_profile,
		std::shared_ptr<info::Stream> output_stream, std::shared_ptr<MediaTrack> output_track)
	{
		auto key		= std::make_pair(serialized_profile, input_track->GetMediaType());

		std::unique_lock<std::shared_mutex> lock(_mutex);
		auto &composite = _contexts[key];
		if (composite == nullptr)
		{
			composite = std::make_shared<CompositeContext>(_last_composite_id++);
			composite->SetInput(input_stream, input_track);
		}

		composite->AddOutput(output_stream, output_track);
		_input_stream = input_stream;
	}

	void Clear()
	{
		std::unique_lock<std::shared_mutex> lock(_mutex);
		_contexts.clear();
		_last_composite_id = 0;

		_input_stream	   = nullptr;

		_input_to_outputs.clear();

		_input_to_decoder.clear();
		_decoder_to_filters.clear();
		_filter_to_encoder.clear();
		_encoder_to_outputs.clear();

		_cache_bypass_outputs_by_input.clear();
		_cache_input_output_by_filter.clear();
		_cache_input_output_by_encoder.clear();
		_cache_outputs_by_encoder.clear();
	}

	bool Build();

	// ─── Link-map accessors ──────────────────────────────────────────────────

	std::optional<MediaTrackId> GetDecoderIdByInputTrackId(MediaTrackId input_track_id);
	std::vector<MediaTrackId> GetFilterIdsByDecoderId(MediaTrackId decoder_id);
	std::optional<MediaTrackId> GetEncoderIdByFilterId(MediaTrackId filter_id);

	// ─── Composite query helpers ─────────────────────────────────────────────

	// Returns [(input_stream, input_track, decoder_id)]
	std::vector<StreamTrackNo> GetDecoderList();

	// Returns [(output_stream, output_track, encoder_id)] 
	std::vector<StreamTrackNo> GetEncoderListByDecoderId(MediaTrackId decoder_id);

	// Returns [(in_stream, in_track, out_stream, out_track, filter_id)]
	std::vector<StreamTrackPairNo> GetFilterListByDecoderId(MediaTrackId decoder_id);

	// Returns [(in_stream, in_track, out_stream, out_track)] 
	std::vector<StreamTrackPair> GetInputOutputListByDecoderId(MediaTrackId decoder_id);

	// Returns [(out_stream, out_track)] 
	std::vector<StreamTrackPair> GetBypassOutputListByInputTrackId(MediaTrackId input_track_id);

	// Returns (in_stream, in_track, out_stream, out_track) 
	std::optional<StreamTrackPair> GetInputOutputByFilterId(MediaTrackId filter_id);

	// Returns (in_stream, in_track, out_stream, out_track) 
	std::optional<StreamTrackPair> GetInputOutputByEncoderId(MediaTrackId encoder_id);

	// Returns [(out_stream, out_track)] 
	std::vector<StreamTrack> GetOutputListByEncoderId(MediaTrackId encoder_id);

	ov::String GetInfoString() const;

public:
	std::atomic<MediaTrackId> _last_composite_id = 0;

	// Profile key → CompositeContext
	std::map<std::pair<ov::String, cmn::MediaType>, std::shared_ptr<CompositeContext>> _contexts;

	std::shared_ptr<info::Stream> _input_stream = nullptr;

	// Bypass:   InputTrackId  → [(OutputStream, OutputTrackId)]
	std::map<MediaTrackId, std::vector<StreamNo>> _input_to_outputs;

	// Decode:   InputTrackId  → DecoderId  (1:1)
	std::map<MediaTrackId, MediaTrackId> _input_to_decoder;

	// Filter:   DecoderId     → [FilterId]  (1:N)
	std::map<MediaTrackId, std::vector<MediaTrackId>> _decoder_to_filters;

	// Encode:   FilterId      → EncoderId  (1:1)
	std::map<MediaTrackId, MediaTrackId> _filter_to_encoder;

	// Output:   EncoderId     → [(OutputStream, OutputTrackId)]  (1:N)
	std::map<MediaTrackId, std::vector<StreamNo>> _encoder_to_outputs;

private:
	mutable std::shared_mutex _mutex;

	// Lock-free versions for internal use (caller must hold _mutex).
	std::shared_ptr<MediaTrack> GetInputTrackByOutputTrackIdLocked(MediaTrackId track_id) const;
	std::vector<MediaTrackId>   GetFilterIdsByDecoderIdLocked(MediaTrackId decoder_id) const;
	void                        BuildCachesLocked();

	// Caches
	// These are for hot-path lookups in the transcoding pipeline. 
	std::unordered_map<MediaTrackId, std::vector<StreamTrackPair>> _cache_bypass_outputs_by_input;
	std::unordered_map<MediaTrackId, StreamTrackPair>              _cache_input_output_by_filter;
	std::unordered_map<MediaTrackId, StreamTrackPair>              _cache_input_output_by_encoder;
	std::unordered_map<MediaTrackId, std::vector<StreamTrack>>     _cache_outputs_by_encoder;
};
