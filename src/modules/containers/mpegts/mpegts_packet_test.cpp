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

	// A valid single-packet PMT for program 1 with one H.264 elementary stream on PID 0x0100
	// (matches MakePatPacket's program_map_pid 0x1000). Feeding PAT + PMT registers the PES PID.
	std::shared_ptr<const ov::Data> MakePmtPacket(uint8_t cc)
	{
		mpegts::PMT pmt;
		pmt._table_id_extension		= 1;
		pmt._version_number			= 0;
		pmt._current_next_indicator = true;
		pmt._section_number			= 0;
		pmt._last_section_number	= 0;
		pmt._pcr_pid				= 0x0100;
		pmt._program_info_length	= 0;

		auto es						= std::make_shared<mpegts::ESInfo>();
		es->_stream_type			= static_cast<uint8_t>(mpegts::WellKnownStreamTypes::H264);
		es->_elementary_pid			= 0x0100;
		es->_es_info_length			= 0;
		pmt._es_info_list.push_back(es);

		auto section = mpegts::Section::Build(0x1000, pmt);
		auto packet	 = mpegts::Packet::Build(section, cc);
		return packet->GetData();
	}

	// A raw 188-byte TS packet carrying PES data (payload only, AFC=01). When `pusi` is set, a minimal
	// video PES header (start code, stream_id 0xE0, unbounded length, PTS=0) is prefixed; the rest of
	// the payload is `fill` (stand-in elementary-stream bytes).
	std::shared_ptr<const ov::Data> MakePesTsPacket(uint16_t pid, uint8_t cc, bool pusi, uint8_t fill)
	{
		std::vector<uint8_t> b(188, fill);
		b[0] = 0x47;
		b[1] = static_cast<uint8_t>((pusi ? 0x40 : 0x00) | ((pid >> 8) & 0x1F));
		b[2] = static_cast<uint8_t>(pid & 0xFF);
		b[3] = static_cast<uint8_t>((0b01 << 4) | (cc & 0x0F));

		if (pusi)
		{
			const uint8_t header[] = {0x00, 0x00, 0x01, 0xE0, 0x00, 0x00, 0x80, 0x80, 0x05, 0x21, 0x00, 0x01, 0x00, 0x01};
			for (size_t i = 0; i < sizeof(header); i++)
			{
				b[4 + i] = header[i];
			}
		}

		return std::make_shared<ov::Data>(b.data(), b.size());
	}

	// A 188-byte TS packet with transport_error_indicator set (aligned but corrupt).
	std::shared_ptr<const ov::Data> MakeTeiPacket(uint16_t pid, uint8_t cc)
	{
		auto bytes = MakePacketBytes(pid, cc);
		bytes[1] |= 0x80;  // transport_error_indicator
		return std::make_shared<ov::Data>(bytes.data(), bytes.size());
	}

	// An adaptation-field-only (no payload) TS packet with the discontinuity_indicator set.
	std::shared_ptr<const ov::Data> MakeDiscontinuityPacket(uint16_t pid, uint8_t cc)
	{
		std::vector<uint8_t> b(188, 0xFF);
		b[0] = 0x47;
		b[1] = static_cast<uint8_t>((pid >> 8) & 0x1F);
		b[2] = static_cast<uint8_t>(pid & 0xFF);
		b[3] = static_cast<uint8_t>((0b10 << 4) | (cc & 0x0F));	 // adaptation field only, no payload
		b[4] = 183;												 // adaptation_field_length (fills the packet body)
		b[5] = 0x80;											 // discontinuity_indicator
		return std::make_shared<ov::Data>(b.data(), b.size());
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

// -----------------------------------------------------------------------------
// Continuity-counter hardening (AddPacket(Packet&)): duplicate drop, continuity-break and
// discontinuity draft discard, transport-error handling.
// -----------------------------------------------------------------------------

TEST(MpegTsDepacketizerCc, TrackInfoAvailableAfterPatPmt)
{
	// Harness sanity: PAT + a single-packet PMT make the track info available.
	mpegts::MpegTsDepacketizer depacketizer;
	depacketizer.AddPacket(MakePatPacket(0));
	depacketizer.AddPacket(MakePmtPacket(0));

	EXPECT_TRUE(depacketizer.IsTrackInfoAvailable());
}

TEST(MpegTsDepacketizerCc, DuplicatePacketNotAppendedTwice)
{
	// A legal single duplicate (same continuity counter) must be dropped, not appended to the PES.
	auto emitted_payload_length = [](bool with_duplicate) -> uint32_t {
		mpegts::MpegTsDepacketizer depacketizer;
		depacketizer.AddPacket(MakePatPacket(0));
		depacketizer.AddPacket(MakePmtPacket(0));
		depacketizer.AddPacket(MakePesTsPacket(0x0100, 0, true, 0xAA));	  // PES1 start
		depacketizer.AddPacket(MakePesTsPacket(0x0100, 1, false, 0xBB));  // PES1 continuation
		if (with_duplicate)
		{
			depacketizer.AddPacket(MakePesTsPacket(0x0100, 1, false, 0xBB));  // duplicate of the continuation
		}
		depacketizer.AddPacket(MakePesTsPacket(0x0100, 2, true, 0xCC));	 // PES2 start completes PES1
		auto pes = depacketizer.PopES();
		return (pes != nullptr) ? pes->PayloadLength() : 0;
	};

	const uint32_t without_duplicate = emitted_payload_length(false);
	EXPECT_GT(without_duplicate, 0u);
	EXPECT_EQ(emitted_payload_length(true), without_duplicate);
}

TEST(MpegTsDepacketizerCc, ContinuityBreakDiscardsPesDraft)
{
	// A genuine continuity break discards the in-progress PES so a corrupt frame is not forwarded.
	mpegts::MpegTsDepacketizer depacketizer;
	depacketizer.AddPacket(MakePatPacket(0));
	depacketizer.AddPacket(MakePmtPacket(0));
	depacketizer.AddPacket(MakePesTsPacket(0x0100, 0, true, 0xAA));	  // PES1 start (cc 0)
	depacketizer.AddPacket(MakePesTsPacket(0x0100, 5, false, 0x11));  // continuity break (cc 5) -> discard PES1
	depacketizer.AddPacket(MakePesTsPacket(0x0100, 6, true, 0xCC));	  // PES2 start (cc 6)
	depacketizer.AddPacket(MakePesTsPacket(0x0100, 7, true, 0xDD));	  // PES3 start completes PES2

	auto pes = depacketizer.PopES();
	ASSERT_NE(pes, nullptr);
	ASSERT_GT(pes->PayloadLength(), 0u);
	// PES1 (0xAA) was discarded on the break; the first emitted PES is PES2 (0xCC).
	EXPECT_EQ(pes->Payload()[0], 0xCC);
}

TEST(MpegTsDepacketizerCc, DiscontinuityIndicatorDiscardsPesDraft)
{
	// A signalled discontinuity (adaptation-field-only packet) also discards the in-progress PES.
	mpegts::MpegTsDepacketizer depacketizer;
	depacketizer.AddPacket(MakePatPacket(0));
	depacketizer.AddPacket(MakePmtPacket(0));
	depacketizer.AddPacket(MakePesTsPacket(0x0100, 0, true, 0xAA));	 // PES1 start
	depacketizer.AddPacket(MakeDiscontinuityPacket(0x0100, 1));		 // discontinuity_indicator -> discard PES1
	depacketizer.AddPacket(MakePesTsPacket(0x0100, 2, true, 0xCC));	 // PES2 start
	depacketizer.AddPacket(MakePesTsPacket(0x0100, 3, true, 0xDD));	 // PES3 start completes PES2

	auto pes = depacketizer.PopES();
	ASSERT_NE(pes, nullptr);
	ASSERT_GT(pes->PayloadLength(), 0u);
	EXPECT_EQ(pes->Payload()[0], 0xCC);
}

TEST(MpegTsDepacketizerCc, TransportErrorPacketDoesNotDerailGrid)
{
	// An aligned transport-error packet is dropped without losing 188-byte grid lock; the following
	// PMT still parses.
	mpegts::MpegTsDepacketizer depacketizer;
	depacketizer.AddPacket(MakePatPacket(0));		   // locks the grid, PAT registered
	depacketizer.AddPacket(MakeTeiPacket(0x1FFF, 0));  // aligned TEI packet -> dropped, grid preserved
	depacketizer.AddPacket(MakePmtPacket(0));

	EXPECT_TRUE(depacketizer.IsTrackInfoAvailable());
}
