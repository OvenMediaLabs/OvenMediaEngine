//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  Covers: RtpFrameBoundaryDetector (H.264/H.265/VP8/AV1, codec gate, DD)
//
//==============================================================================
#include <gtest/gtest.h>

#include "rtp_frame_boundary_detector.h"
#include "rtp_header_extension/rtp_header_extension_dependency_descriptor.h"
#include "rtp_packet.h"

namespace
{
std::shared_ptr<RtpPacket> MakePacket(const std::vector<uint8_t> &payload, bool marker = false)
{
	auto p = std::make_shared<RtpPacket>();
	p->SetPayloadType(96);
	p->SetSequenceNumber(100);
	p->SetTimestamp(90000);
	p->SetSsrc(0x12345678);
	p->SetMarker(marker);
	if (payload.empty() == false)
	{
		p->SetPayload(payload.data(), payload.size());
	}
	return p;
}
}  // namespace

// Defensive gate: unsupported codec returns false.
TEST(RtpFrameBoundaryDetector, UnsupportedCodecReturnsFalse)
{
	auto p = MakePacket({0x01});
	EXPECT_FALSE(RtpFrameBoundaryDetector::Apply(*p, cmn::MediaCodecId::None, 0));
}

// H.264 single NAL (nal_type 1..23): first=true, last=marker.
TEST(RtpFrameBoundaryDetector, H264SingleNalFirstTrue)
{
	auto p = MakePacket({0x21}, true);   // nal_type = 1 (slice)
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*p, cmn::MediaCodecId::H264, 0));
	EXPECT_TRUE(p->IsStartOfUnit());
	EXPECT_TRUE(p->IsLastPacketOfFrame());
}

TEST(RtpFrameBoundaryDetector, H264SpsPpsFirstTrue)
{
	auto sps = MakePacket({0x67, 0x42, 0xc0, 0x1f}, false);   // nal_type 7 (SPS)
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*sps, cmn::MediaCodecId::H264, 0));
	EXPECT_TRUE(sps->IsStartOfUnit());
	EXPECT_FALSE(sps->IsLastPacketOfFrame());
}

// H.264 STAP-A aggregation (nal_type 24): first=true.
TEST(RtpFrameBoundaryDetector, H264StapAAggregateFirstTrue)
{
	auto p = MakePacket({0x18, 0x00, 0x10, 0x67}, false);   // nal_type 24
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*p, cmn::MediaCodecId::H264, 0));
	EXPECT_TRUE(p->IsStartOfUnit());
}

// H.264 FU-A start (S bit set in FU header): first=true.
TEST(RtpFrameBoundaryDetector, H264FuAStartFirstTrue)
{
	// First byte: FU indicator (type 28). Second byte: FU header with S bit (0x80).
	auto p = MakePacket({0x7c, 0x85}, false);
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*p, cmn::MediaCodecId::H264, 0));
	EXPECT_TRUE(p->IsStartOfUnit());
}

// H.264 FU-A middle (S bit cleared): first=false.
TEST(RtpFrameBoundaryDetector, H264FuAMiddleFirstFalse)
{
	auto p = MakePacket({0x7c, 0x05}, false);
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*p, cmn::MediaCodecId::H264, 0));
	EXPECT_FALSE(p->IsStartOfUnit());
}

// H.264 FU-A end (marker=1 → last=true, S bit 0 → first=false).
TEST(RtpFrameBoundaryDetector, H264FuAEndLastTrue)
{
	auto p = MakePacket({0x7c, 0x45}, true);   // E bit 0x40 + marker
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*p, cmn::MediaCodecId::H264, 0));
	EXPECT_FALSE(p->IsStartOfUnit());
	EXPECT_TRUE(p->IsLastPacketOfFrame());
}

// H.264 reserved nal_type rejected.
TEST(RtpFrameBoundaryDetector, H264ReservedNalTypeRejected)
{
	auto p = MakePacket({0x00}, false);   // nal_type 0 (forbidden)
	EXPECT_FALSE(RtpFrameBoundaryDetector::Apply(*p, cmn::MediaCodecId::H264, 0));
}

