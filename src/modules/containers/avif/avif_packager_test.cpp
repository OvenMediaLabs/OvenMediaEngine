//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <gtest/gtest.h>

#include <base/ovlibrary/data.h>
#include <modules/containers/avif/avif_packager.h>

#include <cstring>
#include <optional>
#include <vector>

namespace
{
	// A real sequence header OBU from a libaom allintra still: 320x180, seq_profile 0,
	// 8-bit 4:2:0, BT.709, full range.
	const std::vector<uint8_t> SEQUENCE_HEADER_OBU = {
		0x0a, 0x0e,													  // OBU_SEQUENCE_HEADER, obu_size = 14
		0x00, 0x00, 0x00, 0x04, 0x3c, 0xfe, 0xcc, 0xff,
		0xf8, 0x10, 0x10, 0x10, 0x18, 0x40};

	constexpr uint32_t STILL_WIDTH	= 320;
	constexpr uint32_t STILL_HEIGHT = 180;

	// OBU_TEMPORAL_DELIMITER, obu_size = 0
	const std::vector<uint8_t> TEMPORAL_DELIMITER_OBU = {0x12, 0x00};

	// Stand-in OBU_FRAMEs: only `uncompressed_header()`'s leading bits are read, never the
	// coded data. First payload byte = show_existing_frame(1) | frame_type(2) | show_frame(1).
	const std::vector<uint8_t> FRAME_OBU			 = {0x32, 0x05, 0x10, 0xad, 0xbe, 0xef, 0x00};	// KEY_FRAME
	const std::vector<uint8_t> INTER_FRAME_OBU		 = {0x32, 0x05, 0x30, 0xad, 0xbe, 0xef, 0x00};	// INTER_FRAME
	const std::vector<uint8_t> SHOW_EXISTING_FRAME_OBU = {0x32, 0x05, 0x80, 0xad, 0xbe, 0xef, 0x00};	// show_existing_frame

	std::shared_ptr<ov::Data> Concat(const std::vector<std::vector<uint8_t>> &parts)
	{
		std::vector<uint8_t> bytes;
		for (const auto &part : parts)
		{
			bytes.insert(bytes.end(), part.begin(), part.end());
		}
		return std::make_shared<ov::Data>(bytes.data(), bytes.size());
	}

	uint16_t ReadBE16(const uint8_t *data)
	{
		return static_cast<uint16_t>((data[0] << 8) | data[1]);
	}

