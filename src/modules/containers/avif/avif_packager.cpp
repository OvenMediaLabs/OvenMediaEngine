//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "avif_packager.h"

#include <modules/bitstream/av1/av1_decoder_configuration_record.h>
#include <modules/bitstream/av1/av1_parser.h>

#include <optional>

#include "avif_private.h"

namespace avif
{
	namespace
	{
		// ISO/IEC 14496-12 4.2: size(4) + type(4)
		constexpr size_t BOX_HEADER_SIZE = 8;

		// The single image item. Item IDs are 1-based.
		constexpr uint16_t ITEM_ID = 1;

		// ipma names a property by its 1-based position in ipco, so these must match the order
		// WriteIprpBox() writes them in (ISO/IEC 23008-12 9.3.2).
		constexpr uint8_t PROPERTY_INDEX_AV1C = 1;
		constexpr uint8_t PROPERTY_INDEX_ISPE = 2;
		constexpr uint8_t PROPERTY_INDEX_PIXI = 3;
		constexpr uint8_t PROPERTY_INDEX_COLR = 4;

		// High bit of an 8-bit ipma association: a reader that does not understand the property
		// must refuse the item rather than render it wrongly.
		constexpr uint8_t PROPERTY_ESSENTIAL = 0x80;

		// What the boxes restate about the still, all read from its sequence header.
		struct StillInfo
		{
			uint32_t width	= 0;
			uint32_t height = 0;
			uint8_t channels = 0;
			uint8_t bit_depth = 0;
			uint8_t color_primaries = 0;
			uint8_t transfer_characteristics = 0;
			uint8_t matrix_coefficients = 0;
			bool full_range = false;
			std::shared_ptr<const ov::Data> av1c;  // serialized AV1CodecConfigurationRecord
		};

		bool WriteBox(ov::ByteStream &stream, const char *type, const ov::Data &payload)
		{
			return stream.WriteBE32(static_cast<uint32_t>(BOX_HEADER_SIZE + payload.GetLength())) &&
				   stream.WriteText(type) &&
				   stream.Write(payload);
		}

		bool WriteFullBox(ov::ByteStream &stream, const char *type, uint8_t version, uint32_t flags, const ov::Data &payload)
		{
			// ISO/IEC 14496-12 4.2 FullBox: Box + unsigned int(8) version; bit(24) flags;
			ov::ByteStream full_box(payload.GetLength() + 4);

			return full_box.WriteBE32((static_cast<uint32_t>(version) << 24) | (flags & 0x00FFFFFF)) &&
				   full_box.Write(payload) &&
				   WriteBox(stream, type, *full_box.GetData());
		}

		bool WriteFtypBox(ov::ByteStream &stream)
		{
			// ISO/IEC 23000-22 10.1: brand 'avif', compatible with HEIF ('mif1') and MIAF ('miaf').
			ov::ByteStream payload(32);

			return payload.WriteText("avif") &&			// major_brand
				   payload.WriteBE32(0) &&				// minor_version
				   payload.WriteText("avifmif1miaf") &&	// compatible_brands[]
				   WriteBox(stream, "ftyp", *payload.GetData());
		}

		bool WriteHdlrBox(ov::ByteStream &stream)
		{
			// ISO/IEC 23008-12 6.2: image items live under the 'pict' handler.
			ov::ByteStream payload(32);

			return payload.WriteBE32(0) &&							 // pre_defined
				   payload.WriteText("pict") &&						 // handler_type
				   payload.WriteBE32(0) && payload.WriteBE32(0) && payload.WriteBE32(0) &&	// reserved[3]
				   payload.WriteBE(static_cast<uint8_t>(0)) &&		 // name (empty, null-terminated)
				   WriteFullBox(stream, "hdlr", 0, 0, *payload.GetData());
		}

		bool WritePitmBox(ov::ByteStream &stream)
		{
			// ISO/IEC 14496-12 8.11.4: which item is the image to display.
			ov::ByteStream payload(8);

			return payload.WriteBE16(ITEM_ID) &&
				   WriteFullBox(stream, "pitm", 0, 0, *payload.GetData());
		}

		bool WriteIlocBox(ov::ByteStream &stream, uint32_t payload_offset, uint32_t payload_length)
		{
			// ISO/IEC 14496-12 8.11.3: where the item's bytes are. One extent, addressed
			// absolutely from the start of the file (base_offset_size 0).
			ov::ByteStream payload(32);

			return payload.WriteBE(static_cast<uint8_t>((4 << 4) | 4)) &&	// offset_size(4), length_size(4)
				   payload.WriteBE(static_cast<uint8_t>(0)) &&				// base_offset_size(4), reserved(4)
				   payload.WriteBE16(1) &&									// item_count
				   payload.WriteBE16(ITEM_ID) &&							// item_ID
				   payload.WriteBE16(0) &&									// data_reference_index (0 = this file)
				   payload.WriteBE16(1) &&									// extent_count
				   payload.WriteBE32(payload_offset) &&						// extent_offset
				   payload.WriteBE32(payload_length) &&						// extent_length
				   WriteFullBox(stream, "iloc", 0, 0, *payload.GetData());
		}

