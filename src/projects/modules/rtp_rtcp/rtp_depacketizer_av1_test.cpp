//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  Covers: RtpDepacketizerAV1 (AV1 RTP Specification)
//          - aggregation header W field (0 / 1 / >1)
//          - LEB128 length-prefixed elements
//          - Z/Y fragment reassembly across packets
//          - obu_has_size_field rewrite + extension header
//
//==============================================================================
#include <gtest/gtest.h>

#include "rtp_depacketizer_av1.h"

namespace
{
	std::shared_ptr<ov::Data> Packet(const std::vector<uint8_t> &bytes)
	{
		return std::make_shared<ov::Data>(bytes.data(), bytes.size());
	}

	std::vector<uint8_t> ToVector(const std::shared_ptr<ov::Data> &data)
	{
		const auto *p = data->GetDataAs<uint8_t>();
		return std::vector<uint8_t>(p, p + data->GetLength());
	}

	// OBU header bytes used below (forbidden=0, extension=0, has_size=0, reserved=0):
	//   TemporalDelimiter (type 2) = 0x10, SequenceHeader (type 1) = 0x08, Frame (type 6) = 0x30
	// After rewrite the parser sets obu_has_size_field (0x02): 0x12 / 0x0A / 0x32.
}  // namespace

// W=1: a single OBU element that runs to the end of the packet (no length prefix).
TEST(RtpDepacketizerAV1, SingleObuW1)
{
	RtpDepacketizerAV1 depacketizer;

	// agg=0x10 (W=1), Frame OBU header 0x30 + payload {0xAA,0xBB}
	auto out = depacketizer.ParseAndAssembleFrame({Packet({0x10, 0x30, 0xAA, 0xBB})});
	ASSERT_NE(out, nullptr);
	EXPECT_EQ(ToVector(out), (std::vector<uint8_t>{0x32, 0x02, 0xAA, 0xBB}));
}

// W=0: every element is preceded by a LEB128 length field.
TEST(RtpDepacketizerAV1, TwoObusW0LengthPrefixed)
{
	RtpDepacketizerAV1 depacketizer;

	// agg=0x00 (W=0)
	//  len=1 -> TD {0x10}
	//  len=2 -> Frame {0x30,0xAA}
	auto out = depacketizer.ParseAndAssembleFrame({Packet({0x00, 0x01, 0x10, 0x02, 0x30, 0xAA})});
	ASSERT_NE(out, nullptr);
	EXPECT_EQ(ToVector(out), (std::vector<uint8_t>{0x12, 0x00, 0x32, 0x01, 0xAA}));
}

// W>1: first W-1 elements length-prefixed, last element runs to end of packet.
TEST(RtpDepacketizerAV1, ThreeObusW3)
{
	RtpDepacketizerAV1 depacketizer;

	// agg=0x30 (W=3)
	//  len=1 -> TD {0x10}
	//  len=2 -> SeqHeader {0x08,0xAA}
	//  (last) -> Frame {0x30,0xBB,0xCC}
	auto out = depacketizer.ParseAndAssembleFrame(
		{Packet({0x30, 0x01, 0x10, 0x02, 0x08, 0xAA, 0x30, 0xBB, 0xCC})});
	ASSERT_NE(out, nullptr);
	EXPECT_EQ(ToVector(out),
			  (std::vector<uint8_t>{0x12, 0x00, 0x0A, 0x01, 0xAA, 0x32, 0x02, 0xBB, 0xCC}));
}

// One OBU fragmented across two packets: packet1 Y=1 (continues), packet2 Z=1 (continuation).
TEST(RtpDepacketizerAV1, FragmentedObuReassembled)
{
	RtpDepacketizerAV1 depacketizer;

	// packet1: agg=0x50 (W=1, Y=1), Frame header 0x30 + first payload byte 0xAA
	// packet2: agg=0x90 (W=1, Z=1), continuation bytes {0xBB,0xCC}
	auto out = depacketizer.ParseAndAssembleFrame(
		{Packet({0x50, 0x30, 0xAA}), Packet({0x90, 0xBB, 0xCC})});
	ASSERT_NE(out, nullptr);
	// Reassembled OBU: header 0x30 + payload {0xAA,0xBB,0xCC}
	EXPECT_EQ(ToVector(out), (std::vector<uint8_t>{0x32, 0x03, 0xAA, 0xBB, 0xCC}));
}

