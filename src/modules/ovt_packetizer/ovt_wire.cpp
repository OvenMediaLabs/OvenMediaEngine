//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "ovt_wire.h"

// OVT wire value table.
// This table is the reference for the protocol values, not the C++ enums:
// values are only ever appended, never reassigned or reordered.
// Every field is a plain value except `MediaPacketFlag`, whose byte `MH[29]` is a bitmask.
// A peer up to v0.21.0.0 reads that byte as a value,
// so a packet carrying a flag added after that release must not go to one.
// The full (enumerator, wire value) list is pinned by `ovt_wire_test.cpp`,
// and changing an existing row there is changing the protocol.
//
// The `ToOvtWire()` switches have no `default:` on purpose and this file is compiled with `-Werror=switch`,
// so a new enumerator does not build until it is given a wire value here.
// The `FromOvtWire()` switches must keep `default: return std::nullopt;`:
// unknown bytes are allowed on the receive side.

namespace ovt
{
	namespace
	{
		// The byte that carries "no value": `Unknown`, or `None` for a sample format.
		// Every such enumerator is -1, which OVT1 wrote through `static_cast<int8_t>`.
		constexpr uint8_t WIRE_NO_VALUE = 0xFF;
	}  // namespace

	uint8_t ToOvtWire(cmn::MediaType media_type)
	{
		switch (media_type)
		{
			case cmn::MediaType::Unknown:
				return WIRE_NO_VALUE;
			case cmn::MediaType::Video:
				return 0;
			case cmn::MediaType::Audio:
				return 1;
			case cmn::MediaType::Data:
				return 2;
			case cmn::MediaType::Subtitle:
				return 3;
			case cmn::MediaType::Attachment:
				return 4;
			// End marker, never sent. Listed only so the switch stays exhaustive.
			case cmn::MediaType::Nb:
				return 5;
		}

		return WIRE_NO_VALUE;
	}

	// `MH[29]` is read bit by bit, so a new flag takes the next free bit (`0x04`), not the next integer:
	// a value that carries an existing bit reads as that flag on every build already deployed.
	uint8_t ToOvtWire(MediaPacketFlag flag)
	{
		switch (flag)
		{
			case MediaPacketFlag::Unknown:
				return 0;
			case MediaPacketFlag::NoFlag:
				return 1;
			case MediaPacketFlag::Key:
				return 2;
		}

		return 0;
	}

	uint8_t ToOvtWire(cmn::BitstreamFormat format)
	{
		switch (format)
		{
			case cmn::BitstreamFormat::Unknown:
				return WIRE_NO_VALUE;
			case cmn::BitstreamFormat::H264_AVCC:
				return 0;
			case cmn::BitstreamFormat::H264_ANNEXB:
				return 1;
			case cmn::BitstreamFormat::H264_RTP_RFC_6184:
				return 2;
			case cmn::BitstreamFormat::HVCC:
				return 3;
			case cmn::BitstreamFormat::H265_ANNEXB:
				return 4;
			case cmn::BitstreamFormat::H265_RTP_RFC_7798:
				return 5;
			case cmn::BitstreamFormat::VP8:
				return 6;
			case cmn::BitstreamFormat::VP8_RTP_RFC_7741:
				return 7;
			case cmn::BitstreamFormat::AAC_RAW:
				return 8;
			case cmn::BitstreamFormat::AAC_MPEG4_GENERIC:
				return 9;
			case cmn::BitstreamFormat::AAC_ADTS:
				return 10;
			case cmn::BitstreamFormat::AAC_LATM:
				return 11;
			case cmn::BitstreamFormat::OPUS:
				return 12;
			case cmn::BitstreamFormat::OPUS_RTP_RFC_7587:
				return 13;
			case cmn::BitstreamFormat::MP3:
				return 14;
			case cmn::BitstreamFormat::JPEG:
				return 15;
			case cmn::BitstreamFormat::PNG:
				return 16;
			case cmn::BitstreamFormat::WEBP:
				return 17;
			case cmn::BitstreamFormat::ID3v2:
				return 18;
			case cmn::BitstreamFormat::OVEN_EVENT:
				return 19;
			case cmn::BitstreamFormat::CUE:
				return 20;
			case cmn::BitstreamFormat::AMF:
				return 21;
			case cmn::BitstreamFormat::SEI:
				return 22;
			case cmn::BitstreamFormat::SCTE35:
				return 23;
			case cmn::BitstreamFormat::WebVTT:
				return 24;
			case cmn::BitstreamFormat::MP2:
				return 25;
			case cmn::BitstreamFormat::AV1_OBU:
				return 26;
			case cmn::BitstreamFormat::AV1_RTP_AOM:
				return 27;
			// Released with v0.21.0.0
			case cmn::BitstreamFormat::AVIF:
				return 28;
		}

		return WIRE_NO_VALUE;
	}

