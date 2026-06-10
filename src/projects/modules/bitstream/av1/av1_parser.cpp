//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "av1_parser.h"

#define OV_LOG_TAG "Av1Parser"

#define AV1_READ_BITS(type, value, bits)           \
	type value;                                    \
	if (reader.ReadBits(bits, value) == false)     \
	{                                              \
		return std::nullopt;                       \
	}

std::optional<DecodedLeb128> Av1Parser::DecodeLeb128(const uint8_t *data, size_t size)
{
	if (data == nullptr)
	{
		return std::nullopt;
	}

	uint64_t result = 0;
	size_t i		= 0;

	// AV1 spec 4.10.5: at most 8 bytes; low 7 bits per byte; MSB (`0x80`) indicates continuation.
	for (; i < 8; i++)
	{
		if (i >= size)
		{
			return std::nullopt;
		}

		const uint8_t byte = data[i];
		result |= static_cast<uint64_t>(byte & 0x7F) << (i * 7);

		if ((byte & 0x80) == 0)
		{
			DecodedLeb128 decoded;
			decoded.value		   = result;
			decoded.bytes_consumed = i + 1;
			return decoded;
		}
	}

	// Reached 8 bytes without a terminator.
	return std::nullopt;
}

std::optional<Av1ObuHeader> Av1Parser::ParseObuHeader(BitReader &reader)
{
	if (reader.BytesRemained() < 1)
	{
		return std::nullopt;
	}

	// AV1 spec 5.3.2 `obu_header()`
	AV1_READ_BITS(uint8_t, forbidden_bit, 1);		// obu_forbidden_bit
	if (forbidden_bit != 0)
	{
		return std::nullopt;
	}

	AV1_READ_BITS(uint8_t, obu_type, 4);				// obu_type
	AV1_READ_BITS(uint8_t, extension_flag, 1);		// obu_extension_flag
	AV1_READ_BITS(uint8_t, has_size_field, 1);		// obu_has_size_field
	AV1_READ_BITS(uint8_t, reserved_bit, 1);			// obu_reserved_1bit

	// AV1 spec 5.3.2 Table 5: "It is a requirement of bitstream conformance that the value of obu_type
	// is not equal to one of the reserved values." Reserved values per Table 5 are 0 and 9..14.
	if (obu_type == 0 || (obu_type >= 9 && obu_type <= 14))
	{
		return std::nullopt;
	}

	// AV1 spec 5.3.2: "obu_reserved_1bit ... shall be set to 0." Reject any OBU header that violates
	// this conformance requirement so the strict parser does not silently accept malformed bitstreams.
	if (reserved_bit != 0)
	{
		return std::nullopt;
	}

	Av1ObuHeader out;
	out.type			= static_cast<Av1ObuType>(obu_type);
	out.extension_flag	= (extension_flag != 0);
	out.has_size_field	= (has_size_field != 0);
	out.temporal_id		= 0;
	out.spatial_id		= 0;

	if (out.extension_flag)
	{
		if (reader.BytesRemained() < 1)
		{
			return std::nullopt;
		}

		// AV1 spec 5.3.3 `obu_extension_header()`
		AV1_READ_BITS(uint8_t, temporal_id, 3);				// temporal_id
		AV1_READ_BITS(uint8_t, spatial_id, 2);				// spatial_id
		AV1_READ_BITS(uint8_t, ext_reserved, 3);				// extension_header_reserved_3bits

		// AV1 spec 5.3.3: "extension_header_reserved_3bits ... shall be set to 0." Reject when the
		// reserved bits carry any non-zero value.
		if (ext_reserved != 0)
		{
			return std::nullopt;
		}

		out.temporal_id = temporal_id;
		out.spatial_id	= spatial_id;
	}

	return out;
}

