//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================

#include <algorithm>
#include <cmath>

#include "fmp4_packager.h"
#include "fmp4_private.h"

#include <modules/bitstream/nalu/nal_stream_converter.h>
#include <modules/bitstream/aac/aac_converter.h>
#include <modules/bitstream/av1/av1_parser.h>
#include <modules/bitstream/av1/av1_types.h>

#include <base/modules/data_format/id3v2/id3v2.h>
#include <base/modules/data_format/id3v2/frames/id3v2_text_frame.h>

#include <base/modules/data_format/cue_event/cue_event.h>

namespace bmff
{
	FMP4Packager::FMP4Packager(const std::shared_ptr<FMP4Storage> &storage, const std::shared_ptr<SegmentBoundaryPolicy> &boundary_policy, const std::shared_ptr<const MediaTrack> &media_track, const std::shared_ptr<const MediaTrack> &data_track, const Config &config)
		: Packager(media_track, data_track, config.cenc_property)
	{
		_storage = storage;
		_boundary_policy = boundary_policy;
		_config = config;

		_target_chunk_duration_ms = _config.chunk_duration_ms;
	}

	FMP4Packager::~FMP4Packager()
	{
		logtt("FMP4Packager has been terminated finally");
	}

	// Generate Initialization FMP4Segment
	bool FMP4Packager::CreateInitializationSegment()
	{
		auto track = GetMediaTrack();
		if (track == nullptr)
		{
			logte("Failed to create initialization segment. Track is null");
			return false;
		}

		if ((track->GetCodecId() == cmn::MediaCodecId::H264) ||
			(track->GetCodecId() == cmn::MediaCodecId::H265) ||
			(track->GetCodecId() == cmn::MediaCodecId::Av1) ||
			(track->GetCodecId() == cmn::MediaCodecId::Aac))
		{
			// Supported codecs
		}
		else
		{
			logtw("FMP4Packager::Initialize() - Unsupported codec id(%s)", cmn::GetCodecIdString(track->GetCodecId()));
			return false;
		}

		// Create Initialization FMP4Segment
		ov::ByteStream stream(4096);
		
		if (WriteFtypBox(stream) == false)
		{
			logte("FMP4Packager::Initialize() - Failed to write ftyp box");
			return false;
		}

		if (WriteMoovBox(stream) == false)
		{
			logte("FMP4Packager::Initialize() - Failed to write moov box");
			return false;
		}

		if (StoreInitializationSection(stream.GetDataPointer()) == false)
		{
			return false;
		}

		// The moov just written carries this version's tenc/pssh, so the key it was built
		// with is the key every segment of this version is encrypted with
		if (_storage != nullptr)
		{
			auto content_version = _storage->GetContentVersion();
			_cenc_property_by_version[content_version] = GetCencProperty();

			while (_cenc_property_by_version.size() > kMaxRetainedCencVersions)
			{
				_cenc_property_by_version.erase(_cenc_property_by_version.begin());
			}
		}

		return true;
	}

	std::optional<CencProperty> FMP4Packager::GetCencPropertyForVersion(uint32_t content_version) const
	{
		auto it = _cenc_property_by_version.find(content_version);
		if (it == _cenc_property_by_version.end())
		{
			return std::nullopt;
		}

		return it->second;
	}

	bool FMP4Packager::UpdateTrack(const std::shared_ptr<const MediaTrack> &media_track)
	{
		if (media_track == nullptr)
		{
			return false;
		}

		// The same codecs CreateInitializationSegment supports; validate before mutating
		switch (media_track->GetCodecId())
		{
			case cmn::MediaCodecId::H264:
			case cmn::MediaCodecId::H265:
			case cmn::MediaCodecId::Av1:
			case cmn::MediaCodecId::Aac:
				// Supported codecs
				break;
			default:
				logtw("FMP4Packager::UpdateTrack() - Unsupported codec id(%s)", cmn::GetCodecIdString(media_track->GetCodecId()));
				return false;
		}

		// Close the current content so that samples of different track versions never
		// share a segment
		if (Flush() == false)
		{
			return false;
		}

		int64_t completed_segment_number = -1;
		if (_storage == nullptr || _storage->UpdateTrack(media_track, completed_segment_number) == false)
		{
			return false;
		}

		if (UpdateMediaTrack(media_track) == false)
		{
			// Publish the completion even on failure so the playlist can close the segment
			_storage->NotifySegmentCompleted(completed_segment_number);
			return false;
		}

		if (media_track->GetMediaType() == cmn::MediaType::Video)
		{
			// The new content must start with a keyframe
			_waiting_for_keyframe = true;
			_dropped_samples_while_waiting = 0;
		}

		bool init_section_created = CreateInitializationSegment();

		// The completion is published only after the new initialization section is
		// stored; the playlist hints the new map, so it must be servable immediately
		_storage->NotifySegmentCompleted(completed_segment_number);

		return init_section_created;
	}