	uint8_t ToOvtWire(cmn::PacketType packet_type)
	{
		switch (packet_type)
		{
			case cmn::PacketType::Unknown:
				return WIRE_NO_VALUE;
			case cmn::PacketType::OVT:
				return 0;
			case cmn::PacketType::RAW:
				return 1;
			case cmn::PacketType::SEQUENCE_HEADER:
				return 2;
			case cmn::PacketType::NALU:
				return 3;
			case cmn::PacketType::EVENT:
				return 4;
			case cmn::PacketType::VIDEO_EVENT:
				return 5;
			case cmn::PacketType::AUDIO_EVENT:
				return 6;
		}

		return WIRE_NO_VALUE;
	}

	uint8_t ToOvtWire(cmn::MediaCodecId codec_id)
	{
		switch (codec_id)
		{
			case cmn::MediaCodecId::None:
				return 0;
			case cmn::MediaCodecId::H264:
				return 1;
			case cmn::MediaCodecId::H265:
				return 2;
			case cmn::MediaCodecId::Vp8:
				return 3;
			case cmn::MediaCodecId::Vp9:
				return 4;
			case cmn::MediaCodecId::Av1:
				return 5;
			case cmn::MediaCodecId::Flv:
				return 6;
			case cmn::MediaCodecId::Aac:
				return 7;
			case cmn::MediaCodecId::Mp3:
				return 8;
			case cmn::MediaCodecId::Opus:
				return 9;
			case cmn::MediaCodecId::Jpeg:
				return 10;
			case cmn::MediaCodecId::Png:
				return 11;
			case cmn::MediaCodecId::Webp:
				return 12;
			case cmn::MediaCodecId::WebVTT:
				return 13;
			case cmn::MediaCodecId::Whisper:
				return 14;
			case cmn::MediaCodecId::Mp2:
				return 15;
			// Released with v0.21.0.0
			case cmn::MediaCodecId::Avif:
				return 16;
		}

		return 0;
	}

	uint8_t ToOvtWire(cmn::AudioSample::Format format)
	{
		switch (format)
		{
			case cmn::AudioSample::Format::None:
				return WIRE_NO_VALUE;
			case cmn::AudioSample::Format::U8:
				return 0;
			case cmn::AudioSample::Format::S16:
				return 1;
			case cmn::AudioSample::Format::S32:
				return 2;
			case cmn::AudioSample::Format::Flt:
				return 3;
			case cmn::AudioSample::Format::Dbl:
				return 4;
			case cmn::AudioSample::Format::U8P:
				return 5;
			case cmn::AudioSample::Format::S16P:
				return 6;
			case cmn::AudioSample::Format::S32P:
				return 7;
			case cmn::AudioSample::Format::FltP:
				return 8;
			case cmn::AudioSample::Format::DblP:
				return 9;
			// End marker, never sent. Listed only so the switch stays exhaustive.
			case cmn::AudioSample::Format::Nb:
				return 10;
		}

		return WIRE_NO_VALUE;
	}

	template <>
	std::optional<cmn::MediaType> FromOvtWire<cmn::MediaType>(uint8_t wire)
	{
		switch (wire)
		{
			case WIRE_NO_VALUE:
				return cmn::MediaType::Unknown;
			case 0:
				return cmn::MediaType::Video;
			case 1:
				return cmn::MediaType::Audio;
			case 2:
				return cmn::MediaType::Data;
			case 3:
				return cmn::MediaType::Subtitle;
			case 4:
				return cmn::MediaType::Attachment;
			default:
				return std::nullopt;
		}
	}

	// `MH[29]` is the one bitmask byte, so it is read bit by bit instead of as a value.
	// A bit this build does not know is dropped, so an unknown bit is safe to ignore,
	// and `Key` wins when both known bits are set.
	// `0x00` is `Unknown`'s own wire value and maps to it; `nullopt` is for a non-zero byte with no known bit,
	// which keeps that byte on the packet so a relay re-sends it unchanged.
	template <>
	std::optional<MediaPacketFlag> FromOvtWire<MediaPacketFlag>(uint8_t wire)
	{
		if ((wire & ToOvtWire(MediaPacketFlag::Key)) != 0)
		{
			return MediaPacketFlag::Key;
		}

		if ((wire & ToOvtWire(MediaPacketFlag::NoFlag)) != 0)
		{
			return MediaPacketFlag::NoFlag;
		}

		if (wire == ToOvtWire(MediaPacketFlag::Unknown))
		{
			return MediaPacketFlag::Unknown;
		}

		return std::nullopt;
	}