std::optional<ParsedObuHeader> Av1Parser::ParseObuHeader(const uint8_t *data, size_t size)
{
	if (data == nullptr)
	{
		return std::nullopt;
	}

	BitReader reader(data, size);
	auto header = ParseObuHeader(reader);
	if (header.has_value() == false)
	{
		return std::nullopt;
	}

	ParsedObuHeader parsed;
	parsed.header		  = *header;
	parsed.bytes_consumed = reader.BytesConsumed();
	return parsed;
}

std::shared_ptr<const ov::Data> Av1Parser::ExtractFirstSequenceHeaderObu(const std::shared_ptr<const ov::Data> &config_obus)
{
	if (config_obus == nullptr || config_obus->GetLength() == 0)
	{
		return nullptr;
	}

	const auto *base   = config_obus->GetDataAs<uint8_t>();
	const size_t total = config_obus->GetLength();
	size_t offset	   = 0;

	while (offset < total)
	{
		auto parsed = ParseObuHeader(base + offset, total - offset);
		if (parsed.has_value() == false)
		{
			return nullptr;
		}

		const auto &header	  = parsed->header;
		size_t payload_offset = offset + parsed->bytes_consumed;
		size_t payload_size	  = 0;

		if (header.has_size_field)
		{
			auto leb = DecodeLeb128(base + payload_offset, total - payload_offset);
			if (leb.has_value() == false)
			{
				return nullptr;
			}

			payload_offset += leb->bytes_consumed;

			if (leb->value > total - payload_offset)
			{
				return nullptr;
			}

			payload_size = static_cast<size_t>(leb->value);
		}
		else
		{
			// AV1 ISO BMFF requires `obu_has_size_field=1` inside `configOBUs`. If we hit an OBU without
			// a size field there's no way to know where the next one starts, so the remainder of the
			// buffer is treated as this OBU's payload.
			//
			// AV1 spec 5.6 `temporal_delimiter_obu()` is defined as a NO-OP: "Note: The temporal delimiter
			// has an empty payload." When the stream uses the low-overhead bitstream format
			// (`obu_has_size_field = 0`) we can still walk past a leading TemporalDelimiter because its
			// payload is guaranteed to be zero bytes. This lets `ExtractFirstSequenceHeaderObu()` reach a
			// subsequent SequenceHeader OBU in an in-band scan.
			if (header.type == Av1ObuType::TemporalDelimiter)
			{
				payload_size = 0;
			}
			else
			{
				payload_size = total - payload_offset;
			}
		}

		if (header.type == Av1ObuType::SequenceHeader)
		{
			if (payload_size == 0)
			{
				return std::make_shared<ov::Data>();
			}
			return std::make_shared<ov::Data>(base + payload_offset, payload_size);
		}

		offset = payload_offset + payload_size;
	}

	return nullptr;
}

namespace
{
	// AV1 spec 4.10.3 `uvlc()`. Reads leading-zero count + value; treats `leadingZeros >= 32` as the
	// "invalid" sentinel (still consumed so the bit offset stays accurate). Returns `false` only when
	// the buffer runs out.
	bool SkipUvlc(BitReader &reader)
	{
		uint8_t leading_zeros = 0;
		while (true)
		{
			uint8_t done = 0;
			if (reader.ReadBits<uint8_t>(1, done) == false)
			{
				return false;
			}
			if (done != 0)
			{
				break;
			}
			leading_zeros++;
			if (leading_zeros >= 32)
			{
				// AV1 spec sentinel: value is `(1 << 32) - 1`, no further value bits to read.
				return true;
			}
		}
		if (leading_zeros == 0)
		{
			return true;
		}
		uint32_t value = 0;
		return reader.ReadBits<uint32_t>(leading_zeros, value);
	}

	// AV1 spec 5.5.2 `timing_info()`. Returns `false` only on buffer underflow.
	bool SkipTimingInfo(BitReader &reader)
	{
		uint32_t num_units_in_display_tick = 0;
		if (reader.ReadBits<uint32_t>(32, num_units_in_display_tick) == false)
		{
			return false;
		}
		uint32_t time_scale = 0;
		if (reader.ReadBits<uint32_t>(32, time_scale) == false)
		{
			return false;
		}
		uint8_t equal_picture_interval = 0;
		if (reader.ReadBits<uint8_t>(1, equal_picture_interval) == false)
		{
			return false;
		}
		if (equal_picture_interval != 0)
		{
			if (SkipUvlc(reader) == false)
			{
				return false;
			}
		}
		return true;
	}