	void FMP4Packager::RequestCutForDiscontinuity(double boundary_timestamp_ms)
	{
		if (_boundary_policy == nullptr)
		{
			return;
		}

		// Where the cut lands is the boundary policy's decision
		_boundary_policy->RequestDiscontinuity(std::llround(boundary_timestamp_ms * 1000.0));
	}

	void FMP4Packager::RequestKeyRotation(const CencProperty &cenc_property)
	{
		_pending_key_rotation = cenc_property;
	}

	double FMP4Packager::GetLastSampleEndTimestampMs() const
	{
		return _last_sample_end_timestamp_ms;
	}

	uint32_t FMP4Packager::GetCurrentContentVersion() const
	{
		return (_storage != nullptr) ? _storage->GetContentVersion() : 0;
	}

	bool FMP4Packager::ReserveDataPacket(const std::shared_ptr<const MediaPacket> &media_packet)
	{
		if (GetDataTrack() == nullptr)
		{
			return false;
		}

		_reserved_data_packets.emplace(media_packet);
		return true;
	}

	std::shared_ptr<bmff::Samples> FMP4Packager::GetDataSamples(int64_t start_timestamp, int64_t end_timestamp)
	{
		if (GetDataTrack() == nullptr)
		{
			return nullptr;
		}

		auto rescaled_start_timestamp = ((double)start_timestamp / (double)GetMediaTrack()->GetTimeBase().GetTimescale()) * (double)GetDataTrack()->GetTimeBase().GetTimescale();
		auto rescaled_end_timestamp = ((double)end_timestamp / (double)GetMediaTrack()->GetTimeBase().GetTimescale()) * (double)GetDataTrack()->GetTimeBase().GetTimescale();

		auto samples = std::make_shared<Samples>();

		while (true)
		{
			if (_reserved_data_packets.size() == 0)
			{
				break;
			}

			auto data_packet = _reserved_data_packets.front();

			// Convert data pts timescale to media timescale
			auto pts = (double)data_packet->GetPts();

			logtt("track(%d), pts: %lf, start_timestamp: %lf, end_timestamp: %lf", GetMediaTrack()->GetId(), pts, rescaled_start_timestamp, rescaled_end_timestamp);

			if (pts == -1)
			{
				// Packets that must be inserted immediately
				auto copy_data_packet = data_packet->ClonePacket();
				copy_data_packet->SetPts(rescaled_start_timestamp);
				copy_data_packet->SetDts(rescaled_start_timestamp);
				
				samples->AppendSample(Sample(copy_data_packet));

				_reserved_data_packets.pop();
			}
			else if (pts > rescaled_end_timestamp)
			{
				//Waits for a segment within a time interval.
				break;
			}
			else if (pts < rescaled_start_timestamp)
			{
				// Too old data, ajust to start_timestamp
				_reserved_data_packets.pop();
			}
			else
			{
				// Within the time interval
				samples->AppendSample(data_packet);

				_reserved_data_packets.pop();
			}
		}

		return samples;
	}

