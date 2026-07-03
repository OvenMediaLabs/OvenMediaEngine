//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "mpegts_packet.h"

#include <gtest/gtest.h>

#include <vector>

#include "mpegts_depacketizer.h"
#include "mpegts_section.h"

namespace
{
	// A minimal, self-consistent 188-byte TS packet (payload only, no adaptation field).
	std::vector<uint8_t> MakePacketBytes(uint16_t pid, uint8_t cc)
	{
		std::vector<uint8_t> bytes(188, 0x00);
		bytes[0] = 0x47;
		bytes[1] = static_cast<uint8_t>((pid >> 8) & 0x1F);	 // TEI=0, PUSI=0, prio=0
		bytes[2] = static_cast<uint8_t>(pid & 0xFF);
		bytes[3] = static_cast<uint8_t>((0b01 << 4) | (cc & 0x0F));	 // TSC=0, AFC=payload-only
		return bytes;
	}

	// A valid single-packet PAT (Section::Build computes the CRC), as raw TS bytes.
	std::shared_ptr<const ov::Data> MakePatPacket(uint8_t cc)
	{
		mpegts::PAT pat;
		pat._table_id_extension		= 1;
		pat._version_number			= 0;
		pat._current_next_indicator = true;
		pat._section_number			= 0;
		pat._last_section_number	= 0;
		pat._program_num			= 1;
		pat._program_map_pid		= 0x1000;

		auto section				= mpegts::Section::Build(pat);
		auto packet					= mpegts::Packet::Build(section, cc);
		return packet->GetData();
	}
}  // namespace

TEST(MpegTsPacket, ParseAcceptsValidSyncByte)
{
	auto bytes = MakePacketBytes(0x0100, 3);
	auto data  = std::make_shared<ov::Data>(bytes.data(), bytes.size());

	mpegts::Packet packet(data);
	EXPECT_EQ(packet.Parse(), 188u);
	EXPECT_EQ(packet.PacketIdentifier(), 0x0100);
	EXPECT_EQ(packet.ContinuityCounter(), 3);
}

TEST(MpegTsPacket, ParseRejectsInvalidSyncByte)
{
	auto bytes = MakePacketBytes(0x0100, 0);
	bytes[0]   = 0x00;	// corrupt the sync byte
	auto data  = std::make_shared<ov::Data>(bytes.data(), bytes.size());

	mpegts::Packet packet(data);
	EXPECT_EQ(packet.Parse(), 0u);
}

// A valid PAT should be recognized on its own.
TEST(MpegTsDepacketizerResync, ParsesAlignedPat)
{
	mpegts::MpegTsDepacketizer depacketizer;
	depacketizer.AddPacket(MakePatPacket(0));

	ASSERT_NE(depacketizer.GetFirstPAT(), nullptr);
}

// Leading garbage bytes must not permanently break parsing: the parser resynchronizes on the
// next confirmed sync byte and still recognizes the PAT.
TEST(MpegTsDepacketizerResync, RecoversFromLeadingGarbage)
{
	auto pat1 = MakePatPacket(0);
	auto pat2 = MakePatPacket(1);

	// [garbage][PAT][PAT] in a single buffer so the sync byte can be double-confirmed.
	std::vector<uint8_t> junk(50, 0x00);
	auto buffer = std::make_shared<ov::Data>(junk.data(), junk.size());
	buffer->Append(pat1);
	buffer->Append(pat2);

	mpegts::MpegTsDepacketizer depacketizer;
	depacketizer.AddPacket(buffer);

	ASSERT_NE(depacketizer.GetFirstPAT(), nullptr);
}

// A fixed 188-byte skip on misalignment would never re-lock the grid; resync must handle a
// non-packet-aligned prefix.
TEST(MpegTsDepacketizerResync, RecoversFromMisalignedPrefix)
{
	auto pat1 = MakePatPacket(0);
	auto pat2 = MakePatPacket(1);

	// 3 bytes of junk (not a multiple of 188) shifts the whole grid.
	std::vector<uint8_t> junk(3, 0x11);
	auto buffer = std::make_shared<ov::Data>(junk.data(), junk.size());
	buffer->Append(pat1);
	buffer->Append(pat2);

	mpegts::MpegTsDepacketizer depacketizer;
	depacketizer.AddPacket(buffer);

	ASSERT_NE(depacketizer.GetFirstPAT(), nullptr);
}
