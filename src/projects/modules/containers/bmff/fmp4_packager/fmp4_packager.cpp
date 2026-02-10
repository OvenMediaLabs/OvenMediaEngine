//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================

#include "fmp4_packager.h"
#include "fmp4_private.h"

#include <modules/bitstream/nalu/nal_stream_converter.h>
#include <modules/bitstream/aac/aac_converter.h>

#include <base/modules/data_format/id3v2/id3v2.h>
#include <base/modules/data_format/id3v2/frames/id3v2_text_frame.h>

#include <base/modules/data_format/cue_event/cue_event.h>

namespace bmff
{
	FMP4Packager::FMP4Packager(const std::shared_ptr<FMP4Storage> &storage, const std::shared_ptr<const MediaTrack> &media_track, const std::shared_ptr<const MediaTrack> &data_track, const Config &config)
		: Packager(media_track, data_track, config.cenc_property)
	{
		_storage = storage;
		_config = config;

		_target_chunk_duration_ms = _config.chunk_duration_ms;

		if (media_track->GetMediaType() == cmn::MediaType::Video)
		{
			_segmentation_info.keyframe_interval = media_track->GetKeyFrameInterval();
		}
		else 
		{
			_segmentation_info.keyframe_interval = 1;
		}
		
		_segmentation_info.media_type = media_track->GetMediaType();
		_segmentation_info.framerate = media_track->GetFrameRate();
		_segmentation_info.target_segment_duration_ms = _config.segment_duration_ms;
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

		if (track->GetCodecId() == cmn::MediaCodecId::H264 || 
			track->GetCodecId() == cmn::MediaCodecId::H265 ||
			track->GetCodecId() == cmn::MediaCodecId::Aac)
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

		return StoreInitializationSection(stream.GetDataPointer());
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

		// Convert bitstream format
		auto next_frame = ConvertBitstreamFormat(media_packet);
		if (next_frame == nullptr || next_frame->GetData() == nullptr)
		{
			logtw("Failed to convert bitstream format for track(%d)", GetMediaTrack()->GetId());
			return false;
		}

		std::shared_ptr<Samples> samples = _sample_buffer.GetSamples();

		auto last_segment = std::static_pointer_cast<FMP4Segment>(_storage->GetLastSegment());
		double total_sample_duration = samples != nullptr ? samples->GetTotalDuration() : 0;
		double total_sample_duration_ms = (static_cast<double>(total_sample_duration) / GetMediaTrack()->GetTimeBase().GetTimescale()) * 1000.0;
		// Calculate duration as milliseconds
		double next_total_sample_duration = total_sample_duration + next_frame->GetDuration();
		double next_total_sample_duration_ms = (static_cast<double>(next_total_sample_duration) / GetMediaTrack()->GetTimeBase().GetTimescale()) * 1000.0;
		bool next_frame_is_idr = (next_frame->GetFlag() == MediaPacketFlag::Key) || (GetMediaTrack()->GetMediaType() == cmn::MediaType::Audio);

		// Marker handling
		constexpr uint8_t kNoMarker = 0;
		constexpr uint8_t kFlushAsSoonAsPossible = 1;
		constexpr uint8_t kShouldDeferSegmentFlush = 2;
		constexpr uint8_t kShouldFlushImmediately = 3;

		int64_t last_sequence_number = -1; 
		if (last_segment != nullptr)
		{
			last_sequence_number = last_segment->GetNumber();
			if (last_segment->IsCompleted() == true)
			{
				last_sequence_number++;
			}
		}

		bool has_marker_in_this_sequence = HasMarkerWithSeq(last_sequence_number);
		bool has_marker_in_next_sample = HasMarker(next_frame->GetDts(), next_frame->GetDts() + next_frame->GetDuration());
		bool has_marker_in_curr_samples = false;
		if (samples != nullptr)
		{
			has_marker_in_curr_samples = HasMarker(samples->GetStartTimestamp(), samples->GetEndTimestamp());
		}

		uint8_t marker_handling = kNoMarker;

		ov::String marker_handling_desc; 
		if (has_marker_in_this_sequence == true && has_marker_in_curr_samples == true)
		{
			marker_handling = kFlushAsSoonAsPossible;
			marker_handling_desc = "Flush as soon as possible";
		}
		else if (has_marker_in_this_sequence == true && has_marker_in_curr_samples == false)
		{
			marker_handling = kShouldDeferSegmentFlush;
			marker_handling_desc = "Defer segment flush";
		}
		else if (has_marker_in_this_sequence == false && has_marker_in_next_sample == true)
		{
			marker_handling = kShouldFlushImmediately;
			marker_handling_desc = "Flush immediately";
		}
		else if (has_marker_in_this_sequence == false && has_marker_in_curr_samples == true)
		{
			// Too Late
			// Never happen, it must be handled in the previous condition (has_marker_in_this_sequence == false && has_marker_in_next_sample == true)
			logte("track(%u) - Too late, marker is included in the samples time range : sequence(%" PRId64 "), sample(%" PRId64 " - %" PRId64 ")", GetMediaTrack()->GetId(), last_sequence_number, samples->GetStartTimestamp(), samples->GetEndTimestamp());
		}

		if (marker_handling != kNoMarker)
		{
			logtt("track(%u) - Marker handling : %s, has marker in this sequence(%d), next sample(%d), current samples(%d)", GetMediaTrack()->GetId(), marker_handling_desc.CStr(), has_marker_in_this_sequence, has_marker_in_next_sample, has_marker_in_curr_samples);
		}

		if (samples != nullptr && samples->GetTotalCount() > 0)
		{
			// If the CUE-OUT/IN event is included in the samples time range, flush the samples as soon as possible.
			// samples->GetStartTimestamp() <= CUE events < samples->GetEndTimestamp()
			if (marker_handling == kFlushAsSoonAsPossible)
			{
				auto marker = GetFirstMarker();
				if (marker == nullptr)
				{
					// Never reach here
					logtc("track(%d) - Marker handling : Flush as soon as possible, but marker is null", GetMediaTrack()->GetId());
					return false;
				}

				logtt("track(%u) - Force segment flush, has marker (start: %" PRId64 ", marker:%" PRId64 " (%s) end: %" PRId64 ")", GetMediaTrack()->GetId(), samples->GetStartTimestamp(), marker->GetTimestamp(), marker->GetTag().CStr(), samples->GetEndTimestamp());

				if (marker->IsOutOfNetwork() == true)
				{
					// If a CUE-OUT marker is included, flush the samples immediately. This may cause the next segment to start with a non-keyframe, but it will be replaced by a new segment through another ad-insertion solution.
					marker_handling = kShouldFlushImmediately;
					logti("track(%u) - Force segment flush immediately, cue-out marker : sample duration (%f)", GetMediaTrack()->GetId(), samples->GetTotalDuration());
				}
			}

			// https://datatracker.ietf.org/doc/html/draft-pantos-hls-rfc8216bis#section-4.4.3.8
			// The duration of a Partial Segment MUST be less than or equal to the Part Target Duration.  
			// The duration of each Partial Segment MUST be at least 85% of the Part Target Duration, 
			// with the exception of Partial Segments with the INDEPENDENT=YES attribute 
			// and the final Partial Segment of any Parent Segment.
			double last_segment_duration = 0.0; 
			if (last_segment != nullptr && last_segment->IsCompleted() == false)
			{
				last_segment_duration = last_segment->GetDurationMs();
			}
			bool can_be_last_chunk = false;
			// https://datatracker.ietf.org/doc/html/draft-pantos-hls-rfc8216bis#section-4.4.4.9
			// The duration of a Partial Segment MUST be less than or equal to the
			// Part Target Duration.  The duration of each Partial Segment MUST be
			// at least 85% of the Part Target Duration, with the exception of
			// Partial Segments with the INDEPENDENT=YES attribute and the final
			// Partial Segment of any Parent Segment.
			if (	(marker_handling != kShouldDeferSegmentFlush) && // Wait marker
					
					(
						(total_sample_duration_ms + last_segment_duration >= _storage->GetTargetSegmentDuration()) ||
						// Video && next_frame_is_idr && force_segment_flush
						(GetMediaTrack()->GetMediaType() == cmn::MediaType::Video && next_frame_is_idr && marker_handling == kFlushAsSoonAsPossible) || 
						// Audio && force_segment_flush
						(GetMediaTrack()->GetMediaType() == cmn::MediaType::Audio && marker_handling == kFlushAsSoonAsPossible) ||
						// force segment flush immediately
						(marker_handling == kShouldFlushImmediately)
					)
				)
			{
				// Last partial segment
				can_be_last_chunk = true;
			}

			logtt("track(%d), total_sample_duration_ms: %lf, next_total_sample_duration_ms: %lf, target_chunk_duration_ms: %lf, next_frame_is_idr: %d, is_last_partial_segment: %d last_segment_duration: %lf, target_segment_duration: %f", GetMediaTrack()->GetId(), total_sample_duration_ms, next_total_sample_duration_ms, _target_chunk_duration_ms, next_frame_is_idr, can_be_last_chunk, last_segment != nullptr ? last_segment->GetDurationMs() : -1, _storage->GetTargetSegmentDuration());

			// - In the last partial segment, if the next frame is a keyframe, a segment is created immediately. This allows the segment to start with a keyframe.
			// - When adding samples, if the Part Target Duration is exceeded, a chunk is created immediately.
			// - If it exceeds 85% and the next sample is independent, a chunk is created. This makes the next chunk start independent.
			if (	(total_sample_duration_ms >= _target_chunk_duration_ms) ||
				
					(marker_handling == kShouldFlushImmediately) ||

					(can_be_last_chunk == true && GetMediaTrack()->GetMediaType() == cmn::MediaType::Video && next_frame_is_idr == true) ||
					(can_be_last_chunk == true && GetMediaTrack()->GetMediaType() == cmn::MediaType::Audio) ||
					
					((next_total_sample_duration_ms > _target_chunk_duration_ms) && (total_sample_duration_ms >= _target_chunk_duration_ms * 0.85)) 
				)
			{
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

				auto markers = PopMarkers(samples->GetStartTimestamp(), samples->GetEndTimestamp());

				////////////////////////////////////////////////////
				// Auto Insertion of Cue-In/SCTE35-IN Marker
				// It is moved to the upper layer (LLHLS Stream) to synchronize with all tracks
				////////////////////////////////////////////////////

				// if (markers.empty() == false)
				// {
				// 	// If the last marker is a cue-out marker, insert a cue-in marker automatically after duration of cue-out marker
				// 	auto last_pop_marker = markers.back();
				// 	auto next_marker = GetFirstMarker();
				// 	if (last_pop_marker != nullptr && last_pop_marker->IsOutOfNetwork() == true && next_marker == nullptr)
				// 	{
				// 		// If there is no next xxx-IN marker, insert a xxx-in marker automatically
				// 		if (last_pop_marker->GetMarkerFormat() == cmn::BitstreamFormat::CUE)
				// 		{
				// 			// Insert a CUE-IN marker
				// 			auto cue_out_event = last_pop_marker->GetCueEvent();
				// 			if (cue_out_event != nullptr)
				// 			{
				// 				auto duration_msec = cue_out_event->GetDurationMsec();
				// 				auto main_track = GetMediaTrack();
				// 				int64_t cue_in_timestamp = (samples->GetEndTimestamp() - 1) + (static_cast<double>(duration_msec) / 1000.0 * main_track->GetTimeBase().GetTimescale());

				// 				auto cue_in_marker = Marker::CreateMarker(cmn::BitstreamFormat::CUE, cue_in_timestamp, CueEvent::Create(CueEvent::CueType::IN, 0)->Serialize());

				// 				InsertMarker(cue_in_marker);
				// 			}
				// 		}
				// 		else if (last_pop_marker->GetMarkerFormat() == cmn::BitstreamFormat::SCTE35)
				// 		{
				// 			// Insert a SCTE35-IN marker
				// 			auto scte35_out_event = last_pop_marker->GetScte35Event();
				// 			if (scte35_out_event != nullptr)
				// 			{
				// 				auto duration_msec = scte35_out_event->GetDurationMsec();
				// 				auto main_track = GetMediaTrack();
				// 				int64_t scte35_in_timestamp = (samples->GetEndTimestamp() - 1) + (static_cast<double>(duration_msec) / 1000.0 * main_track->GetTimeBase().GetTimescale());

				// 				auto scte_in_data = Scte35Event::Create(mpegts::SpliceCommandType::SPLICE_INSERT, scte35_out_event->GetID(), false, scte35_in_timestamp, duration_msec, false)->Serialize();

				// 				auto marker = Marker::CreateMarker(cmn::BitstreamFormat::SCTE35, scte35_in_timestamp, scte_in_data);

				// 				InsertMarker(marker);
				// 			}
				// 		}
				// 	}
				// }

				bool last_chunk = can_be_last_chunk && next_frame_is_idr;
				if (marker_handling == kShouldFlushImmediately)
				{
					last_chunk = true;
				}

				if (_storage != nullptr && _storage->AppendMediaChunk(chunk, 
												samples->GetStartTimestamp(), 
												total_sample_duration_ms, 
												samples->IsIndependent(), 
												last_chunk, 
												markers) == false)
				{
					logte("FMP4Packager::AppendSample() - Failed to store media chunk");
					return false;
				}

				_sample_buffer.Reset();

				// Set the average chunk duration to config.chunk_duration_ms
				// _target_chunk_duration_ms -= total_sample_duration_ms;
				// _target_chunk_duration_ms += _config.chunk_duration_ms;
			}
		}

		if (_sample_buffer.AppendSample(next_frame) == false)
		{
			logte("FMP4Packager::AppendSample() - Failed to append sample");
			return false;
		}

		samples = _sample_buffer.GetSamples();
		total_sample_duration = samples != nullptr ? samples->GetTotalDuration() : 0;
		total_sample_duration_ms = (static_cast<double>(total_sample_duration) / GetMediaTrack()->GetTimeBase().GetTimescale()) * 1000.0;

		_segmentation_info.last_sample_timestamp_ms = static_cast<double>(next_frame->GetDts()) / GetMediaTrack()->GetTimeBase().GetTimescale() * 1000.0;
		_segmentation_info.last_sample_duration_ms = static_cast<double>(next_frame->GetDuration()) / GetMediaTrack()->GetTimeBase().GetTimescale() * 1000.0;

		last_segment = std::static_pointer_cast<FMP4Segment>(_storage->GetLastSegment());

		if (last_segment != nullptr)
		{
			_segmentation_info.is_last_segment_completed = last_segment->IsCompleted();
			_segmentation_info.last_segment_number = last_segment->GetNumber();
			_segmentation_info.last_partial_segment_number = last_segment->GetLastPartialNumber();
			_segmentation_info.last_segement_duration_ms = total_sample_duration_ms;
			if (last_segment->IsCompleted() == false)
			{
				_segmentation_info.last_segement_duration_ms += last_segment->GetDurationMs();
			} 
			logtt("track(%u) - last_segment_number: %" PRId64 ", last_partial_segment_number: %" PRId64 ", last_segment_duration_ms: %f", GetMediaTrack()->GetId(), _segmentation_info.last_segment_number, _segmentation_info.last_partial_segment_number, _segmentation_info.last_segement_duration_ms);
		}

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

			if (_storage != nullptr && _storage->AppendMediaChunk(chunk, 
											samples->GetStartTimestamp(), 
											samples->GetTotalDuration(), 
											samples->IsIndependent(), true) == false)
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

	std::optional<MarkerBox::SegmentationInfo> FMP4Packager::GetSegmentationInfo() const
	{
		return _segmentation_info;
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
		stream.WriteText("iso6mp42avc1dashhlsfaid3"); // compatible brands

		// stream.WriteText("mp42"); // major brand
		// stream.WriteBE32(0); // minor version
		// stream.WriteText("isommp42iso5dash"); // compatible brands
		
		return WriteBox(data_stream, "ftyp", *stream.GetData());
	}
} // namespace bmff