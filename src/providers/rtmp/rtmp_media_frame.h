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

namespace pvd::rtmp
{
	// A codec description is not media, so it must not end the wait for an input's first frame.
	// An encoder can send the sequence header as soon as it has initialised, long before its first frame:
	// ffmpeg at 1 fps sends it within 100 ms of `publish` and its first frame 65 s later.
	//
	// These read the FLV tag header directly rather than going through `flv::VideoData::Parse()`
	// or `flv::AudioData::Parse()`.
	// That parser rejects every video codec this provider cannot service and logs an error for it,
	// and it reads the audio packet type byte without checking that the sound format has one.
	// Only a layout known to carry a description is excluded here; anything else counts as a frame,
	// which is what the provider did for every media message before the wait existed.

	inline bool CarriesVideoFrame(const std::shared_ptr<const ov::Data> &payload)
	{
		if ((payload == nullptr) || (payload->GetLength() < 2))
		{
			return true;
		}

		auto header = payload->GetDataAs<uint8_t>();

		// An enhanced video tag header carries `videoPacketType` in the low nibble instead of a codec id.
		// The legacy provider cannot service those codecs at all and drops the connection on the first
		// one, so there is nothing to gain from telling their descriptions apart here.
		if (OV_CHECK_FLAG(header[0], 0x80))
		{
			return true;
		}

		if ((header[0] & 0x0F) != ov::ToUnderlyingType(flv::VideoCodecId::AVC))
		{
			return true;
		}

		return header[1] != ov::ToUnderlyingType(flv::AvcPacketType::SequenceHeader);
	}

	inline bool CarriesAudioFrame(const std::shared_ptr<const ov::Data> &payload)
	{
		// An audio message with no tag header at all is RTMP's silence message, not a description.
		if ((payload == nullptr) || (payload->GetLength() < 2))
		{
			return true;
		}

		const auto *header = payload->GetDataAs<uint8_t>();

		// The packet type byte exists only for AAC. For every other sound format that byte is already
		// media data, so reading it as a packet type would decide this at random.
		if ((header[0] >> 4) != ov::ToUnderlyingType(flv::SoundFormat::AAC))
		{
			return true;
		}

		return header[1] != ov::ToUnderlyingType(flv::AACPacketType::SequenceHeader);
	}
}  // namespace pvd::rtmp
