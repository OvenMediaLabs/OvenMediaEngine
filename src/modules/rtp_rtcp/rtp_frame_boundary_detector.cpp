#include "rtp_frame_boundary_detector.h"

#include "rtp_header_extension/rtp_header_extension_dependency_descriptor.h"

#define OV_LOG_TAG "RtpFrameBoundary"

bool RtpFrameBoundaryDetector::Apply(RtpPacket &packet, cmn::MediaCodecId codec, uint8_t dd_extension_id)
{
	// Reject unsupported codecs up front (defensive — the receive pipeline
	// shouldn't have negotiated them in the first place).
	switch (codec)
	{
		case cmn::MediaCodecId::H264:
		case cmn::MediaCodecId::H265:
		case cmn::MediaCodecId::Vp8:
		case cmn::MediaCodecId::Av1:
			break;
		default:
			return false;
	}

	// Defaults: end-of-frame is the RTP marker bit. With DD present the S/E
	// bits give the authoritative frame start/end; without it the codec parse
	// only marks NAL/unit starts (StartOfUnit) and the jitter buffer derives
	// the frame start from the lowest one.
	packet.SetFirstPacketOfFrame(false);
	packet.SetStartOfUnit(false);
	packet.SetLastPacketOfFrame(packet.Marker());

	if (dd_extension_id != 0 && TryDependencyDescriptor(packet, dd_extension_id))
	{
		return true;
	}

	switch (codec)
	{
		case cmn::MediaCodecId::H264:
			return ApplyH264(packet);
		case cmn::MediaCodecId::H265:
			return ApplyH265(packet);
		case cmn::MediaCodecId::Vp8:
			return ApplyVp8(packet);
		case cmn::MediaCodecId::Av1:
			return ApplyAv1(packet);
		default:
			return false;  // unreachable due to the gate above
	}
}

// Dependency Descriptor: the mandatory descriptor's S/E bits give the true
// frame start and end. Parsed via the dedicated extension class.
bool RtpFrameBoundaryDetector::TryDependencyDescriptor(RtpPacket &packet, uint8_t dd_extension_id)
{
	auto ext = packet.GetExtension(dd_extension_id);
	if (ext.has_value() == false)
	{
		return false;
	}

	RtpHeaderExtensionDependencyDescriptor dd(dd_extension_id);
	if (dd.SetData(ext.value().Clone()) == false)
	{
		return false;
	}

	packet.SetFirstPacketOfFrame(dd.IsStartOfFrame());
	packet.SetLastPacketOfFrame(dd.IsEndOfFrame());
	return true;
}

// H.264 (RFC 6184). First payload byte: F(1) | NRI(2) | Type(5).
//   Type 1..23 : Single NAL unit       → packet starts a NAL.
//   Type 24..27: STAP / MTAP aggregate → packet starts a NAL.
//   Type 28..29: FU-A / FU-B           → second byte's S bit indicates NAL start.
//   Otherwise  : reserved / invalid    → reject.
bool RtpFrameBoundaryDetector::ApplyH264(RtpPacket &packet)
{
	auto payload = packet.Payload();
	auto size = packet.PayloadSize();
	if (size < 1)
	{
		return false;
	}

	uint8_t nal_type = payload[0] & 0x1F;

	if (nal_type >= 1 && nal_type <= 23)
	{
		packet.SetStartOfUnit(true);
		return true;
	}
	if (nal_type >= 24 && nal_type <= 27)
	{
		packet.SetStartOfUnit(true);
		return true;
	}
	if (nal_type == 28 || nal_type == 29)
	{
		if (size < 2)
		{
			return false;
		}
		packet.SetStartOfUnit((payload[1] & 0x80) != 0);
		return true;
	}
	return false;
}

// H.265 (RFC 7798). PayloadHdr is 2 bytes; type = bits 1..6 of byte 0.
//   Type 0..47 : Single NAL unit → packet starts a NAL.
//   Type 48 (AP): aggregate     → packet starts a NAL.
//   Type 49 (FU): fragmented    → third byte's S bit indicates NAL start.
//   Type 50 (PACI): single NAL  → packet starts a NAL.
bool RtpFrameBoundaryDetector::ApplyH265(RtpPacket &packet)
{
	auto payload = packet.Payload();
	auto size = packet.PayloadSize();
	if (size < 2)
	{
		return false;
	}

	uint8_t nal_type = (payload[0] >> 1) & 0x3F;

	if (nal_type < 48 || nal_type == 48 || nal_type == 50)
	{
		packet.SetStartOfUnit(true);
		return true;
	}
	if (nal_type == 49)
	{
		if (size < 3)
		{
			return false;
		}
		packet.SetStartOfUnit((payload[2] & 0x80) != 0);
		return true;
	}
	return false;
}

// VP8 (RFC 7741). First payload byte = Payload Descriptor.
//   S bit (0x10) marks start of a VP8 partition. PID==0 + S==1 = frame start.
bool RtpFrameBoundaryDetector::ApplyVp8(RtpPacket &packet)
{
	auto payload = packet.Payload();
	auto size = packet.PayloadSize();
	if (size < 1)
	{
		return false;
	}

	uint8_t pd = payload[0];
	bool s = (pd & 0x10) != 0;
	uint8_t pid = pd & 0x07;
	packet.SetStartOfUnit(s && pid == 0);
	return true;
}

// AV1 (https://aomediacodec.github.io/av1-rtp-spec/). Aggregation header byte
// has Z (0x80) = continuation from previous packet's last OBU element.
// Z == 0 → this packet starts a fresh OBU element, treated as a frame-start
// candidate. AV1 publishers in practice negotiate DD; this is a fallback.
bool RtpFrameBoundaryDetector::ApplyAv1(RtpPacket &packet)
{
	auto payload = packet.Payload();
	auto size = packet.PayloadSize();
	if (size < 1)
	{
		return false;
	}

	uint8_t agg = payload[0];
	packet.SetStartOfUnit((agg & 0x80) == 0);
	return true;
}