	template <>
	std::optional<cmn::BitstreamFormat> FromOvtWire<cmn::BitstreamFormat>(uint8_t wire)
	{
		switch (wire)
		{
			case WIRE_NO_VALUE:
				return cmn::BitstreamFormat::Unknown;
			case 0:
				return cmn::BitstreamFormat::H264_AVCC;
			case 1:
				return cmn::BitstreamFormat::H264_ANNEXB;
			case 2:
				return cmn::BitstreamFormat::H264_RTP_RFC_6184;
			case 3:
				return cmn::BitstreamFormat::HVCC;
			case 4:
				return cmn::BitstreamFormat::H265_ANNEXB;
			case 5:
				return cmn::BitstreamFormat::H265_RTP_RFC_7798;
			case 6:
				return cmn::BitstreamFormat::VP8;
			case 7:
				return cmn::BitstreamFormat::VP8_RTP_RFC_7741;
			case 8:
				return cmn::BitstreamFormat::AAC_RAW;
			case 9:
				return cmn::BitstreamFormat::AAC_MPEG4_GENERIC;
			case 10:
				return cmn::BitstreamFormat::AAC_ADTS;
			case 11:
				return cmn::BitstreamFormat::AAC_LATM;
			case 12:
				return cmn::BitstreamFormat::OPUS;
			case 13:
				return cmn::BitstreamFormat::OPUS_RTP_RFC_7587;
			case 14:
				return cmn::BitstreamFormat::MP3;
			case 15:
				return cmn::BitstreamFormat::JPEG;
			case 16:
				return cmn::BitstreamFormat::PNG;
			case 17:
				return cmn::BitstreamFormat::WEBP;
			case 18:
				return cmn::BitstreamFormat::ID3v2;
			case 19:
				return cmn::BitstreamFormat::OVEN_EVENT;
			case 20:
				return cmn::BitstreamFormat::CUE;
			case 21:
				return cmn::BitstreamFormat::AMF;
			case 22:
				return cmn::BitstreamFormat::SEI;
			case 23:
				return cmn::BitstreamFormat::SCTE35;
			case 24:
				return cmn::BitstreamFormat::WebVTT;
			case 25:
				return cmn::BitstreamFormat::MP2;
			case 26:
				return cmn::BitstreamFormat::AV1_OBU;
			case 27:
				return cmn::BitstreamFormat::AV1_RTP_AOM;
			case 28:
				return cmn::BitstreamFormat::AVIF;
			default:
				return std::nullopt;
		}
	}

	template <>
	std::optional<cmn::PacketType> FromOvtWire<cmn::PacketType>(uint8_t wire)
	{
		switch (wire)
		{
			case WIRE_NO_VALUE:
				return cmn::PacketType::Unknown;
			case 0:
				return cmn::PacketType::OVT;
			case 1:
				return cmn::PacketType::RAW;
			case 2:
				return cmn::PacketType::SEQUENCE_HEADER;
			case 3:
				return cmn::PacketType::NALU;
			case 4:
				return cmn::PacketType::EVENT;
			case 5:
				return cmn::PacketType::VIDEO_EVENT;
			case 6:
				return cmn::PacketType::AUDIO_EVENT;
			default:
				return std::nullopt;
		}
	}

	template <>
	std::optional<cmn::MediaCodecId> FromOvtWire<cmn::MediaCodecId>(uint8_t wire)
	{
		switch (wire)
		{
			case 0:
				return cmn::MediaCodecId::None;
			case 1:
				return cmn::MediaCodecId::H264;
			case 2:
				return cmn::MediaCodecId::H265;
			case 3:
				return cmn::MediaCodecId::Vp8;
			case 4:
				return cmn::MediaCodecId::Vp9;
			case 5:
				return cmn::MediaCodecId::Av1;
			case 6:
				return cmn::MediaCodecId::Flv;
			case 7:
				return cmn::MediaCodecId::Aac;
			case 8:
				return cmn::MediaCodecId::Mp3;
			case 9:
				return cmn::MediaCodecId::Opus;
			case 10:
				return cmn::MediaCodecId::Jpeg;
			case 11:
				return cmn::MediaCodecId::Png;
			case 12:
				return cmn::MediaCodecId::Webp;
			case 13:
				return cmn::MediaCodecId::WebVTT;
			case 14:
				return cmn::MediaCodecId::Whisper;
			case 15:
				return cmn::MediaCodecId::Mp2;
			case 16:
				return cmn::MediaCodecId::Avif;
			default:
				return std::nullopt;
		}
	}