// H.264 truncated FU-A (size < 2) rejected.
TEST(RtpFrameBoundaryDetector, H264TruncatedFuRejected)
{
	auto p = MakePacket({0x7c}, false);   // only the FU indicator
	EXPECT_FALSE(RtpFrameBoundaryDetector::Apply(*p, cmn::MediaCodecId::H264, 0));
}

// H.265 single NAL (type 0..47): first=true.
TEST(RtpFrameBoundaryDetector, H265SingleNalFirstTrue)
{
	// First byte: F(0)|Type(6)|LayerID(6)|TID(3) — we craft Type=1 (TRAIL_N).
	// Byte0 = 0x02 (type=1, layer=0), Byte1 = 0x01 (TID=1).
	auto p = MakePacket({0x02, 0x01}, true);
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*p, cmn::MediaCodecId::H265, 0));
	EXPECT_TRUE(p->IsStartOfUnit());
	EXPECT_TRUE(p->IsLastPacketOfFrame());
}

// H.265 FU (type 49) with S bit set in FU header: first=true.
TEST(RtpFrameBoundaryDetector, H265FuStartFirstTrue)
{
	// Type 49 -> 49 << 1 = 0x62.
	// Bytes: PayloadHdr(2) + FU header(1) with S bit (0x80).
	auto p = MakePacket({0x62, 0x01, 0x80}, false);
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*p, cmn::MediaCodecId::H265, 0));
	EXPECT_TRUE(p->IsStartOfUnit());
}

// H.265 FU middle: S bit cleared -> first=false.
TEST(RtpFrameBoundaryDetector, H265FuMiddleFirstFalse)
{
	auto p = MakePacket({0x62, 0x01, 0x00}, false);
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*p, cmn::MediaCodecId::H265, 0));
	EXPECT_FALSE(p->IsStartOfUnit());
}

// VP8 first packet of frame: S bit + PID=0 -> first=true.
TEST(RtpFrameBoundaryDetector, Vp8FirstPidZeroFirstTrue)
{
	// Payload descriptor byte: X|R|N|S|R|PID(3).
	// S=1 (0x10), PID=0 -> 0x10.
	auto p = MakePacket({0x10}, false);
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*p, cmn::MediaCodecId::Vp8, 0));
	EXPECT_TRUE(p->IsStartOfUnit());
}

// VP8 not first (S=0 or PID!=0): first=false.
TEST(RtpFrameBoundaryDetector, Vp8MiddleFirstFalse)
{
	// S=0, PID=1 -> 0x01.
	auto p = MakePacket({0x01}, false);
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*p, cmn::MediaCodecId::Vp8, 0));
	EXPECT_FALSE(p->IsStartOfUnit());
}

// AV1: aggregation header with Z=0 -> first=true.
TEST(RtpFrameBoundaryDetector, Av1ZBitClearFirstTrue)
{
	// Aggregation header: Z|Y|W|N. Z=0 means this packet starts a fresh OBU element.
	auto p = MakePacket({0x00}, false);
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*p, cmn::MediaCodecId::Av1, 0));
	EXPECT_TRUE(p->IsStartOfUnit());
}

TEST(RtpFrameBoundaryDetector, Av1ZBitSetFirstFalse)
{
	auto p = MakePacket({0x80}, false);
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*p, cmn::MediaCodecId::Av1, 0));
	EXPECT_FALSE(p->IsStartOfUnit());
}

// Marker bit always reflected as IsLastPacketOfFrame for supported codecs.
TEST(RtpFrameBoundaryDetector, MarkerBitMapsToLast)
{
	auto p = MakePacket({0x21}, true);   // H264 single NAL, marker
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*p, cmn::MediaCodecId::H264, 0));
	EXPECT_TRUE(p->IsLastPacketOfFrame());
}

// Empty payload returns false (no header bytes to inspect).
TEST(RtpFrameBoundaryDetector, EmptyPayloadRejected)
{
	auto p = MakePacket({}, true);
	EXPECT_FALSE(RtpFrameBoundaryDetector::Apply(*p, cmn::MediaCodecId::H264, 0));
}

// ---- Keyframe start detection ----

