#pragma once

#include <base/info/media_track.h>
#include "rtp_packet.h"

// Stateless helper that stamps frame boundary flags on an incoming RTP packet.
//   - With the Dependency Descriptor present, the S/E bits give the
//     authoritative frame start (IsFirstPacketOfFrame) and end.
//   - Without it, the codec-specific payload header only reveals NAL/unit
//     starts (IsStartOfUnit); the jitter buffer derives the frame start from
//     the lowest one. End-of-frame defaults to the RTP marker bit.
//
//   - Packets are also stamped as keyframe (IsKeyframe) from the codec payload
//     header, with or without DD. For H.264/H.265 that is any packet carrying
//     an IDR/IRAP NAL or the first fragment of one, so the mark survives a
//     lost STAP-A start; for VP8 (P bit) and AV1 (N bit) only the frame's
//     first packet carries it. Parameter sets alone do not count.
//
// Returns false when the packet cannot be parsed (e.g. truncated payload,
// reserved/invalid nal type). The caller is expected to drop such packets.
class RtpFrameBoundaryDetector
{
public:
	// dd_extension_id == 0 means DD was not negotiated; codec parse is used.
	static bool Apply(RtpPacket &packet, cmn::MediaCodecId codec, uint8_t dd_extension_id);

private:
	static bool TryDependencyDescriptor(RtpPacket &packet, uint8_t dd_extension_id);
	static bool ApplyH264(RtpPacket &packet);
	static bool ApplyH265(RtpPacket &packet);
	static bool ApplyVp8(RtpPacket &packet);
	static bool ApplyAv1(RtpPacket &packet);

	static bool IsKeyframeStart(const RtpPacket &packet, cmn::MediaCodecId codec);
	static bool IsH264KeyframeStart(const uint8_t *payload, size_t size);
	static bool IsH265KeyframeStart(const uint8_t *payload, size_t size);
	static bool IsVp8KeyframeStart(const uint8_t *payload, size_t size);
	static bool IsAv1KeyframeStart(const uint8_t *payload);
};
