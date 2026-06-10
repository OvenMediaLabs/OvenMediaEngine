//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <stdint.h>

// AV1 Bitstream & Decoding Process Specification, section 6.2.1
// (https://aomediacodec.github.io/av1-spec/av1-spec.pdf)
enum class Av1ObuType : uint8_t
{
	Reserved = 0,
	SequenceHeader = 1,
	TemporalDelimiter = 2,
	FrameHeader = 3,
	TileGroup = 4,
	Metadata = 5,
	Frame = 6,
	RedundantFrameHeader = 7,
	TileList = 8,
	Padding = 15,
};

constexpr const char *EnumToString(Av1ObuType obu_type)
{
	switch (obu_type)
	{
		OV_CASE_RETURN_ENUM_STRING(Av1ObuType, Reserved);
		OV_CASE_RETURN_ENUM_STRING(Av1ObuType, SequenceHeader);
		OV_CASE_RETURN_ENUM_STRING(Av1ObuType, TemporalDelimiter);
		OV_CASE_RETURN_ENUM_STRING(Av1ObuType, FrameHeader);
		OV_CASE_RETURN_ENUM_STRING(Av1ObuType, TileGroup);
		OV_CASE_RETURN_ENUM_STRING(Av1ObuType, Metadata);
		OV_CASE_RETURN_ENUM_STRING(Av1ObuType, Frame);
		OV_CASE_RETURN_ENUM_STRING(Av1ObuType, RedundantFrameHeader);
		OV_CASE_RETURN_ENUM_STRING(Av1ObuType, TileList);
		OV_CASE_RETURN_ENUM_STRING(Av1ObuType, Padding);
	}

	return "(Unknown)";
}

// AV1 spec 5.3.2 `obu_header()`
struct Av1ObuHeader
{
	Av1ObuType type = Av1ObuType::Reserved;
	bool extension_flag = false;
	bool has_size_field = false;
	uint8_t temporal_id = 0;
	uint8_t spatial_id = 0;
};

/// Result of `Av1Parser::DecodeLeb128()` - decoded value plus byte count.
struct DecodedLeb128
{
	uint64_t value = 0;
	size_t bytes_consumed = 0;
};

/// Result of the raw-byte `Av1Parser::ParseObuHeader()` overload - parsed header plus the number of bytes consumed
/// (1 byte for the base header, 2 bytes when `obu_extension_flag` is set).
struct ParsedObuHeader
{
	Av1ObuHeader header;
	size_t bytes_consumed = 0;
};

// AV1 spec 5.5.1 `sequence_header_obu()` - only the diagnostic-friendly leading fields, plus the
// `color_config()` (spec 5.5.2) values needed for the `configOBUs` cross-check defined by AV1 ISOBMFF
// binding v1.2.0 section 2.3.2.
struct Av1SequenceHeaderSummary
{
	uint8_t seq_profile = 0;
	bool still_picture = false;
	bool reduced_still_picture_header = false;
	uint8_t seq_level_idx_0 = 0;
	uint8_t seq_tier_0 = 0;
	uint32_t max_frame_width = 0;
	uint32_t max_frame_height = 0;

	// AV1 spec 5.5.2 `color_config()` fields used by AV1 ISOBMFF binding v1.2.0 section 2.3.2
	// "When the configOBUs field contains a Sequence Header OBU, the values of the
	// AV1CodecConfigurationRecord fields shall match those of the OBU."
	uint8_t high_bitdepth = 0;
	uint8_t twelve_bit = 0;
	uint8_t monochrome = 0;
	uint8_t chroma_subsampling_x = 0;
	uint8_t chroma_subsampling_y = 0;
	uint8_t chroma_sample_position = 0;

	// AV1 spec 5.5.1 `sequence_header_obu()` operating-point `i == 0` initial display delay signaling.
	// Recorded so AV1 ISOBMFF binding v1.2.0 section 2.3.2 can cross-check
	// `initial_presentation_delay_minus_one` from the `av1C` fixed header:
	//   "initial_presentation_delay_minus_one, when present, all shall match."
	uint8_t initial_display_delay_present_for_op_0 = 0;
	uint8_t initial_display_delay_minus_1_for_op_0 = 0;

	bool parsed = false;
};