TEST(RtpFrameBoundaryDetector, H264IdrIsKeyframeStartParameterSetsAreNot)
{
	auto idr = MakePacket({0x65, 0x88});                      // nal_type 5
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*idr, cmn::MediaCodecId::H264, 0));
	EXPECT_TRUE(idr->IsKeyframe());

	auto sps = MakePacket({0x67, 0x42, 0xc0, 0x1f});          // nal_type 7: configuration, no picture
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*sps, cmn::MediaCodecId::H264, 0));
	EXPECT_FALSE(sps->IsKeyframe());

	auto slice = MakePacket({0x21, 0x9a});                    // nal_type 1
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*slice, cmn::MediaCodecId::H264, 0));
	EXPECT_FALSE(slice->IsKeyframe());
}

TEST(RtpFrameBoundaryDetector, H264StapAWithIdrIsKeyframeStart)
{
	// STAP-A: [size 2][PPS 0x68 0xce][size 2][IDR 0x65 0x88]
	auto p = MakePacket({0x18, 0x00, 0x02, 0x68, 0xce, 0x00, 0x02, 0x65, 0x88});
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*p, cmn::MediaCodecId::H264, 0));
	EXPECT_TRUE(p->IsKeyframe());

	auto q = MakePacket({0x18, 0x00, 0x02, 0x67, 0x42, 0x00, 0x02, 0x68, 0xce});   // SPS + PPS only
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*q, cmn::MediaCodecId::H264, 0));
	EXPECT_FALSE(q->IsKeyframe());
}

TEST(RtpFrameBoundaryDetector, H264EveryIdrFragmentIsMarkedKeyframe)
{
	auto start = MakePacket({0x7c, 0x85, 0x88});    // FU-A, S=1, type 5
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*start, cmn::MediaCodecId::H264, 0));
	EXPECT_TRUE(start->IsKeyframe());

	auto middle = MakePacket({0x7c, 0x05, 0x88});   // FU-A, S=0, type 5: still an IDR fragment
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*middle, cmn::MediaCodecId::H264, 0));
	EXPECT_TRUE(middle->IsKeyframe());

	auto delta = MakePacket({0x7c, 0x81, 0x88});    // FU-A, S=1, type 1
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*delta, cmn::MediaCodecId::H264, 0));
	EXPECT_FALSE(delta->IsKeyframe());
}

TEST(RtpFrameBoundaryDetector, H265IrapIsKeyframeStart)
{
	auto idr = MakePacket({0x26, 0x01, 0xaf});      // type 19 (IDR_W_RADL)
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*idr, cmn::MediaCodecId::H265, 0));
	EXPECT_TRUE(idr->IsKeyframe());

	auto trail = MakePacket({0x02, 0x01, 0xaf});    // type 1 (TRAIL_R)
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*trail, cmn::MediaCodecId::H265, 0));
	EXPECT_FALSE(trail->IsKeyframe());

	auto sps = MakePacket({0x42, 0x01, 0x01});      // type 33 (SPS): configuration, no picture
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*sps, cmn::MediaCodecId::H265, 0));
	EXPECT_FALSE(sps->IsKeyframe());

	auto fu_start = MakePacket({0x62, 0x01, 0x93}); // FU, S=1, FuType 19
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*fu_start, cmn::MediaCodecId::H265, 0));
	EXPECT_TRUE(fu_start->IsKeyframe());

	auto fu_mid = MakePacket({0x62, 0x01, 0x13});   // FU, S=0, FuType 19: still an IRAP fragment
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*fu_mid, cmn::MediaCodecId::H265, 0));
	EXPECT_TRUE(fu_mid->IsKeyframe());
}