	// Generate Media FMP4Segment
	bool FMP4Packager::AppendSample(const std::shared_ptr<const MediaPacket> &media_packet)
	{
		logtt("MediaPacket : track(%d) pts(%" PRId64 "), dts(%" PRId64 "), duration(%" PRId64 "), flag(%d), size(%zu)", media_packet->GetTrackId(), media_packet->GetPts(), media_packet->GetDts(), media_packet->GetDuration(), ov::ToUnderlyingType(media_packet->GetFlag()), media_packet->GetDataLength());

		if (_waiting_for_keyframe == true)
		{
			if (media_packet->GetFlag() != MediaPacketFlag::Key)
			{
				_dropped_samples_while_waiting++;
				return true;
			}

			_waiting_for_keyframe = false;
			if (_dropped_samples_while_waiting > 0)
			{
				logtw("track(%u) - Dropped %u non-keyframe samples until the first keyframe of the new track configuration", GetMediaTrack()->GetId(), _dropped_samples_while_waiting);
				_dropped_samples_while_waiting = 0;
			}
		}

		// Convert bitstream format
		auto next_frame = ConvertBitstreamFormat(media_packet);
		if (next_frame == nullptr || next_frame->GetData() == nullptr)
		{
			logtw("Failed to convert bitstream format for track(%d)", GetMediaTrack()->GetId());
			return false;
		}

		std::shared_ptr<Samples> samples = _sample_buffer.GetSamples();
		if (samples == nullptr)
		{
			// The policy is consulted even with nothing buffered: a pending
			// discontinuity may have to cut right here
			samples = _no_samples;
		}

		auto last_segment = std::static_pointer_cast<FMP4Segment>(_storage->GetLastSegment());
		// Set from the samples actually taken, where the chunk is stored
		double total_sample_duration_ms = 0.0;
		bool next_frame_is_idr = (next_frame->GetFlag() == MediaPacketFlag::Key) || (GetMediaTrack()->GetMediaType() == cmn::MediaType::Audio);

		if (_boundary_policy != nullptr)
		{
			// https://datatracker.ietf.org/doc/html/draft-pantos-hls-rfc8216bis#section-4.4.3.8
			// The duration of a Partial Segment MUST be less than or equal to the Part Target Duration.  
			// The duration of each Partial Segment MUST be at least 85% of the Part Target Duration, 
			// with the exception of Partial Segments with the INDEPENDENT=YES attribute 
			// and the final Partial Segment of any Parent Segment.
			int64_t last_segment_duration_us = 0;
			if (last_segment != nullptr && last_segment->IsCompleted() == false)
			{
				last_segment_duration_us = std::llround(last_segment->GetDurationMs() * 1000.0);
			}

			// Where the accumulating segment starts: the incomplete segment in the
			// storage, else the samples opening the next one, else this frame.
			// Known at the first sample, so a boundary policy can answer correctly
			// even before the first chunk reaches the storage.
			int64_t segment_start_timestamp = next_frame->GetDts();
			if (last_segment != nullptr && last_segment->IsCompleted() == false && last_segment->GetPartialCount() > 0)
			{
				segment_start_timestamp = last_segment->GetStartTimestamp();
			}
			else if (samples->GetTotalCount() > 0)
			{
				segment_start_timestamp = samples->GetStartTimestamp();
			}

			double timescale = GetMediaTrack()->GetTimeBase().GetTimescale();
			int64_t segment_start_timestamp_us = TicksToUs(segment_start_timestamp, timescale);

			SampleTiming next_timing;
			next_timing.dts_us = TicksToUs(next_frame->GetDts(), timescale);
			next_timing.pts_us = TicksToUs(next_frame->GetPts(), timescale);
			next_timing.duration_us = TicksToUs(next_frame->GetDts() + next_frame->GetDuration(), timescale) - next_timing.dts_us;
			next_timing.independent = next_frame_is_idr;

			auto decision = _boundary_policy->GetChunkPlan(*samples, next_timing, segment_start_timestamp_us, last_segment_duration_us);

			if (decision.discontinuity == true && decision.emit_count == 0)
			{
				// The timeline breaks here with nothing buffered for the old content
				if (_storage != nullptr)
				{
					_storage->CutSegmentForDiscontinuity();
				}
			}
			else if (decision.completes_segment == true && decision.emit_count == 0)
			{
				// A marker cut with every buffered sample belonging after it: the
				// segment closes on what is already stored
				if (_storage != nullptr)
				{
					_storage->CutSegmentAtMarker();
				}
			}
			else if (decision.emit_count > 0)
			{
				// Take only what goes out; the buffer keeps the rest
				samples = _sample_buffer.PopFront(decision.emit_count);
				if (samples == nullptr)
				{
					logtc("FMP4Packager::AppendSample() - Failed to take samples for the chunk, track(%u)", GetMediaTrack()->GetId());
					return false;
				}
				total_sample_duration_ms = (samples->GetTotalDuration() / timescale) * 1000.0;

				double reserve_buffer_size;

				if (GetMediaTrack()->GetMediaType() == cmn::MediaType::Video)
				{
					// Reserve 10 Mbps.
					reserve_buffer_size = (_target_chunk_duration_ms / 1000.0) * ((10.0 * 1000.0 * 1000.0) / 8.0);
				}
				else
				{
					// Reserve 0.5 Mbps.
					reserve_buffer_size = (_target_chunk_duration_ms / 1000.0) * ((0.5 * 1000.0 * 1000.0) / 8.0);
				}

				ov::ByteStream chunk_stream(reserve_buffer_size);
				
				auto data_samples = GetDataSamples(samples->GetStartTimestamp(), samples->GetEndTimestamp());
				if (data_samples != nullptr)
				{
					if (WriteEmsgBox(chunk_stream, data_samples) == false)
					{
						logtw("FMP4Packager::AppendSample() - Failed to write emsg box");
					}
				}

				if (WriteMoofBox(chunk_stream, samples) == false)
				{
					logte("FMP4Packager::AppendSample() - Failed to write moof box");
					return false;
				}

				if (WriteMdatBox(chunk_stream, samples) == false)
				{
					logte("FMP4Packager::AppendSample() - Failed to write mdat box");
					return false;
				}

				auto chunk = chunk_stream.GetDataPointer();

				bool last_chunk = decision.completes_segment;

				if (_storage != nullptr && _storage->AppendMediaChunk(chunk,
												samples->GetStartTimestamp(),
												total_sample_duration_ms,
												samples->IsIndependent(),
												last_chunk, decision.discontinuity) == false)
				{
					logte("FMP4Packager::AppendSample() - Failed to store media chunk");
					return false;
				}

				// Set the average chunk duration to config.chunk_duration_ms
				// _target_chunk_duration_ms -= total_sample_duration_ms;
				// _target_chunk_duration_ms += _config.chunk_duration_ms;
			}
		}

		// A DRM key rotation changes no content, so it needs no cut: it takes effect where
		// a new segment starts on its own. Applying it here keeps every segment on a
		// single key and leaves the segment duration pacing untouched.
		if (_pending_key_rotation.has_value() == true)
		{
			auto buffered_samples = _sample_buffer.GetSamples();
			bool buffer_is_empty = (buffered_samples == nullptr) || (buffered_samples->GetTotalCount() == 0);

			auto pending_segment = std::static_pointer_cast<FMP4Segment>(_storage->GetLastSegment());
			bool segment_is_starting = (pending_segment == nullptr) ||
									   ((pending_segment->IsCompleted() == false) && (pending_segment->GetPartialCount() == 0));

			// The new version starts at an independently decodable sample
			bool independent = (next_frame->GetFlag() == MediaPacketFlag::Key) || (GetMediaTrack()->GetMediaType() == cmn::MediaType::Audio);

			if (buffer_is_empty == true && segment_is_starting == true && independent == true)
			{
				_storage->StartNewContentVersionForKeyRotation();

				// Rebuild the encryptor with the new key, then regenerate the
				// initialization section so its tenc/pssh carry that key; it is stored
				// under the version advanced above
				UpdateCencProperty(_pending_key_rotation.value());
				if (CreateInitializationSegment() == false)
				{
					// The version has no initialization section and no key to advertise,
					// so this track cannot be played from here on
					logtc("FMP4Packager::AppendSample() - Failed to regenerate initialization segment for key rotation, track(%u)", GetMediaTrack()->GetId());
				}

				_pending_key_rotation.reset();
			}
		}

		if (_sample_buffer.AppendSample(next_frame) == false)
		{
			logte("FMP4Packager::AppendSample() - Failed to append sample");
			return false;
		}

		double timescale = GetMediaTrack()->GetTimeBase().GetTimescale();
		_last_sample_end_timestamp_ms = static_cast<double>(next_frame->GetDts() + next_frame->GetDuration()) / timescale * 1000.0;

		return true;
	}