	// AV1 spec 5.5.3 `decoder_model_info()`. The 5-bit `buffer_delay_length_minus_1` field controls
	// per-op `operating_parameters_info()` sizing; the caller captures it via `buffer_delay_length`.
	bool SkipDecoderModelInfo(BitReader &reader, uint8_t &buffer_delay_length)
	{
		uint8_t buffer_delay_length_minus_1 = 0;
		if (reader.ReadBits<uint8_t>(5, buffer_delay_length_minus_1) == false)
		{
			return false;
		}
		buffer_delay_length = buffer_delay_length_minus_1 + 1;

		uint32_t num_units_in_decoding_tick = 0;
		if (reader.ReadBits<uint32_t>(32, num_units_in_decoding_tick) == false)
		{
			return false;
		}
		uint8_t buffer_removal_time_length_minus_1 = 0;
		if (reader.ReadBits<uint8_t>(5, buffer_removal_time_length_minus_1) == false)
		{
			return false;
		}
		uint8_t frame_presentation_time_length_minus_1 = 0;
		if (reader.ReadBits<uint8_t>(5, frame_presentation_time_length_minus_1) == false)
		{
			return false;
		}
		return true;
	}

	// AV1 spec 5.5.5 `operating_parameters_info(op)`: `decoder_buffer_delay(n)`,
	// `encoder_buffer_delay(n)`, `low_delay_mode_flag(1)` where `n = buffer_delay_length`.
	bool SkipOperatingParametersInfo(BitReader &reader, uint8_t buffer_delay_length)
	{
		uint32_t tmp = 0;
		if (reader.ReadBits<uint32_t>(buffer_delay_length, tmp) == false)
		{
			return false;
		}
		if (reader.ReadBits<uint32_t>(buffer_delay_length, tmp) == false)
		{
			return false;
		}
		uint8_t low_delay_mode_flag = 0;
		return reader.ReadBits<uint8_t>(1, low_delay_mode_flag);
	}
}  // namespace

