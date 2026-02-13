//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/common_types.h>

#define MAX_RTP_RECORDS 1500

// https://tools.ietf.org/html/rfc5761#section-4
// - payload type values in the range 64-95 MUST NOT be used
// - dynamic RTP payload types SHOULD be chosen in the range 96-127 where possible
enum class FixedRtcPayloadType : uint8_t
{
	VP8_PAYLOAD_TYPE	  = 96,
	VP8_RTX_PAYLOAD_TYPE  = 97,
	H264_PAYLOAD_TYPE	  = 98,
	H264_RTX_PAYLOAD_TYPE = 99,
	H265_PAYLOAD_TYPE	  = 100,
	H265_RTX_PAYLOAD_TYPE = 101,
	OPUS_PAYLOAD_TYPE	  = 110,
	RED_PAYLOAD_TYPE	  = 120,
	RED_RTX_PAYLOAD_TYPE  = 121,
	ULPFEC_PAYLOAD_TYPE	  = 122
};

static cmn::MediaCodecId CodecIdFromPayloadType(FixedRtcPayloadType payload_type)
{
	switch (payload_type)
	{
		case FixedRtcPayloadType::VP8_PAYLOAD_TYPE:
			return cmn::MediaCodecId::Vp8;
		case FixedRtcPayloadType::VP8_RTX_PAYLOAD_TYPE:
			return cmn::MediaCodecId::Vp8;
		case FixedRtcPayloadType::H264_PAYLOAD_TYPE:
			return cmn::MediaCodecId::H264;
		case FixedRtcPayloadType::H264_RTX_PAYLOAD_TYPE:
			return cmn::MediaCodecId::H264;
		case FixedRtcPayloadType::H265_PAYLOAD_TYPE:
			return cmn::MediaCodecId::H265;
		case FixedRtcPayloadType::H265_RTX_PAYLOAD_TYPE:
			return cmn::MediaCodecId::H265;
		case FixedRtcPayloadType::OPUS_PAYLOAD_TYPE:
			return cmn::MediaCodecId::Opus;
		default:
			return cmn::MediaCodecId::None;
	}

	return cmn::MediaCodecId::None;
}

static cmn::MediaCodecId CodecIdFromPayloadTypeNumber(uint8_t payload_type)
{
	return CodecIdFromPayloadType(static_cast<FixedRtcPayloadType>(payload_type));
}

static uint8_t PayloadTypeFromCodecId(cmn::MediaCodecId codec_id)
{
	uint8_t payload_type = 0;

	switch (codec_id)
	{
		case cmn::MediaCodecId::Vp8:
			payload_type = static_cast<uint8_t>(FixedRtcPayloadType::VP8_PAYLOAD_TYPE);
			break;
		case cmn::MediaCodecId::H264:
			payload_type = static_cast<uint8_t>(FixedRtcPayloadType::H264_PAYLOAD_TYPE);
			break;
		case cmn::MediaCodecId::H265:
			payload_type = static_cast<uint8_t>(FixedRtcPayloadType::H265_PAYLOAD_TYPE);
			break;
		case cmn::MediaCodecId::Opus:
			payload_type = static_cast<uint8_t>(FixedRtcPayloadType::OPUS_PAYLOAD_TYPE);
			break;
		default:
			// No support codecs
			return 0;
	}

	return payload_type;
}

static uint8_t RtxPayloadTypeFromCodecId(cmn::MediaCodecId codec_id)
{
	uint8_t payload_type = 0;

	switch (codec_id)
	{
		case cmn::MediaCodecId::Vp8:
			payload_type = static_cast<uint8_t>(FixedRtcPayloadType::VP8_RTX_PAYLOAD_TYPE);
			break;
		case cmn::MediaCodecId::H264:
			payload_type = static_cast<uint8_t>(FixedRtcPayloadType::H264_RTX_PAYLOAD_TYPE);
			break;
		case cmn::MediaCodecId::H265:
			payload_type = static_cast<uint8_t>(FixedRtcPayloadType::H265_RTX_PAYLOAD_TYPE);
			break;
		default:
			// No support codecs
			return 0;
	}

	return payload_type;
}