	template <>
	std::optional<cmn::AudioSample::Format> FromOvtWire<cmn::AudioSample::Format>(uint8_t wire)
	{
		switch (wire)
		{
			case WIRE_NO_VALUE:
				return cmn::AudioSample::Format::None;
			case 0:
				return cmn::AudioSample::Format::U8;
			case 1:
				return cmn::AudioSample::Format::S16;
			case 2:
				return cmn::AudioSample::Format::S32;
			case 3:
				return cmn::AudioSample::Format::Flt;
			case 4:
				return cmn::AudioSample::Format::Dbl;
			case 5:
				return cmn::AudioSample::Format::U8P;
			case 6:
				return cmn::AudioSample::Format::S16P;
			case 7:
				return cmn::AudioSample::Format::S32P;
			case 8:
				return cmn::AudioSample::Format::FltP;
			case 9:
				return cmn::AudioSample::Format::DblP;
			default:
				return std::nullopt;
		}
	}

	template <>
	std::optional<OvtPayloadType> FromOvtWire<OvtPayloadType>(uint8_t wire)
	{
		switch (wire)
		{
			case 10:
				return OvtPayloadType::MessageRequest;
			case 20:
				return OvtPayloadType::MessageResponse;
			case 30:
				return OvtPayloadType::MediaPacket;
			case 40:
				return OvtPayloadType::Required;
			default:
				return std::nullopt;
		}
	}

	// No `default:` here either: a new codec must say whether v0.21.0.0 already had its value.
	bool HasLegacyWireValue(cmn::MediaCodecId codec_id)
	{
		switch (codec_id)
		{
			case cmn::MediaCodecId::None:
				[[fallthrough]];
			case cmn::MediaCodecId::H264:
				[[fallthrough]];
			case cmn::MediaCodecId::H265:
				[[fallthrough]];
			case cmn::MediaCodecId::Vp8:
				[[fallthrough]];
			case cmn::MediaCodecId::Vp9:
				[[fallthrough]];
			case cmn::MediaCodecId::Av1:
				[[fallthrough]];
			case cmn::MediaCodecId::Flv:
				[[fallthrough]];
			case cmn::MediaCodecId::Aac:
				[[fallthrough]];
			case cmn::MediaCodecId::Mp3:
				[[fallthrough]];
			case cmn::MediaCodecId::Opus:
				[[fallthrough]];
			case cmn::MediaCodecId::Jpeg:
				[[fallthrough]];
			case cmn::MediaCodecId::Png:
				[[fallthrough]];
			case cmn::MediaCodecId::Webp:
				[[fallthrough]];
			case cmn::MediaCodecId::WebVTT:
				[[fallthrough]];
			case cmn::MediaCodecId::Whisper:
				[[fallthrough]];
			case cmn::MediaCodecId::Mp2:
				[[fallthrough]];
			case cmn::MediaCodecId::Avif:
				return true;
		}

		return false;
	}

