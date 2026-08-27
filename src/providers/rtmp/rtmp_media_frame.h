//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <modules/containers/flv/flv_parser.h>
#include <modules/containers/flv_v2/flv_audio_data.h>
#include <modules/containers/flv_v2/flv_video_data.h>

namespace pvd::rtmp
{
	// Whether an RTMP message has a coded frame,
	// which is what ends `PushStream`'s wait for an input's first media.
	// A codec description does not: an encoder can send its sequence header long before its first frame.
	//
	// Only a layout known to hold a coded frame counts, because the two wrong answers differ in cost.
	// Taking a description for a frame ends the wait early and gets the channel deleted.
	// Taking a frame for a description keeps the wait running until the stream publishes.
	//
	// The legacy overloads read the tag header directly rather than through `flv::VideoData::Parse()`,
	// which logs an error for every video codec it does not service.

	inline bool HasVideoFrame(const std::shared_ptr<const ov::Data> &payload)
	{
		if ((payload == nullptr) || (payload->GetLength() < 2))
		{
			return false;
		}

		const auto *header = payload->GetDataAs<uint8_t>();

		// Bit 7 marks an enhanced tag header, whose codec is a FOURCC. This path services AVC only.
		if (OV_CHECK_FLAG(header[0], 0x80))
		{
			return false;
		}

		// A video info or command frame holds a command in place of a coded frame.
		if ((header[0] >> 4) == ov::ToUnderlyingType(flv::VideoFrameType::VideoInfoCommand))
		{
			return false;
		}

		if ((header[0] & 0x0F) != ov::ToUnderlyingType(flv::VideoCodecId::AVC))
		{
			return false;
		}

		// `SequenceHeader` describes the codec, and `EndOfSequence` has an empty body.
		return header[1] == ov::ToUnderlyingType(flv::AvcPacketType::NALU);
	}

	inline bool HasAudioFrame(const std::shared_ptr<const ov::Data> &payload)
	{
		if ((payload == nullptr) || (payload->GetLength() < 2))
		{
			return false;
		}

		const auto *header = payload->GetDataAs<uint8_t>();

		// This path services AAC only, and AAC is the one sound format whose byte 1 is a packet type.
		// Sound format 9 keeps an `AudioPacketType` in the low nibble and a FOURCC in byte 1,
		// and byte 1 is already media data for the rest.
		if ((header[0] >> 4) != ov::ToUnderlyingType(flv::SoundFormat::AAC))
		{
			return false;
		}

		return header[1] == ov::ToUnderlyingType(flv::AACPacketType::Raw);
	}

	// The E-RTMP overloads answer for one parsed track.
	// A multitrack message yields one of these per track, and the caller combines them.

	inline bool HasAudioFrame(const modules::flv::AudioData &data)
	{
		// The enhanced path fills `audio_packet_type`, and legacy AAC fills `aac_packet_type`.
		if (data.audio_packet_type.has_value())
		{
			return data.audio_packet_type.value() == modules::flv::AudioPacketType::CodedFrames;
		}

		if (data.sound_format == modules::flv::SoundFormat::Aac)
		{
			return data.aac_packet_type == modules::flv::AACPacketType::Raw;
		}

		return false;
	}

	inline bool HasVideoFrame(const modules::flv::VideoData &data)
	{
		if (data.video_frame_type == modules::flv::VideoFrameType::Command)
		{
			return false;
		}

		// The legacy AVC path reads its `AvcPacketType` byte into this field,
		// so `CodedFrames` covers an AVC NALU as well as an enhanced coded frame.
		switch (data.video_packet_type)
		{
			case modules::flv::VideoPacketType::CodedFrames:
				[[fallthrough]];
			case modules::flv::VideoPacketType::CodedFramesX:
				return true;

			default:
				return false;
		}
	}
}  // namespace pvd::rtmp