		bool WriteIinfBox(ov::ByteStream &stream)
		{
			// ISO/IEC 14496-12 8.11.6 + one entry. Entry version 2 carries item_type, which
			// ISO/IEC 23000-22 6 defines as 'av01' for an AV1 image item.
			ov::ByteStream infe_payload(32);
			if ((infe_payload.WriteBE16(ITEM_ID) &&							  // item_ID
				 infe_payload.WriteBE16(0) &&								  // item_protection_index (0 = unprotected)
				 infe_payload.WriteText("av01") &&							  // item_type
				 infe_payload.WriteBE(static_cast<uint8_t>(0))) == false)	  // item_name (empty, null-terminated)
			{
				return false;
			}

			ov::ByteStream payload(64);

			return payload.WriteBE16(1) &&	// entry_count
				   WriteFullBox(payload, "infe", 2, 0, *infe_payload.GetData()) &&
				   WriteFullBox(stream, "iinf", 0, 0, *payload.GetData());
		}

		bool WriteIspeBox(ov::ByteStream &stream, const StillInfo &info)
		{
			// ISO/IEC 23008-12 6.5.3 ImageSpatialExtentsProperty
			ov::ByteStream payload(8);

			return payload.WriteBE32(info.width) &&
				   payload.WriteBE32(info.height) &&
				   WriteFullBox(stream, "ispe", 0, 0, *payload.GetData());
		}

		bool WritePixiBox(ov::ByteStream &stream, const StillInfo &info)
		{
			// ISO/IEC 23008-12 6.5.6 PixelInformationProperty
			ov::ByteStream payload(8);

			if (payload.WriteBE(info.channels) == false)
			{
				return false;
			}

			for (uint8_t i = 0; i < info.channels; i++)
			{
				if (payload.WriteBE(info.bit_depth) == false)
				{
					return false;
				}
			}

			return WriteFullBox(stream, "pixi", 0, 0, *payload.GetData());
		}

		bool WriteColrBox(ov::ByteStream &stream, const StillInfo &info)
		{
			// ISO/IEC 14496-12 12.1.5, 'nclx' form: restates the CICP the sequence header
			// signals, so a reader knows the colour without decoding the bitstream first.
			ov::ByteStream payload(16);

			return payload.WriteText("nclx") &&	 // colour_type
				   payload.WriteBE16(info.color_primaries) &&
				   payload.WriteBE16(info.transfer_characteristics) &&
				   payload.WriteBE16(info.matrix_coefficients) &&
				   payload.WriteBE(static_cast<uint8_t>(info.full_range ? 0x80 : 0x00)) &&	// full_range_flag(1), reserved(7)
				   WriteBox(stream, "colr", *payload.GetData());
		}

		bool WriteIprpBox(ov::ByteStream &stream, const StillInfo &info)
		{
			// ISO/IEC 23008-12 9.3: the property container plus the item-to-property associations.
			ov::ByteStream ipco_payload(320);
			if ((WriteBox(ipco_payload, "av1C", *info.av1c) &&	// AV1 ISOBMFF binding 2.3
				 WriteIspeBox(ipco_payload, info) &&
				 WritePixiBox(ipco_payload, info) &&
				 WriteColrBox(ipco_payload, info)) == false)
			{
				return false;
			}

			// version 0 + flags 0 selects 16-bit item IDs and 8-bit associations. av1C is the
			// only essential property: without it the payload cannot be decoded.
			ov::ByteStream ipma_payload(32);
			if ((ipma_payload.WriteBE32(1) &&																 // entry_count
				 ipma_payload.WriteBE16(ITEM_ID) &&															 // item_ID
				 ipma_payload.WriteBE(static_cast<uint8_t>(4)) &&											 // association_count
				 ipma_payload.WriteBE(static_cast<uint8_t>(PROPERTY_ESSENTIAL | PROPERTY_INDEX_AV1C)) &&
				 ipma_payload.WriteBE(static_cast<uint8_t>(PROPERTY_INDEX_ISPE)) &&
				 ipma_payload.WriteBE(static_cast<uint8_t>(PROPERTY_INDEX_PIXI)) &&
				 ipma_payload.WriteBE(static_cast<uint8_t>(PROPERTY_INDEX_COLR))) == false)
			{
				return false;
			}

			ov::ByteStream payload(384);

			return WriteBox(payload, "ipco", *ipco_payload.GetData()) &&
				   WriteFullBox(payload, "ipma", 0, 0, *ipma_payload.GetData()) &&
				   WriteBox(stream, "iprp", *payload.GetData());
		}

