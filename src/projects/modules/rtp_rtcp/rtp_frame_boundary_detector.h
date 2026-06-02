#pragma once

#include <base/info/media_track.h>
#include "rtp_packet.h"

// Stateless helper that stamps IsFirstPacketOfFrame and IsLastPacketOfFrame
// on an incoming RTP packet using either the AV1 Dependency Descriptor RTP
// header extension (when negotiated) or the codec-specific RTP payload
// header. End-of-frame defaults to the RTP marker bit, refined by DD's E bit
// when present.
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
};
