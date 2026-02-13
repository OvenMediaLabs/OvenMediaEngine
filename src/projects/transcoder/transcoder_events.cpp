//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================

#include "transcoder_events.h"

#include <modules/bitstream/nalu/nal_header.h>
#include <modules/bitstream/nalu/nal_unit_insertor.h>
#include <modules/bitstream/h264/h264_sei.h>
#include "transcoder_private.h"



TranscoderEvents::TranscoderEvents()
{
}

TranscoderEvents::~TranscoderEvents()
{
}

// If the event is saved, return true and allow the packet to be discarded
bool TranscoderEvents::PushEvent(std::shared_ptr<info::Stream> &stream, std::shared_ptr<MediaPacket> &packet)
{
	if (packet->GetBitstreamFormat() == cmn::BitstreamFormat::SEI)
	{
		for (auto &[output_track_id, track] : stream->GetTracks())
		{
			auto found = (track->GetCodecId() == cmn::MediaCodecId::H264);
						// Not implemented
						// || track->GetCodecId() == cmn::MediaCodecId::H265); 
			if (!found)
			{
				continue;
			}

			_event_packets[output_track_id].push_back(packet);
		}

		// Discard event packet
		return true;
	}

	// If the event is not saved, return false and allow the packet to be sent
	return false;
}

// If the stored event matches the packet, insert the event.
void TranscoderEvents::RunEvent(std::shared_ptr<info::Stream> &stream, std::shared_ptr<MediaPacket> &packet)
{
	auto event_packets = _event_packets.find(packet->GetTrackId());
	if (event_packets != _event_packets.end())
	{
		auto is_inserted = false;
		auto &event_list = event_packets->second;
		auto it = event_list.begin();
		while (it != event_list.end())
		{
			auto event_packet = *it;

			// If the KeyFrame flag of the event packet is True, the target for inserting the event should also be KeyFrame packet
			auto require_keyframe = event_packet->IsKeyFrame();
			if (require_keyframe == true && packet->IsKeyFrame() == false)
			{
				it++;
				continue;
			}

			auto packet_time = (int64_t)(packet->GetDts() * stream->GetTrack(packet->GetTrackId())->GetTimeBase().GetExpr() * 1000);
			auto event_time = (int64_t)(event_packet->GetDts() * stream->GetTrack(event_packet->GetTrackId())->GetTimeBase().GetExpr() * 1000);
			if (event_time > packet_time)
			{
				it++;
				continue;
			}

			// Prevent inserting multiple events into the same packet
			if(is_inserted)
			{
				it = event_list.erase(it);
				continue;
			}

			if (UpdateEventPacket(event_packet) == false)
			{
				it = event_list.erase(it);
				continue;
			}

			auto nalu = std::make_shared<ov::Data>();
			if (nalu == nullptr)
			{
				it = event_list.erase(it);
				continue;
			}

			auto track = stream->GetTrack(packet->GetTrackId());
			if (track == nullptr)
			{
				it = event_list.erase(it);
				continue;
			}

			// Append NAL Header
			auto codec_id = track->GetCodecId();
			if (codec_id == cmn::MediaCodecId::H264)
			{
				nalu->Append(NalHeader::CreateH264(6));
			}
			else if (codec_id == cmn::MediaCodecId::H265)
			{
				// Not implemented
				// nalu->Append(NalHeader::CreateH265(35, 0, 0));
			}

			// Append NAL Payload
			nalu->Append(event_packet->GetData());

			// Insert NAL into the packet
			auto new_data = NalUnitInsertor::Insert(packet->GetData(), nalu, packet->GetBitstreamFormat());
			if (new_data.has_value())
			{
				packet->SetData(std::get<0>(new_data.value()));
				packet->SetFragHeader(std::get<1>(new_data.value()).get());
				is_inserted = true;

				logtt("Inserted NALU. stream(%s), track(%u), cur.ts(%lldms), req.ts(%lldms), key(%s)",
					  stream->GetName().CStr(), packet->GetTrackId(), packet_time, event_time, packet->IsKeyFrame() ? "key" : "non-key");
			}
			else
			{
				logte("Could not insert NALU");
			}

			// Remove used event packet
			it = event_list.erase(it);
		}
	}
}

bool TranscoderEvents::UpdateEventPacket(std::shared_ptr<MediaPacket> &event_packet)
{
	// Change SEI Payload TimeCode to CurrentTime. 
	if (event_packet->GetBitstreamFormat() == cmn::BitstreamFormat::SEI)
	{
		auto sei = H264SEI::Parse(event_packet->GetData());
		if (sei == nullptr)
		{
			logte("Failed to parse SEI");
			return false;
		}

		[[maybe_unused]]  auto previous_time = sei->GetPayloadTimeCode();
		auto current_time = ov::Time::GetTimestampInMs();
		sei->SetPayloadTimeCode(current_time);
		
		// Replace ${EpochTime} with current epoch time
		if (sei->GetPayloadData() != nullptr && sei->GetPayloadData()->GetLength() > 0)
		{
			ov::String payload_data(sei->GetPayloadData()->GetDataAs<char>(), sei->GetPayloadData()->GetLength());
			payload_data = payload_data.Replace("${EpochTime}", ov::String::FormatString("%lld", current_time));
			sei->SetPayloadData(payload_data.ToData());
		}

		auto new_paket_data = sei->Serialize();
		event_packet->SetData(new_paket_data);
	}

	return true;
}