TEST(RtpFrameBoundaryDetector, Vp8KeyframeFromPBit)
{
	// X=1 S=1 PID=0; ext I=1 L=1 T=1; 2-byte picture id; TL0PICIDX; TID byte; frame tag P=0
	auto key = MakePacket({0x90, 0xe0, 0x80, 0x01, 0x05, 0x20, 0x50, 0x01, 0x00});
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*key, cmn::MediaCodecId::Vp8, 0));
	EXPECT_TRUE(key->IsKeyframe());

	auto delta = MakePacket({0x90, 0xe0, 0x80, 0x01, 0x05, 0x20, 0x51, 0x01, 0x00});   // P=1
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*delta, cmn::MediaCodecId::Vp8, 0));
	EXPECT_FALSE(delta->IsKeyframe());

	auto no_ext = MakePacket({0x10, 0x50, 0x01, 0x00});   // X=0 S=1 PID=0, frame tag P=0
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*no_ext, cmn::MediaCodecId::Vp8, 0));
	EXPECT_TRUE(no_ext->IsKeyframe());

	auto partition = MakePacket({0x11, 0x50});            // PID=1: not a frame start
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*partition, cmn::MediaCodecId::Vp8, 0));
	EXPECT_FALSE(partition->IsKeyframe());
}

TEST(RtpFrameBoundaryDetector, Av1NewCodedVideoSequenceIsKeyframeStart)
{
	auto key = MakePacket({0x08, 0x0a, 0x0b});    // Z=0, N=1
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*key, cmn::MediaCodecId::Av1, 0));
	EXPECT_TRUE(key->IsKeyframe());

	auto delta = MakePacket({0x00, 0x32, 0x0b});  // N=0
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*delta, cmn::MediaCodecId::Av1, 0));
	EXPECT_FALSE(delta->IsKeyframe());
}

// With DD negotiated the frame type still comes from the codec payload.
TEST(RtpFrameBoundaryDetector, KeyframeIsStampedWithDependencyDescriptor)
{
	// Extensions are only readable from a parsed packet, so build then re-parse
	auto built = MakePacket({});
	uint8_t dd_bytes[] = {0xC0, 0x00, 0x05};       // S=1, E=1
	auto dd = std::make_shared<RtpHeaderExtensionDependencyDescriptor>(12);
	ASSERT_TRUE(dd->SetData(std::make_shared<ov::Data>(dd_bytes, sizeof(dd_bytes))));
	RtpHeaderExtensions extensions;
	extensions.AddExtention(dd);
	built->SetExtensions(extensions);
	uint8_t idr[] = {0x65, 0x88};
	built->SetPayload(idr, sizeof(idr));

	auto p = std::make_shared<RtpPacket>();
	ASSERT_TRUE(p->Parse(built->GetData()));
	ASSERT_TRUE(RtpFrameBoundaryDetector::Apply(*p, cmn::MediaCodecId::H264, 12));
	EXPECT_TRUE(p->IsFirstPacketOfFrame());
	EXPECT_TRUE(p->IsKeyframe());
}

// ---- RtpHeaderExtensionDependencyDescriptor ----

TEST(RtpHeaderExtensionDependencyDescriptor, ParsesMandatoryFields)
{
	// S=1, E=1, template_id=0, frame_number=5.
	uint8_t bytes[] = {0xC0, 0x00, 0x05};
	auto data = std::make_shared<ov::Data>(bytes, sizeof(bytes));

	RtpHeaderExtensionDependencyDescriptor dd(12);
	ASSERT_TRUE(dd.SetData(data));
	EXPECT_TRUE(dd.IsStartOfFrame());
	EXPECT_TRUE(dd.IsEndOfFrame());
	EXPECT_EQ(dd.GetFrameNumber(), 5);
}

TEST(RtpHeaderExtensionDependencyDescriptor, ParsesMidFrame)
{
	// S=0, E=0 (a packet in the middle of a frame).
	uint8_t bytes[] = {0x00, 0x00, 0x06};
	auto data = std::make_shared<ov::Data>(bytes, sizeof(bytes));

	RtpHeaderExtensionDependencyDescriptor dd(12);
	ASSERT_TRUE(dd.SetData(data));
	EXPECT_FALSE(dd.IsStartOfFrame());
	EXPECT_FALSE(dd.IsEndOfFrame());
}

TEST(RtpHeaderExtensionDependencyDescriptor, RejectsEmpty)
{
	RtpHeaderExtensionDependencyDescriptor dd(12);
	EXPECT_FALSE(dd.SetData(std::make_shared<ov::Data>()));
}