	uint32_t ReadBE32(const uint8_t *data)
	{
		return (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
			   (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[3]);
	}

	struct FoundBox
	{
		const uint8_t *payload = nullptr;
		size_t size			   = 0;	 // payload size, header excluded
		size_t file_offset	   = 0;	 // offset of the payload from the start of the file
	};

	// Locate `name` among the sibling boxes in [data, data + size). `full_box` skips the
	// version/flags word.
	std::optional<FoundBox> FindBox(const uint8_t *data, size_t size, size_t base_offset, const char *name, bool full_box)
	{
		size_t pos = 0;
		while ((pos + 8) <= size)
		{
			const size_t box_size = ReadBE32(data + pos);
			if ((box_size < 8) || ((pos + box_size) > size))
			{
				return std::nullopt;
			}

			if (::memcmp(data + pos + 4, name, 4) == 0)
			{
				const size_t header_size = full_box ? 12 : 8;
				if (box_size < header_size)
				{
					return std::nullopt;
				}

				return FoundBox{data + pos + header_size, box_size - header_size, base_offset + pos + header_size};
			}

			pos += box_size;
		}

		return std::nullopt;
	}
}  // namespace

TEST(AvifPackager, RejectsNullInput)
{
	EXPECT_EQ(avif::Packager::Pack(nullptr), nullptr);
}

TEST(AvifPackager, RejectsEmptyInput)
{
	EXPECT_EQ(avif::Packager::Pack(std::make_shared<ov::Data>()), nullptr);
}

TEST(AvifPackager, RejectsTemporalUnitWithoutSequenceHeader)
{
	// Without a sequence header there is nothing to put in the av1C, and no source for the
	// picture size or the colour tag.
	EXPECT_EQ(avif::Packager::Pack(Concat({TEMPORAL_DELIMITER_OBU, FRAME_OBU})), nullptr);
}

TEST(AvifPackager, RejectsInterFrame)
{
	// An image item must decode standalone; an inter frame references frames the file does
	// not carry.
	EXPECT_EQ(avif::Packager::Pack(Concat({TEMPORAL_DELIMITER_OBU, SEQUENCE_HEADER_OBU, INTER_FRAME_OBU})), nullptr);
}

TEST(AvifPackager, RejectsShowExistingFrame)
{
	EXPECT_EQ(avif::Packager::Pack(Concat({TEMPORAL_DELIMITER_OBU, SEQUENCE_HEADER_OBU, SHOW_EXISTING_FRAME_OBU})), nullptr);
}

TEST(AvifPackager, WrapsStillIntoAvifFile)
{
	auto still = Concat({TEMPORAL_DELIMITER_OBU, SEQUENCE_HEADER_OBU, FRAME_OBU});

	auto file = avif::Packager::Pack(still);
	ASSERT_NE(file, nullptr);

	const auto *bytes = file->GetDataAs<uint8_t>();
	const size_t length = file->GetLength();

	// ISO/IEC 23000-22 10.1: the file declares the 'avif' brand.
	auto ftyp = FindBox(bytes, length, 0, "ftyp", false);
	ASSERT_TRUE(ftyp.has_value());
	EXPECT_EQ(::memcmp(ftyp->payload, "avif", 4), 0);

	auto meta = FindBox(bytes, length, 0, "meta", true);
	ASSERT_TRUE(meta.has_value());

	// hdlr must be first in meta (ISO/IEC 23008-12 6.2) and use the 'pict' handler.
	ASSERT_GE(meta->size, 16u);
	EXPECT_EQ(::memcmp(bytes + meta->file_offset + 4, "hdlr", 4), 0);
	auto hdlr = FindBox(meta->payload, meta->size, meta->file_offset, "hdlr", true);
	ASSERT_TRUE(hdlr.has_value());
	EXPECT_EQ(::memcmp(hdlr->payload + 4, "pict", 4), 0);

	// The primary item is the one image item.
	auto pitm = FindBox(meta->payload, meta->size, meta->file_offset, "pitm", true);
	ASSERT_TRUE(pitm.has_value());
	const uint16_t item_id = ReadBE16(pitm->payload);
	EXPECT_EQ(item_id, 1u);

	// The item is an AV1 image item.
	auto iinf = FindBox(meta->payload, meta->size, meta->file_offset, "iinf", true);
	ASSERT_TRUE(iinf.has_value());
	ASSERT_GE(iinf->size, 2u);	// entry_count precedes the entries
	auto infe = FindBox(iinf->payload + 2, iinf->size - 2, 0, "infe", true);
	ASSERT_TRUE(infe.has_value());
	EXPECT_EQ(ReadBE16(infe->payload), item_id);
	EXPECT_EQ(::memcmp(infe->payload + 4, "av01", 4), 0);

	// The single extent must address the temporal unit byte for byte at its real file
	// offset - only right if meta was sized before being written.
	auto iloc = FindBox(meta->payload, meta->size, meta->file_offset, "iloc", true);
	ASSERT_TRUE(iloc.has_value());
	ASSERT_EQ(iloc->size, 18u);
	EXPECT_EQ(iloc->payload[0], 0x44);	// offset_size = 4, length_size = 4
	EXPECT_EQ(iloc->payload[1], 0x00);	// base_offset_size = 0
	EXPECT_EQ(ReadBE16(iloc->payload + 2), 1u);		   // item_count
	EXPECT_EQ(ReadBE16(iloc->payload + 4), item_id);   // item_ID
	EXPECT_EQ(ReadBE16(iloc->payload + 8), 1u);		   // extent_count
	const uint32_t extent_offset = ReadBE32(iloc->payload + 10);
	const uint32_t extent_length = ReadBE32(iloc->payload + 14);
	ASSERT_EQ(extent_length, still->GetLength());
	ASSERT_LE(extent_offset + extent_length, length);
	EXPECT_EQ(::memcmp(bytes + extent_offset, still->GetData(), extent_length), 0);

	// ...and the extent must land inside mdat, right after its box header.
	auto mdat = FindBox(bytes, length, 0, "mdat", false);
	ASSERT_TRUE(mdat.has_value());
	EXPECT_EQ(mdat->file_offset, extent_offset);
	EXPECT_EQ(mdat->size, extent_length);

	auto iprp = FindBox(meta->payload, meta->size, meta->file_offset, "iprp", false);
	ASSERT_TRUE(iprp.has_value());
	auto ipco = FindBox(iprp->payload, iprp->size, iprp->file_offset, "ipco", false);
	ASSERT_TRUE(ipco.has_value());

	// av1C: fields read from the sequence header, plus the header itself as configOBUs.
	auto av1c = FindBox(ipco->payload, ipco->size, ipco->file_offset, "av1C", false);
	ASSERT_TRUE(av1c.has_value());
	ASSERT_EQ(av1c->size, 4 + SEQUENCE_HEADER_OBU.size());
	EXPECT_EQ(av1c->payload[0], 0x81);				  // marker = 1, version = 1
	EXPECT_EQ(av1c->payload[1] >> 5, 0u);			  // seq_profile
	EXPECT_EQ((av1c->payload[2] >> 6) & 0x01, 0u);	  // high_bitdepth (8-bit)
	EXPECT_EQ((av1c->payload[2] >> 4) & 0x01, 0u);	  // monochrome
	EXPECT_EQ((av1c->payload[2] >> 2) & 0x03, 0x03u); // chroma_subsampling_x/y (4:2:0)
	EXPECT_EQ(::memcmp(av1c->payload + 4, SEQUENCE_HEADER_OBU.data(), SEQUENCE_HEADER_OBU.size()), 0);

	// ispe restates the coded picture size.
	auto ispe = FindBox(ipco->payload, ipco->size, ipco->file_offset, "ispe", true);
	ASSERT_TRUE(ispe.has_value());
	ASSERT_EQ(ispe->size, 8u);
	EXPECT_EQ(ReadBE32(ispe->payload), STILL_WIDTH);
	EXPECT_EQ(ReadBE32(ispe->payload + 4), STILL_HEIGHT);

	// pixi: three 8-bit channels.
	auto pixi = FindBox(ipco->payload, ipco->size, ipco->file_offset, "pixi", true);
	ASSERT_TRUE(pixi.has_value());
	ASSERT_EQ(pixi->size, 4u);
	EXPECT_EQ(pixi->payload[0], 3u);
	EXPECT_EQ(pixi->payload[1], 8u);
	EXPECT_EQ(pixi->payload[2], 8u);
	EXPECT_EQ(pixi->payload[3], 8u);

	// colr restates the sequence header's CICP: BT.709, full range.
	auto colr = FindBox(ipco->payload, ipco->size, ipco->file_offset, "colr", false);
	ASSERT_TRUE(colr.has_value());
	ASSERT_EQ(colr->size, 11u);
	EXPECT_EQ(::memcmp(colr->payload, "nclx", 4), 0);
	EXPECT_EQ(ReadBE16(colr->payload + 4), 1u);	 // colour_primaries
	EXPECT_EQ(ReadBE16(colr->payload + 6), 1u);	 // transfer_characteristics
	EXPECT_EQ(ReadBE16(colr->payload + 8), 1u);	 // matrix_coefficients
	EXPECT_EQ(colr->payload[10], 0x80);			 // full_range_flag = 1

	// All four properties associated, av1C essential. Indices are 1-based positions in ipco,
	// so this pins the order ipco was written in.
	auto ipma = FindBox(iprp->payload, iprp->size, iprp->file_offset, "ipma", true);
	ASSERT_TRUE(ipma.has_value());
	ASSERT_EQ(ipma->size, 11u);
	EXPECT_EQ(ReadBE32(ipma->payload), 1u);			   // entry_count
	EXPECT_EQ(ReadBE16(ipma->payload + 4), item_id);   // item_ID
	EXPECT_EQ(ipma->payload[6], 4u);				   // association_count
	EXPECT_EQ(ipma->payload[7], 0x80 | 1);			   // av1C, essential
	EXPECT_EQ(ipma->payload[8], 2u);				   // ispe
	EXPECT_EQ(ipma->payload[9], 3u);				   // pixi
	EXPECT_EQ(ipma->payload[10], 4u);				   // colr
}

TEST(AvifPackager, WrapsStillWithoutTemporalDelimiter)
{
	// A temporal unit that starts straight at the sequence header is equally valid.
	auto still = Concat({SEQUENCE_HEADER_OBU, FRAME_OBU});

	auto file = avif::Packager::Pack(still);
	ASSERT_NE(file, nullptr);

	auto mdat = FindBox(file->GetDataAs<uint8_t>(), file->GetLength(), 0, "mdat", false);
	ASSERT_TRUE(mdat.has_value());
	EXPECT_EQ(mdat->size, still->GetLength());
	EXPECT_EQ(::memcmp(mdat->payload, still->GetData(), still->GetLength()), 0);
}
