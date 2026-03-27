
//==============================================================================
//
//  TranscoderComposite
//
//  Created by Keukhan
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "transcoder_composite.h"

// Import CompositeMap type aliases into this translation unit for brevity.
using StreamTrack   = CompositeMap::StreamTrack;
using StreamTrackNo = CompositeMap::StreamTrackNo;
using StreamTrackPair            = CompositeMap::StreamTrackPair;
using StreamTrackPairNo      = CompositeMap::StreamTrackPairNo;

//==============================================================================
// Builder
//==============================================================================
bool CompositeMap::Build()
{
	std::unique_lock<std::shared_mutex> lock(_mutex);
	int32_t output_count = 0;
	for (auto &[key, composite] : _contexts)
	{
		(void)key;

		auto input_track_id = composite->GetInputTrackId();
		auto decoder_id     = composite->GetDecoderId();
		auto filter_id      = composite->GetFilterId();
		auto encoder_id     = composite->GetEncoderId();

		for (auto &[output_stream, output_track] : composite->GetOutputs())
		{
			auto out_pair = std::make_pair(output_stream, output_track->GetId());

			if (output_track->IsBypass())
			{
				// Bypass flow: Input → Output (direct)
				_input_to_outputs[input_track_id].push_back(out_pair);
				output_count++;
			}
			else
			{
				// Transcoding flow: 
				// 		Input → Decoder → Filter → Encoder → Output
				//                                         → Output
				//                      → Filter → Encoder → Output
				//                      → Filter → Encoder → Output
				//                                         → Output

				// InputTrack → Decoder
				_input_to_decoder[input_track_id] = decoder_id;

				// Decoder → Filter(s)
				auto &filter_ids = _decoder_to_filters[decoder_id];
				if (std::find(filter_ids.begin(), filter_ids.end(), filter_id) == filter_ids.end())
				{
					filter_ids.push_back(filter_id);
				}

				// Filter → Encoder
				_filter_to_encoder[filter_id] = encoder_id;

				// Encoder → OutputTrack
				_encoder_to_outputs[encoder_id].push_back(out_pair);

				output_count++;
			}
		}
	}

	if(output_count == 0)
	{
		return false;
	}

	// Build caches for hot-path lookups in the transcoding pipeline.
	BuildCachesLocked();

	return true;
}

void CompositeMap::BuildCachesLocked()
{
	// Cache : input_track_id → [(in_stream, in_track, out_stream, out_track)]  (bypass)
	for (auto &[input_track_id, stream_nos] : _input_to_outputs)
	{
		auto input_track = _input_stream->GetTrack(input_track_id);
		if (input_track == nullptr)
		{
			continue;
		}
		for (auto &[output_stream, output_track_id] : stream_nos)
		{
			auto output_track = output_stream->GetTrack(output_track_id);
			if (output_track != nullptr)
			{
				_cache_bypass_outputs_by_input[input_track_id].emplace_back(
					_input_stream, input_track, output_stream, output_track);
			}
		}
	}

	auto resolve_input_track = [&](MediaTrackId track_id) -> std::shared_ptr<MediaTrack> {
		for (const auto &[key, composite] : _contexts)
		{
			(void)key;
			for (const auto &[os, ot] : composite->GetOutputs())
			{
				(void)os;
				if (ot->GetId() == track_id)
				{
					return composite->GetInput().second;
				}
			}
		}
		return nullptr;
	};

	// Cache : filter_id → (in_stream, in_track, out_stream, out_track)
	for (auto &[filter_id, encoder_id] : _filter_to_encoder)
	{
		auto outputs_it = _encoder_to_outputs.find(encoder_id);
		if (outputs_it == _encoder_to_outputs.end())
		{
			continue;
		}
		for (auto &[stream, track_id] : outputs_it->second)
		{
			auto output_track = stream->GetTrack(track_id);
			auto input_track  = resolve_input_track(track_id);
			if (output_track == nullptr || input_track == nullptr)
			{
				continue;
			}
			_cache_input_output_by_filter[filter_id] =
				std::make_tuple(_input_stream, input_track, stream, output_track);
			break;
		}
	}

	// Cache : encoder_id → (in_stream, in_track, out_stream, out_track)
	for (auto &[encoder_id, stream_nos] : _encoder_to_outputs)
	{
		for (auto &[stream, track_id] : stream_nos)
		{
			auto output_track = stream->GetTrack(track_id);
			auto input_track  = resolve_input_track(track_id);
			if (output_track == nullptr || input_track == nullptr)
			{
				continue;
			}
			_cache_input_output_by_encoder[encoder_id] =
				std::make_tuple(_input_stream, input_track, stream, output_track);
			break;
		}
	}

	// Cache : encoder_id → [(out_stream, out_track)]
	for (auto &[encoder_id, stream_nos] : _encoder_to_outputs)
	{
		for (auto &[stream, track_id] : stream_nos)
		{
			auto output_track = stream->GetTrack(track_id);
			if (output_track != nullptr)
			{
				_cache_outputs_by_encoder[encoder_id].emplace_back(stream, output_track);
			}
		}
	}
}