	bool HasLegacyWireValue(cmn::BitstreamFormat format)
	{
		switch (format)
		{
			case cmn::BitstreamFormat::Unknown:
				[[fallthrough]];
			case cmn::BitstreamFormat::H264_AVCC:
				[[fallthrough]];
			case cmn::BitstreamFormat::H264_ANNEXB:
				[[fallthrough]];
			case cmn::BitstreamFormat::H264_RTP_RFC_6184:
				[[fallthrough]];
			case cmn::BitstreamFormat::HVCC:
				[[fallthrough]];
			case cmn::BitstreamFormat::H265_ANNEXB:
				[[fallthrough]];
			case cmn::BitstreamFormat::H265_RTP_RFC_7798:
				[[fallthrough]];
			case cmn::BitstreamFormat::VP8:
				[[fallthrough]];
			case cmn::BitstreamFormat::VP8_RTP_RFC_7741:
				[[fallthrough]];
			case cmn::BitstreamFormat::AAC_RAW:
				[[fallthrough]];
			case cmn::BitstreamFormat::AAC_MPEG4_GENERIC:
				[[fallthrough]];
			case cmn::BitstreamFormat::AAC_ADTS:
				[[fallthrough]];
			case cmn::BitstreamFormat::AAC_LATM:
				[[fallthrough]];
			case cmn::BitstreamFormat::OPUS:
				[[fallthrough]];
			case cmn::BitstreamFormat::OPUS_RTP_RFC_7587:
				[[fallthrough]];
			case cmn::BitstreamFormat::MP3:
				[[fallthrough]];
			case cmn::BitstreamFormat::JPEG:
				[[fallthrough]];
			case cmn::BitstreamFormat::PNG:
				[[fallthrough]];
			case cmn::BitstreamFormat::WEBP:
				[[fallthrough]];
			case cmn::BitstreamFormat::ID3v2:
				[[fallthrough]];
			case cmn::BitstreamFormat::OVEN_EVENT:
				[[fallthrough]];
			case cmn::BitstreamFormat::CUE:
				[[fallthrough]];
			case cmn::BitstreamFormat::AMF:
				[[fallthrough]];
			case cmn::BitstreamFormat::SEI:
				[[fallthrough]];
			case cmn::BitstreamFormat::SCTE35:
				[[fallthrough]];
			case cmn::BitstreamFormat::WebVTT:
				[[fallthrough]];
			case cmn::BitstreamFormat::MP2:
				[[fallthrough]];
			case cmn::BitstreamFormat::AV1_OBU:
				[[fallthrough]];
			case cmn::BitstreamFormat::AV1_RTP_AOM:
				[[fallthrough]];
			case cmn::BitstreamFormat::AVIF:
				return true;
		}

		return false;
	}

	bool HasLegacyWireValue(cmn::PacketType packet_type)
	{
		switch (packet_type)
		{
			case cmn::PacketType::Unknown:
				[[fallthrough]];
			case cmn::PacketType::OVT:
				[[fallthrough]];
			case cmn::PacketType::RAW:
				[[fallthrough]];
			case cmn::PacketType::SEQUENCE_HEADER:
				[[fallthrough]];
			case cmn::PacketType::NALU:
				[[fallthrough]];
			case cmn::PacketType::EVENT:
				[[fallthrough]];
			case cmn::PacketType::VIDEO_EVENT:
				[[fallthrough]];
			case cmn::PacketType::AUDIO_EVENT:
				return true;
		}

		return false;
	}

	// No `default:` here either: a new codec must state its transport format (or its absence).
	std::optional<cmn::BitstreamFormat> TransportBitstreamFormat(cmn::MediaCodecId codec_id)
	{
		switch (codec_id)
		{
			case cmn::MediaCodecId::H264:
				return cmn::BitstreamFormat::H264_ANNEXB;
			case cmn::MediaCodecId::H265:
				return cmn::BitstreamFormat::H265_ANNEXB;
			case cmn::MediaCodecId::Aac:
				return cmn::BitstreamFormat::AAC_ADTS;
			case cmn::MediaCodecId::Av1:
				return cmn::BitstreamFormat::AV1_OBU;
			case cmn::MediaCodecId::Vp8:
				return cmn::BitstreamFormat::VP8;
			case cmn::MediaCodecId::Opus:
				return cmn::BitstreamFormat::OPUS;
			case cmn::MediaCodecId::Mp3:
				return cmn::BitstreamFormat::MP3;
			case cmn::MediaCodecId::Mp2:
				return cmn::BitstreamFormat::MP2;
			case cmn::MediaCodecId::Jpeg:
				return cmn::BitstreamFormat::JPEG;
			case cmn::MediaCodecId::Png:
				return cmn::BitstreamFormat::PNG;
			case cmn::MediaCodecId::Webp:
				return cmn::BitstreamFormat::WEBP;
			case cmn::MediaCodecId::Avif:
				return cmn::BitstreamFormat::AVIF;
			case cmn::MediaCodecId::WebVTT:
				return cmn::BitstreamFormat::WebVTT;
			case cmn::MediaCodecId::None:
				[[fallthrough]];
			case cmn::MediaCodecId::Vp9:
				[[fallthrough]];
			case cmn::MediaCodecId::Flv:
				[[fallthrough]];
			case cmn::MediaCodecId::Whisper:
				return std::nullopt;
		}

		return std::nullopt;
	}
}  // namespace ovt