	bool FMP4Packager::Flush()
	{
		std::shared_ptr<Samples> samples = _sample_buffer.GetSamples();

		if (samples != nullptr && samples->GetTotalCount() > 0)
		{
			ov::ByteStream chunk_stream(4096);

			auto data_samples = GetDataSamples(samples->GetStartTimestamp(), samples->GetEndTimestamp());
			if (data_samples != nullptr)
			{
				if (WriteEmsgBox(chunk_stream, data_samples) == false)
				{
					logtw("FMP4Packager::Flush() - Failed to write emsg box");
				}
			}

			if (WriteMoofBox(chunk_stream, samples) == false)
			{
				logte("FMP4Packager::Flush() - Failed to write moof box");
				return false;
			}

			if (WriteMdatBox(chunk_stream, samples) == false)
			{
				logte("FMP4Packager::Flush() - Failed to write mdat box");
				return false;
			}

			auto chunk = chunk_stream.GetDataPointer();

			// Storage expects milliseconds, samples hold durations in timescale units
			double total_sample_duration_ms = (static_cast<double>(samples->GetTotalDuration()) / GetMediaTrack()->GetTimeBase().GetTimescale()) * 1000.0;

			bool last_chunk = true;
			if (_storage != nullptr && _storage->AppendMediaChunk(chunk,
											samples->GetStartTimestamp(),
											total_sample_duration_ms,
											samples->IsIndependent(), last_chunk) == false)
			{
				logte("FMP4Packager::Flush() - Failed to store media chunk");
				return false;
			}

			_sample_buffer.Reset();
		}

		return true;
	}