ov::String CompositeMap::GetInfoString() const
{
	std::shared_lock<std::shared_mutex> lock(_mutex);
	ov::String log = "Component composition list\n";

	auto input_stream = _input_stream;
	if(input_stream == nullptr)
	{
		log.AppendFormat("* (No input stream)\n");
		return log;
	}

	for (auto &[input_track_id, input_track] : input_stream->GetTracks())
	{
		bool matched = false;
		log.AppendFormat("* Input(%s/%u) : %s\n",
						 input_stream->GetName().CStr(),
						 input_track_id,
						 input_track->GetInfoString().CStr());

		auto bypass_it = _input_to_outputs.find(input_track_id);
		if (bypass_it != _input_to_outputs.end())
		{
			matched = true;
			for (auto &[stream, track_id] : bypass_it->second)
			{
				auto output_track = stream->GetTrack(track_id);
				log.AppendFormat("  + Output(%s/%d) : Passthrough, %s\n",
								 stream->GetName().CStr(),
								 track_id,
								 output_track->GetInfoString().CStr());
			}
		}

		auto decoder_it = _input_to_decoder.find(input_track_id);
		if (decoder_it != _input_to_decoder.end())
		{
			matched = true;
			auto decoder_id = decoder_it->second;
			log.AppendFormat("  + Decoder(%u)\n", decoder_id);

			auto filters_it = _decoder_to_filters.find(decoder_id);
			if (filters_it != _decoder_to_filters.end())
			{
				for (auto filter_id : filters_it->second)
				{
					log.AppendFormat("    + Filter(%u)\n", filter_id);

					auto encoder_it = _filter_to_encoder.find(filter_id);
					if (encoder_it == _filter_to_encoder.end())
					{
						continue;
					}
					auto encoder_id = encoder_it->second;
					log.AppendFormat("      + Encoder(%d)\n", encoder_id);

					auto outputs_it = _encoder_to_outputs.find(encoder_id);
					if (outputs_it == _encoder_to_outputs.end())
					{
						continue;
					}
					for (auto &[stream, track_id] : outputs_it->second)
					{
						auto output_track = stream->GetTrack(track_id);
						log.AppendFormat("        + Output(%s/%u) : %s\n",
										 stream->GetName().CStr(),
										 track_id,
										 output_track->GetInfoString().CStr());
					}
				}
			}
		}

		if (!matched)
		{
			log.AppendFormat("  + (No output)\n");
		}
	}

	return log;
}

//==============================================================================
// Query Helpers
//==============================================================================
std::vector<StreamTrackNo> CompositeMap::GetDecoderList()
{
	std::shared_lock<std::shared_mutex> lock(_mutex);
	std::vector<StreamTrackNo> result;

	for (auto &[input_track_id, decoder_id] : _input_to_decoder)
	{
		auto it = _input_stream->GetTracks().find(input_track_id);
		if (it == _input_stream->GetTracks().end())
		{
			continue;
		}
		result.emplace_back(_input_stream, it->second, decoder_id);
	}

	return result;
}

std::vector<StreamTrackNo> CompositeMap::GetEncoderListByDecoderId(MediaTrackId decoder_id)
{
	std::shared_lock<std::shared_mutex> lock(_mutex);
	std::vector<StreamTrackNo> result;

	for (auto filter_id : GetFilterIdsByDecoderIdLocked(decoder_id))
	{
		auto encoder_it = _filter_to_encoder.find(filter_id);
		if (encoder_it == _filter_to_encoder.end())
		{
			continue;
		}
		auto encoder_id = encoder_it->second;

		auto outputs_it = _encoder_to_outputs.find(encoder_id);
		if (outputs_it == _encoder_to_outputs.end())
		{
			continue;
		}

		for (auto &[stream, track_id] : outputs_it->second)
		{
			auto output_track = stream->GetTrack(track_id);
			if (output_track != nullptr)
			{
				result.emplace_back(stream, output_track, encoder_id);
			}
		}
	}

	return result;
}

std::vector<StreamTrackPairNo> CompositeMap::GetFilterListByDecoderId(MediaTrackId decoder_id)
{
	std::shared_lock<std::shared_mutex> lock(_mutex);
	std::vector<StreamTrackPairNo> result;

	for (auto filter_id : GetFilterIdsByDecoderIdLocked(decoder_id))
	{
		auto encoder_it = _filter_to_encoder.find(filter_id);
		if (encoder_it == _filter_to_encoder.end())
		{
			continue;
		}

		auto outputs_it = _encoder_to_outputs.find(encoder_it->second);
		if (outputs_it == _encoder_to_outputs.end())
		{
			continue;
		}

		for (auto &[stream, track_id] : outputs_it->second)
		{
			auto output_track = stream->GetTrack(track_id);
			auto input_track  = GetInputTrackByOutputTrackIdLocked(track_id);
			if (output_track == nullptr || input_track == nullptr)
			{
				continue;
			}
			result.emplace_back(_input_stream, input_track, stream, output_track, filter_id);
		}
	}

	return result;
}