// OBU with an extension header byte: the extension byte is preserved, size field inserted after it.
TEST(RtpDepacketizerAV1, ObuWithExtensionHeader)
{
	RtpDepacketizerAV1 depacketizer;

	// agg=0x10 (W=1), header 0x34 (Frame, extension_flag=1), ext byte 0x00, payload {0xAA}
	auto out = depacketizer.ParseAndAssembleFrame({Packet({0x10, 0x34, 0x00, 0xAA})});
	ASSERT_NE(out, nullptr);
	EXPECT_EQ(ToVector(out), (std::vector<uint8_t>{0x36, 0x00, 0x01, 0xAA}));
}

// An element that already carries obu_has_size_field == 1 is passed through verbatim.
TEST(RtpDepacketizerAV1, AlreadyHasSizeFieldPassthrough)
{
	RtpDepacketizerAV1 depacketizer;

	// agg=0x10 (W=1), header 0x32 (has_size_field=1), size 0x01, payload {0xAA}
	auto out = depacketizer.ParseAndAssembleFrame({Packet({0x10, 0x32, 0x01, 0xAA})});
	ASSERT_NE(out, nullptr);
	EXPECT_EQ(ToVector(out), (std::vector<uint8_t>{0x32, 0x01, 0xAA}));
}

// Empty payload list -> null.
TEST(RtpDepacketizerAV1, EmptyPayloadListReturnsNull)
{
	RtpDepacketizerAV1 depacketizer;
	EXPECT_EQ(depacketizer.ParseAndAssembleFrame({}), nullptr);
}

// A zero-length payload (valid empty Data, no aggregation header byte) -> null.
TEST(RtpDepacketizerAV1, EmptyPacketReturnsNull)
{
	RtpDepacketizerAV1 depacketizer;
	EXPECT_EQ(depacketizer.ParseAndAssembleFrame({std::make_shared<ov::Data>()}), nullptr);
}

// A LEB128 element length that overruns the packet -> null.
TEST(RtpDepacketizerAV1, ElementLengthExceedsPacketReturnsNull)
{
	RtpDepacketizerAV1 depacketizer;

	// agg=0x00 (W=0), len=5 but only 1 byte follows
	EXPECT_EQ(depacketizer.ParseAndAssembleFrame({Packet({0x00, 0x05, 0x30})}), nullptr);
}

// A stray zero-length element is skipped; the rest of the temporal unit survives.
TEST(RtpDepacketizerAV1, ZeroLengthElementSkippedNotDropped)
{
	RtpDepacketizerAV1 depacketizer;

	// agg=0x00 (W=0); len=0 -> empty element; len=2 -> Frame {0x30,0xAA}
	auto out = depacketizer.ParseAndAssembleFrame({Packet({0x00, 0x00, 0x02, 0x30, 0xAA})});
	ASSERT_NE(out, nullptr);
	EXPECT_EQ(ToVector(out), (std::vector<uint8_t>{0x32, 0x01, 0xAA}));
}

// A temporal unit consisting solely of zero-length elements yields no OBU -> null.
TEST(RtpDepacketizerAV1, OnlyZeroLengthElementsReturnsNull)
{
	RtpDepacketizerAV1 depacketizer;

	// agg=0x00 (W=0); single len=0 element
	EXPECT_EQ(depacketizer.ParseAndAssembleFrame({Packet({0x00, 0x00})}), nullptr);
}

// W declares more elements than the packet actually contains -> null.
TEST(RtpDepacketizerAV1, WCountMismatchReturnsNull)
{
	RtpDepacketizerAV1 depacketizer;

	// agg=0x30 (W=3) but only one element (len=1 -> {0x10}) is present
	EXPECT_EQ(depacketizer.ParseAndAssembleFrame({Packet({0x30, 0x01, 0x10})}), nullptr);
}