	// Get config
	const FMP4Packager::Config &FMP4Packager::GetConfig() const
	{
		return _config;
	}

	bool FMP4Packager::StoreInitializationSection(const std::shared_ptr<ov::Data> &section)
	{
		if (section == nullptr || _storage == nullptr)
		{
			return false;
		}

		if (_storage->StoreInitializationSection(section) == false)
		{
			return false;
		}

		return true;
	}

	std::shared_ptr<const MediaPacket> FMP4Packager::ConvertBitstreamFormat(const std::shared_ptr<const MediaPacket> &media_packet)
	{
		auto converted_packet = media_packet;

		// fmp4 uses avcC format
		if (media_packet->GetBitstreamFormat() == cmn::BitstreamFormat::H264_AVCC)
		{

		}
		else if (media_packet->GetBitstreamFormat() == cmn::BitstreamFormat::HVCC)
		{

		}
		else if (media_packet->GetBitstreamFormat() == cmn::BitstreamFormat::H264_ANNEXB)
		{
			auto converted_data = NalStreamConverter::ConvertAnnexbToXvcc(media_packet->GetData(), media_packet->GetFragHeader());
			if (converted_data == nullptr)
			{
				logtw("FMP4Packager::ConvertBitstreamFormat() - Failed to convert annexb to avcc");
				return nullptr;
			}

			auto new_packet = std::make_shared<MediaPacket>(*media_packet);
			new_packet->SetData(converted_data);
			new_packet->SetBitstreamFormat(cmn::BitstreamFormat::H264_AVCC);
			new_packet->SetPacketType(cmn::PacketType::NALU);

			converted_packet = new_packet;
		}
		else if (media_packet->GetBitstreamFormat() == cmn::BitstreamFormat::H265_ANNEXB)
		{
			auto converted_data = NalStreamConverter::ConvertAnnexbToXvcc(media_packet->GetData(), media_packet->GetFragHeader());
			if (converted_data == nullptr)
			{
				logtw("FMP4Packager::ConvertBitstreamFormat() - Failed to convert annexb to hvcc");
				return nullptr;
			}

			auto new_packet = std::make_shared<MediaPacket>(*media_packet);
			new_packet->SetData(converted_data);
			new_packet->SetBitstreamFormat(cmn::BitstreamFormat::HVCC);
			new_packet->SetPacketType(cmn::PacketType::NALU);

			converted_packet = new_packet;
		}
		else if (media_packet->GetBitstreamFormat() == cmn::BitstreamFormat::AAC_ADTS)
		{
			auto raw_data = AacConverter::ConvertAdtsToRaw(media_packet->GetData(), nullptr);
			if (raw_data == nullptr)
			{
				logtw("FMP4Packager::ConvertBitstreamFormat() - Failed to convert adts to raw");
				return nullptr;
			}

			auto new_packet = std::make_shared<MediaPacket>(*media_packet);
			new_packet->SetData(raw_data);
			new_packet->SetBitstreamFormat(cmn::BitstreamFormat::AAC_RAW);
			new_packet->SetPacketType(cmn::PacketType::RAW);

			converted_packet = new_packet;
		}
		else if (media_packet->GetBitstreamFormat() == cmn::BitstreamFormat::AAC_RAW)
		{
		}
		else if (media_packet->GetBitstreamFormat() == cmn::BitstreamFormat::AV1_OBU)
		{
			// AV1 ISOBMFF v1.3.0 section 2.4:
			// - OBU_TEMPORAL_DELIMITER, OBU_PADDING, and OBU_REDUNDANT_FRAME_HEADER
			//   SHOULD NOT be used. We strip temporal delimiters here; padding and
			//   redundant frame headers are not expected from our encoder pipeline.
			// - obu_has_size_field SHALL be 1 for every OBU except the last in a sample,
			//   which MAY omit it. `Av1Parser::ReadObu()` handles both cases: an unsized
			//   TemporalDelimiter has an empty payload, and any other unsized OBU takes
			//   the remainder of the buffer (so it is necessarily the terminal OBU).
			auto data		   = media_packet->GetData();
			const auto *base   = data->GetDataAs<uint8_t>();
			const size_t total = data->GetLength();

			if (total == 0)
			{
				return nullptr;
			}

			// Walk the OBUs once, copying everything except temporal delimiters. `ReadObu()`
			// returns false on malformed input and tolerates a missing size field on the
			// terminal OBU, so no separate size-field validation pass is needed.
			auto filtered = std::make_shared<ov::Data>(total);
			size_t offset = 0;
			Av1ObuSpan obu;

			while (offset < total)
			{
				if (Av1Parser::ReadObu(base, total, offset, obu) == false)
				{
					logte("FMP4Packager::ConvertBitstreamFormat() - Failed to parse AV1 OBU at offset %zu", offset);
					return nullptr;
				}

				if (obu.header.type != Av1ObuType::TemporalDelimiter)
				{
					filtered->Append(base + obu.obu_offset, obu.next_offset - obu.obu_offset);
				}

				offset = obu.next_offset;
			}

			if (filtered->GetLength() == 0)
			{
				return nullptr;
			}

			auto new_packet = std::make_shared<MediaPacket>(*media_packet);
			new_packet->SetData(filtered);
			converted_packet = new_packet;
		}
		else
		{
			// Not supported yet
		}

		return converted_packet;
	}	

	bool FMP4Packager::WriteFtypBox(ov::ByteStream &data_stream)
	{
		ov::ByteStream stream(128);

		stream.WriteText("iso6"); // major brand
		stream.WriteBE32(0); // minor version
		stream.WriteText("iso6mp42");  // compatible brands

		{
			switch (GetMediaTrack()->GetCodecId())
			{
				case cmn::MediaCodecId::H264:
					stream.WriteText("avc1");
					break;

				case cmn::MediaCodecId::H265:
					stream.WriteText("hvc1");
					break;

				case cmn::MediaCodecId::Av1:
					stream.WriteText("av01");
					break;

				default:
					// Non-video tracks (e.g. AAC) carry no video codec brand
					break;
			}
		}

		stream.WriteText("dashhlsfaid3");

		// stream.WriteText("mp42"); // major brand
		// stream.WriteBE32(0); // minor version
		// stream.WriteText("isommp42iso5dash"); // compatible brands
		
		return WriteBox(data_stream, "ftyp", *stream.GetData());
	}
} // namespace bmff