std::optional<Av1SequenceHeaderSummary> Av1Parser::ParseSequenceHeaderSummary(const uint8_t *payload, size_t size)
{
	if ((payload == nullptr) || (size == 0))
	{
		return std::nullopt;
	}

	BitReader reader(payload, size);
	Av1SequenceHeaderSummary out;

	// AV1 spec 5.5.1 `sequence_header_obu()`
	AV1_READ_BITS(uint8_t, seq_profile, 3);						// seq_profile
	AV1_READ_BITS(uint8_t, still_picture, 1);					// still_picture
	AV1_READ_BITS(uint8_t, reduced_still_picture_header, 1);		// reduced_still_picture_header

	out.seq_profile					 = seq_profile;
	out.still_picture				 = (still_picture != 0);
	out.reduced_still_picture_header = (reduced_still_picture_header != 0);

	// AV1 spec 5.5.1: "It is a requirement of bitstream conformance that if reduced_still_picture_header
	// is equal to 1, the value of still_picture shall be equal to 1." Reject the malformed combination.
	if (out.reduced_still_picture_header && (still_picture == 0))
	{
		return std::nullopt;
	}

	uint8_t seq_level_idx_0_value = 0;
	uint8_t seq_tier_0_value = 0;
	if (out.reduced_still_picture_header)
	{
		AV1_READ_BITS(uint8_t, seq_level_idx_0, 5);				// seq_level_idx[0]
		seq_level_idx_0_value = seq_level_idx_0;
	}
	else
	{
		AV1_READ_BITS(uint8_t, timing_info_present_flag, 1);		// timing_info_present_flag

		uint8_t decoder_model_info_present_flag = 0;
		uint8_t buffer_delay_length				= 0;
		if (timing_info_present_flag != 0)
		{
			// Spec 5.5.2 `timing_info()` lives before `decoder_model_info_present_flag`. Skip it
			// with exact bit count so subsequent fields stay aligned.
			if (SkipTimingInfo(reader) == false)
			{
				return std::nullopt;
			}

			AV1_READ_BITS(uint8_t, dmip_flag, 1);				// decoder_model_info_present_flag
			decoder_model_info_present_flag = dmip_flag;
			if (decoder_model_info_present_flag != 0)
			{
				// Spec 5.5.3 `decoder_model_info()`. Captures `buffer_delay_length` for the
				// per-operating-point `operating_parameters_info(i)` sub-tree below.
				if (SkipDecoderModelInfo(reader, buffer_delay_length) == false)
				{
					return std::nullopt;
				}
			}
		}

		AV1_READ_BITS(uint8_t, initial_display_delay_present_flag, 1);	// initial_display_delay_present_flag
		AV1_READ_BITS(uint8_t, operating_points_cnt_minus_1, 5);			// operating_points_cnt_minus_1

		// Spec 5.5.1: walk every operating point so width/height bits are reached at the correct
		// offset. Only the `i == 0` `seq_level_idx` is recorded (matches the `av1C` summary).
		const uint32_t op_count = static_cast<uint32_t>(operating_points_cnt_minus_1) + 1;
		for (uint32_t i = 0; i < op_count; i++)
		{
			AV1_READ_BITS(uint16_t, operating_point_idc, 12);	// operating_point_idc[i]
			AV1_READ_BITS(uint8_t, seq_level_idx, 5);			// seq_level_idx[i]
			(void)operating_point_idc;

			if (i == 0)
			{
				seq_level_idx_0_value = seq_level_idx;
			}

			if (seq_level_idx > 7)
			{
				AV1_READ_BITS(uint8_t, seq_tier, 1);			// seq_tier[i]
				if (i == 0)
				{
					seq_tier_0_value = seq_tier;
				}
			}
			// AV1 spec 5.5.1: "seq_tier[ i ] ... If seq_level_idx[ i ] is less than 8, the value of
			// seq_tier[ i ] is inferred to be 0." `seq_tier_0_value` is already initialized to 0.

			if (decoder_model_info_present_flag != 0)
			{
				AV1_READ_BITS(uint8_t, decoder_model_present_for_this_op, 1);
				if (decoder_model_present_for_this_op != 0)
				{
					if (SkipOperatingParametersInfo(reader, buffer_delay_length) == false)
					{
						return std::nullopt;
					}
				}
			}

			if (initial_display_delay_present_flag != 0)
			{
				AV1_READ_BITS(uint8_t, initial_display_delay_present_for_this_op, 1);
				uint8_t initial_display_delay_minus_1_for_this_op = 0;
				if (initial_display_delay_present_for_this_op != 0)
				{
					AV1_READ_BITS(uint8_t, initial_display_delay_minus_1, 4);
					initial_display_delay_minus_1_for_this_op = initial_display_delay_minus_1;
				}

				// AV1 ISOBMFF binding v1.2.0 section 2.3.2 cross-check requires the `op_0`
				// `initial_display_delay` signaling to match the `av1C` fixed header. Capture
				// the per-op values for `i == 0` only; later operating points are still walked
				// for bit alignment but their values are not recorded.
				if (i == 0)
				{
					out.initial_display_delay_present_for_op_0 = initial_display_delay_present_for_this_op;
					out.initial_display_delay_minus_1_for_op_0 = initial_display_delay_minus_1_for_this_op;
				}
			}
		}
	}

	out.seq_level_idx_0 = seq_level_idx_0_value;
	out.seq_tier_0		= seq_tier_0_value;

	// `frame_width_bits_minus_1` (4), `frame_height_bits_minus_1` (4),
	// `max_frame_width_minus_1` (n), `max_frame_height_minus_1` (n).
	AV1_READ_BITS(uint8_t, frame_width_bits_minus_1, 4);			// frame_width_bits_minus_1
	AV1_READ_BITS(uint8_t, frame_height_bits_minus_1, 4);		// frame_height_bits_minus_1

	const uint8_t width_bits  = frame_width_bits_minus_1 + 1;
	const uint8_t height_bits = frame_height_bits_minus_1 + 1;

	AV1_READ_BITS(uint32_t, max_frame_width_minus_1, width_bits);	// max_frame_width_minus_1
	AV1_READ_BITS(uint32_t, max_frame_height_minus_1, height_bits);	// max_frame_height_minus_1

	out.max_frame_width	 = max_frame_width_minus_1 + 1;
	out.max_frame_height = max_frame_height_minus_1 + 1;

	// AV1 spec 5.5.1 `sequence_header_obu()`: continue past `max_frame_height_minus_1` so the
	// `color_config()` sub-tree can be reached. Cross-checking the configOBUs requires the
	// `color_config()` values per AV1 ISOBMFF binding v1.2.0 section 2.3.2.
	if (out.reduced_still_picture_header == false)
	{
		AV1_READ_BITS(uint8_t, frame_id_numbers_present_flag, 1);
		if (frame_id_numbers_present_flag != 0)
		{
			AV1_READ_BITS(uint8_t, delta_frame_id_length_minus_2, 4);
			AV1_READ_BITS(uint8_t, additional_frame_id_length_minus_1, 3);
			(void)delta_frame_id_length_minus_2;
			(void)additional_frame_id_length_minus_1;
		}
	}
	AV1_READ_BITS(uint8_t, use_128x128_superblock, 1);
	AV1_READ_BITS(uint8_t, enable_filter_intra, 1);
	AV1_READ_BITS(uint8_t, enable_intra_edge_filter, 1);
	(void)use_128x128_superblock;
	(void)enable_filter_intra;
	(void)enable_intra_edge_filter;

	if (out.reduced_still_picture_header == false)
	{
		AV1_READ_BITS(uint8_t, enable_interintra_compound, 1);
		AV1_READ_BITS(uint8_t, enable_masked_compound, 1);
		AV1_READ_BITS(uint8_t, enable_warped_motion, 1);
		AV1_READ_BITS(uint8_t, enable_dual_filter, 1);
		AV1_READ_BITS(uint8_t, enable_order_hint, 1);
		(void)enable_interintra_compound;
		(void)enable_masked_compound;
		(void)enable_warped_motion;
		(void)enable_dual_filter;

		if (enable_order_hint != 0)
		{
			AV1_READ_BITS(uint8_t, enable_jnt_comp, 1);
			AV1_READ_BITS(uint8_t, enable_ref_frame_mvs, 1);
			(void)enable_jnt_comp;
			(void)enable_ref_frame_mvs;
		}

		AV1_READ_BITS(uint8_t, seq_choose_screen_content_tools, 1);
		uint8_t seq_force_screen_content_tools = 2;	// AV1 spec: SELECT_SCREEN_CONTENT_TOOLS
		if (seq_choose_screen_content_tools == 0)
		{
			AV1_READ_BITS(uint8_t, sfsct, 1);
			seq_force_screen_content_tools = sfsct;
		}

		if (seq_force_screen_content_tools > 0)
		{
			AV1_READ_BITS(uint8_t, seq_choose_integer_mv, 1);
			if (seq_choose_integer_mv == 0)
			{
				AV1_READ_BITS(uint8_t, seq_force_integer_mv, 1);
				(void)seq_force_integer_mv;
			}
		}

		if (enable_order_hint != 0)
		{
			AV1_READ_BITS(uint8_t, order_hint_bits_minus_1, 3);
			(void)order_hint_bits_minus_1;
		}
	}

	AV1_READ_BITS(uint8_t, enable_superres, 1);
	AV1_READ_BITS(uint8_t, enable_cdef, 1);
	AV1_READ_BITS(uint8_t, enable_restoration, 1);
	(void)enable_superres;
	(void)enable_cdef;
	(void)enable_restoration;

	// AV1 spec 5.5.2 `color_config()` - capture every field needed by AV1 ISOBMFF binding v1.2.0
	// section 2.3.2 cross-check.
	AV1_READ_BITS(uint8_t, high_bitdepth, 1);
	out.high_bitdepth = high_bitdepth;
	if ((out.seq_profile == 2) && (high_bitdepth != 0))
	{
		AV1_READ_BITS(uint8_t, twelve_bit, 1);
		out.twelve_bit = twelve_bit;
	}
	else
	{
		out.twelve_bit = 0;
	}

	if (out.seq_profile == 1)
	{
		// AV1 spec 5.5.2: "if ( seq_profile == 1 ) { monochrome = 0 }" - implicit, no bits read.
		out.monochrome = 0;
	}
	else
	{
		AV1_READ_BITS(uint8_t, monochrome, 1);
		out.monochrome = monochrome;
	}

	AV1_READ_BITS(uint8_t, color_description_present_flag, 1);
	uint8_t color_primaries			= 2;	// AV1 spec: CP_UNSPECIFIED
	uint8_t transfer_characteristics = 2;	// AV1 spec: TC_UNSPECIFIED
	uint8_t matrix_coefficients		= 2;	// AV1 spec: MC_UNSPECIFIED
	if (color_description_present_flag != 0)
	{
		AV1_READ_BITS(uint8_t, cp, 8);
		AV1_READ_BITS(uint8_t, tc, 8);
		AV1_READ_BITS(uint8_t, mc, 8);
		color_primaries			 = cp;
		transfer_characteristics = tc;
		matrix_coefficients		 = mc;
	}

	if (out.monochrome != 0)
	{
		// AV1 spec 5.5.2 monochrome branch: read color_range, infer subsampling 4:0:0 mapped to (1,1)
		// per spec, `chroma_sample_position = CSP_UNKNOWN (0)`.
		AV1_READ_BITS(uint8_t, color_range_mono, 1);
		(void)color_range_mono;
		out.chroma_subsampling_x	= 1;
		out.chroma_subsampling_y	= 1;
		out.chroma_sample_position	= 0;
	}
	else if ((color_primaries == 1) && (transfer_characteristics == 13) && (matrix_coefficients == 0))
	{
		// AV1 spec 5.5.2: sRGB shortcut - color_range = 1, subsampling 4:4:4.
		out.chroma_subsampling_x	= 0;
		out.chroma_subsampling_y	= 0;
		out.chroma_sample_position	= 0;
	}
	else
	{
		AV1_READ_BITS(uint8_t, color_range_full, 1);
		(void)color_range_full;
		if (out.seq_profile == 0)
		{
			out.chroma_subsampling_x = 1;
			out.chroma_subsampling_y = 1;
		}
		else if (out.seq_profile == 1)
		{
			out.chroma_subsampling_x = 0;
			out.chroma_subsampling_y = 0;
		}
		else	// seq_profile == 2
		{
			const uint8_t bit_depth = (high_bitdepth != 0) ? ((out.twelve_bit != 0) ? 12 : 10) : 8;
			if (bit_depth == 12)
			{
				AV1_READ_BITS(uint8_t, ss_x, 1);
				out.chroma_subsampling_x = ss_x;
				if (ss_x != 0)
				{
					AV1_READ_BITS(uint8_t, ss_y, 1);
					out.chroma_subsampling_y = ss_y;
				}
				else
				{
					out.chroma_subsampling_y = 0;
				}
			}
			else
			{
				out.chroma_subsampling_x = 1;
				out.chroma_subsampling_y = 0;
			}
		}

		if ((out.chroma_subsampling_x != 0) && (out.chroma_subsampling_y != 0))
		{
			AV1_READ_BITS(uint8_t, csp, 2);
			out.chroma_sample_position = csp;
		}
		else
		{
			out.chroma_sample_position = 0;
		}
	}

	out.parsed = true;
	return out;
}