std::vector<StreamTrackPair> CompositeMap::GetInputOutputListByDecoderId(MediaTrackId decoder_id)
{
	std::shared_lock<std::shared_mutex> lock(_mutex);
	std::vector<StreamTrackPair> result;

	for (auto filter_id : GetFilterIdsByDecoderIdLocked(decoder_id))
	{
		auto encoder_it = _filter_to_encoder.find(filter_id);
		if (encoder_it == _filter_to_encoder.end())
		{
			continue;
		}

		auto outputs_it = _encoder_to_outputs.find(encoder_it->second);
		if (outputs_it == _encoder_to_outputs.end())
		{
			continue;
		}

		for (auto &[stream, track_id] : outputs_it->second)
		{
			auto output_track = stream->GetTrack(track_id);
			auto input_track  = GetInputTrackByOutputTrackIdLocked(track_id);
			if (output_track == nullptr || input_track == nullptr)
			{
				continue;
			}
			result.emplace_back(_input_stream, input_track, stream, output_track);
		}
	}

	return result;
}

std::vector<StreamTrackPair> CompositeMap::GetBypassOutputListByInputTrackId(MediaTrackId input_track_id)
{
	std::shared_lock<std::shared_mutex> lock(_mutex);
	auto it = _cache_bypass_outputs_by_input.find(input_track_id);
	if (it == _cache_bypass_outputs_by_input.end())
	{
		return {};
	}
	return it->second;
}

std::optional<StreamTrackPair> CompositeMap::GetInputOutputByFilterId(MediaTrackId filter_id)
{
	std::shared_lock<std::shared_mutex> lock(_mutex);
	auto it = _cache_input_output_by_filter.find(filter_id);
	if (it == _cache_input_output_by_filter.end())
	{
		return std::nullopt;
	}
	return it->second;
}

std::optional<StreamTrackPair> CompositeMap::GetInputOutputByEncoderId(MediaTrackId encoder_id)
{
	std::shared_lock<std::shared_mutex> lock(_mutex);
	auto it = _cache_input_output_by_encoder.find(encoder_id);
	if (it == _cache_input_output_by_encoder.end())
	{
		return std::nullopt;
	}
	return it->second;
}

std::vector<StreamTrack> CompositeMap::GetOutputListByEncoderId(MediaTrackId encoder_id)
{
	std::shared_lock<std::shared_mutex> lock(_mutex);
	auto it = _cache_outputs_by_encoder.find(encoder_id);
	if (it == _cache_outputs_by_encoder.end())
	{
		return {};
	}
	return it->second;
}


std::shared_ptr<MediaTrack> CompositeMap::GetInputTrackByOutputTrackIdLocked(MediaTrackId track_id) const
{
	for (const auto &[key, composite] : _contexts)
	{
		(void)key;
		for (const auto &[output_stream, output_track] : composite->GetOutputs())
		{
			(void)output_stream;
			if (output_track->GetId() == track_id)
			{
				return composite->GetInput().second;
			}
		}
	}
	return nullptr;
}

std::vector<MediaTrackId> CompositeMap::GetFilterIdsByDecoderIdLocked(MediaTrackId decoder_id) const
{
	auto it = _decoder_to_filters.find(decoder_id);
	if (it == _decoder_to_filters.end())
	{
		return {};
	}
	return it->second;
}

std::optional<MediaTrackId> CompositeMap::GetDecoderIdByInputTrackId(MediaTrackId input_track_id)
{
	std::shared_lock<std::shared_mutex> lock(_mutex);
	auto it = _input_to_decoder.find(input_track_id);
	if (it == _input_to_decoder.end())
	{
		return std::nullopt;
	}
	return it->second;
}

std::vector<MediaTrackId> CompositeMap::GetFilterIdsByDecoderId(MediaTrackId decoder_id)
{
	std::shared_lock<std::shared_mutex> lock(_mutex);
	return GetFilterIdsByDecoderIdLocked(decoder_id);
}

std::optional<MediaTrackId> CompositeMap::GetEncoderIdByFilterId(MediaTrackId filter_id)
{
	std::shared_lock<std::shared_mutex> lock(_mutex);
	auto it = _filter_to_encoder.find(filter_id);
	if (it == _filter_to_encoder.end())
	{
		return std::nullopt;
	}
	return it->second;
}