		bool WriteMetaBox(ov::ByteStream &stream, const StillInfo &info, uint32_t payload_offset, uint32_t payload_length)
		{
			// ISO/IEC 14496-12 8.11.1: the whole description of the image item. ISO/IEC 23008-12
			// 6.2 requires hdlr to be the first contained box.
			ov::ByteStream payload(512);

			return WriteHdlrBox(payload) &&
				   WritePitmBox(payload) &&
				   WriteIlocBox(payload, payload_offset, payload_length) &&
				   WriteIinfBox(payload) &&
				   WriteIprpBox(payload, info) &&
				   WriteFullBox(stream, "meta", 0, 0, *payload.GetData());
		}

		std::optional<StillInfo> ReadStillInfo(const std::shared_ptr<const ov::Data> &av1_temporal_unit)
		{
			// The still must carry its own sequence header: it is both the av1C configOBUs and
			// the source of every value the container restates.
			auto sequence_header = Av1Parser::ExtractFirstSequenceHeaderObuRaw(av1_temporal_unit);
			auto summary		 = Av1Parser::ParseSequenceHeaderSummary(Av1Parser::ExtractFirstSequenceHeaderObu(av1_temporal_unit));
			if ((sequence_header == nullptr) || (summary.has_value() == false))
			{
				return std::nullopt;
			}

			AV1DecoderConfigurationRecord record;
			record.SetSeqProfile(summary->seq_profile);
			record.SetSeqLevelIdx0(summary->seq_level_idx_0);
			record.SetSeqTier0(summary->seq_tier_0);
			record.SetHighBitdepth(summary->high_bitdepth);
			record.SetTwelveBit(summary->twelve_bit);
			record.SetMonochrome(summary->monochrome);
			record.SetChromaSubsamplingX(summary->chroma_subsampling_x);
			record.SetChromaSubsamplingY(summary->chroma_subsampling_y);
			record.SetChromaSamplePosition(summary->chroma_sample_position);
			// A still is decodable on its own, so there is no presentation delay to signal.
			record.SetInitialPresentationDelay(false, 0);
			record.SetConfigObus(sequence_header->Clone());

			auto av1c = record.Serialize();
			if (av1c == nullptr)
			{
				return std::nullopt;
			}

			StillInfo info;
			info.width					 = summary->max_frame_width;
			info.height					 = summary->max_frame_height;
			info.channels				 = (summary->monochrome != 0) ? 1 : 3;
			info.bit_depth				 = record.BitDepth();
			info.color_primaries		 = summary->color_primaries;
			info.transfer_characteristics = summary->transfer_characteristics;
			info.matrix_coefficients	 = summary->matrix_coefficients;
			info.full_range				 = (summary->color_range != 0);
			info.av1c					 = av1c;

			if ((info.width == 0) || (info.height == 0))
			{
				return std::nullopt;
			}

			return info;
		}
	}  // namespace

	std::shared_ptr<ov::Data> Packager::Pack(const std::shared_ptr<const ov::Data> &av1_temporal_unit)
	{
		if ((av1_temporal_unit == nullptr) || (av1_temporal_unit->GetLength() == 0))
		{
			return nullptr;
		}

		// The iloc extent fields are 32-bit, and mdat's size field must hold the payload plus
		// its own box header.
		if (av1_temporal_unit->GetLength() > (UINT32_MAX - BOX_HEADER_SIZE))
		{
			logtw("AV1 still of %zu bytes is too large to address from an iloc extent", av1_temporal_unit->GetLength());
			return nullptr;
		}

		auto info = ReadStillInfo(av1_temporal_unit);
		if (info.has_value() == false)
		{
			logtw("Could not read the sequence header of the AV1 still; it cannot be wrapped into AVIF");
			return nullptr;
		}

		// An image item must be decodable on its own, so the temporal unit has to code a key
		// frame - an inter frame would reference frames the file does not carry.
		if (Av1Parser::IsKeyFrame(av1_temporal_unit) == false)
		{
			logtw("The AV1 still does not code a key frame; it cannot be wrapped into AVIF");
			return nullptr;
		}

		const auto payload_length = static_cast<uint32_t>(av1_temporal_unit->GetLength());

		ov::ByteStream measured_meta(1024);
		if (WriteMetaBox(measured_meta, info.value(), 0, payload_length) == false)
		{
			logte("Could not build the AVIF meta box");
			return nullptr;
		}

		ov::ByteStream file(measured_meta.GetLength() + payload_length + 64);
		if (WriteFtypBox(file) == false)
		{
			logte("Could not build the AVIF ftyp box");
			return nullptr;
		}

		const auto payload_offset = static_cast<uint32_t>(file.GetLength() + measured_meta.GetLength() + BOX_HEADER_SIZE);
		if (WriteMetaBox(file, info.value(), payload_offset, payload_length) == false ||
			file.GetLength() != payload_offset - BOX_HEADER_SIZE)
		{
			logte("Could not build the AVIF meta box");
			return nullptr;
		}

		// The AV1 temporal unit, unmodified.
		if (WriteBox(file, "mdat", *av1_temporal_unit) == false)
		{
			logte("Could not build the AVIF mdat box");
			return nullptr;
		}

		return file.GetDataPointer();
	}
}  // namespace avif
