//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

namespace pvd::rtmp
{
	// control(1) + sequence(1) + offsettime(3)
	static constexpr const size_t VIDEO_DATA_MIN_SIZE					= (sizeof(uint8_t) + sizeof(uint8_t) + (sizeof(uint8_t) * 3));
	// control(1) + sequence(1)
	static constexpr const size_t AUDIO_DATA_MIN_SIZE					= (sizeof(uint8_t) + sizeof(uint8_t));

	static constexpr const size_t DEFAULT_CHUNK_SIZE					= 128;
	static constexpr const size_t DEFAULT_ACKNOWNLEDGEMENT_SIZE			= 2500000;
	static constexpr const size_t DEFAULT_PEER_BANDWIDTH				= 2500000;

	// The maximum number of messages to wait for other tracks when the stream is temporarily delayed
	// even though there is audio or video track information in the metadata.
	static constexpr const size_t MAX_PACKET_COUNT_TO_WAIT_OTHER_TRACKS = 500;

	// The maximum number of packets to wait for receiving a sequence header.
	// If this number is exceeded, the track will be ignored.
	static constexpr const size_t MAX_PACKET_COUNT_BEFORE_SEQ_HEADER	= 10;

	// Used for legacy RTMP streams
	// TODO(dimiden): Assigning a sufficiently large temporary value to avoid `track_id` conflicts.
	// Needs to be changed later.
	static constexpr const uint32_t TRACK_ID_FOR_VIDEO					= 100;
	static constexpr const uint32_t TRACK_ID_FOR_AUDIO					= 200;
	static constexpr const uint32_t TRACK_ID_FOR_DATA					= 300;

	enum class EncoderType : int32_t
	{
		Custom = 0,	 // General Rtmp client (Client that cannot be distinguished from others)
		XSplit,		 // XSplit
		OBS,		 // OBS
		Lavf,		 // Libavformat (lavf)
	};

	constexpr EncoderType ToEncoderType(const char *name)
	{
		if (ov::cexpr::IndexOf(name, "Open Broadcaster") >= 0)
		{
			return EncoderType::OBS;
		}

		if (ov::cexpr::IndexOf(name, "obs-output") >= 0)
		{
			return EncoderType::OBS;
		}

		if (ov::cexpr::IndexOf(name, "XSplitBroadcaster") >= 0)
		{
			return EncoderType::XSplit;
		}

		if (ov::cexpr::IndexOf(name, "Lavf") >= 0)
		{
			return EncoderType::Lavf;
		}

		return EncoderType::Custom;
	}
	static_assert(ToEncoderType("Test") == EncoderType::Custom, "ToEncoderType() doesn't work properly");

	constexpr const char *EnumToString(const EncoderType encoder_type)
	{
		switch (encoder_type)
		{
			OV_CASE_RETURN(EncoderType::XSplit, "XSplit");
			OV_CASE_RETURN(EncoderType::OBS, "OBS");
			OV_CASE_RETURN(EncoderType::Lavf, "Lavf/FFmpeg");
			OV_CASE_RETURN(EncoderType::Custom, "Unknown");
		}

		return "Unknown";
	}
	static_assert(EnumToString(EncoderType::Custom) != nullptr, "ToString() doesn't work properly");
}  // namespace pvd::rtmp
