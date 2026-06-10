//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "av1_decoder_configuration_record.h"

#include <cstring>

#include <base/common_types.h>
#include <base/ovlibrary/ovlibrary.h>

#include "av1_parser.h"
#include "av1_types.h"

#define OV_LOG_TAG "AV1DecoderConfigurationRecord"

bool AV1DecoderConfigurationRecord::IsValid() const
{
	return _parsed && _marker == 1 && _version == 1;
}

ov::String AV1DecoderConfigurationRecord::GetCodecsParameter() const
{
	// RFC 6381 / AV1 ISO BMFF spec:
	// `av01.<profile>.<level_idx><tier>.<bitDepth>`
	// `tier`: `'M'` (main) or `'H'` (high)
	// `bitDepth`: `08`, `10`, or `12`

	const char tier_char = (_seq_tier_0 == 0) ? 'M' : 'H';
	const uint8_t bit_depth = BitDepth();

	return ov::String::FormatString(
		"av01.%u.%02u%c.%02u",
		static_cast<uint32_t>(_seq_profile),
		static_cast<uint32_t>(_seq_level_idx_0),
		tier_char,
		static_cast<uint32_t>(bit_depth));
}

uint8_t AV1DecoderConfigurationRecord::BitDepth() const
{
	if (_seq_profile == 2 && _high_bitdepth == 1)
	{
		return _twelve_bit ? 12 : 10;
	}

	return _high_bitdepth ? 10 : 8;
}

bool AV1DecoderConfigurationRecord::Parse(const uint8_t *data, size_t length)
{
	// Invalidate any prior parse state up front.
	// A re-parse on a reused instance must not leave the previous result observable:
	// without this, a failure at any point below would leave `_parsed` (hence `IsValid()`) `true`
	// and `GetData()` returning bytes cached by an earlier successful parse.
	// `UpdateData()` drops the cached serialized buffer so `GetData()` no longer returns stale bytes.
	// `_config_obus` is only assigned after the fixed header is fully read, so an early failure would
	// otherwise leave the previous parse's OBU buffer reachable through `ConfigObus()`; clear it too.
	_parsed		 = false;
	_config_obus = nullptr;
	UpdateData();

	if ((data == nullptr) || (length < MIN_AV1DECODERCONFIGURATIONRECORD_SIZE))
	{
		return false;
	}

	BitReader parser(data, length);

	// AV1 ISOBMFF binding `AV1CodecConfigurationRecord` (`av1C` box).
	_marker = parser.ReadBits<uint8_t>(1);							// marker
	_version = parser.ReadBits<uint8_t>(7);							// version

	if (_marker != 1 || _version != 1)
	{
		return false;
	}

	_seq_profile = parser.ReadBits<uint8_t>(3);						// seq_profile
	_seq_level_idx_0 = parser.ReadBits<uint8_t>(5);					// seq_level_idx_0

	// AV1 spec 6.4.1 `seq_profile` semantics: "seq_profile shall be in the range of 0 to 2." The
	// values 3..7 are reserved (Table 4 of section 6.4.1). Reject reserved encodings.
	if (_seq_profile > 2)
	{
		return false;
	}

	_seq_tier_0 = parser.ReadBits<uint8_t>(1);						// seq_tier_0
	_high_bitdepth = parser.ReadBits<uint8_t>(1);					// high_bitdepth
	_twelve_bit = parser.ReadBits<uint8_t>(1);						// twelve_bit
	_monochrome = parser.ReadBits<uint8_t>(1);						// monochrome
	_chroma_subsampling_x = parser.ReadBits<uint8_t>(1);			// chroma_subsampling_x
	_chroma_subsampling_y = parser.ReadBits<uint8_t>(1);			// chroma_subsampling_y
	_chroma_sample_position = parser.ReadBits<uint8_t>(2);			// chroma_sample_position

	// AV1 spec 6.4.2 Table 8 `chroma_sample_position` semantics: value 3 (CSP_RESERVED) is reserved
	// for future use. Reject any record that encodes the reserved chroma sample position.
	if (_chroma_sample_position == 3)
	{
		return false;
	}

	// AV1 ISOBMFF binding v1.2.0 section 2.3.1: the 3-bit `reserved` field SHALL be 0. Reject any
	// av1C blob that carries non-zero reserved bits so the strict parser does not silently accept
	// malformed records (same policy as the OBU header reserved-bit reject in `av1_parser.cpp`).
	const uint8_t reserved_3 = parser.ReadBits<uint8_t>(3);			// reserved
	if (reserved_3 != 0)
	{
		return false;
	}

	_initial_presentation_delay_present = parser.ReadBits<uint8_t>(1);	// initial_presentation_delay_present
	if (_initial_presentation_delay_present)
	{
		_initial_presentation_delay_minus_one = parser.ReadBits<uint8_t>(4);	// initial_presentation_delay_minus_one
	}
	else
	{
		// AV1 ISOBMFF binding v1.2.0 section 2.3.1: the trailing 4-bit `reserved` field (present
		// only when `initial_presentation_delay_present == 0`) SHALL be 0.
		const uint8_t reserved_4 = parser.ReadBits<uint8_t>(4);		// reserved
		if (reserved_4 != 0)
		{
			return false;
		}
		_initial_presentation_delay_minus_one = 0;
	}

	const auto remaining = parser.BytesRemained();
	if (remaining > 0)
	{
		_config_obus = std::make_shared<ov::Data>(parser.CurrentPosition(), remaining);
	}
	else
	{
		_config_obus = nullptr;
	}

	// AV1 ISOBMFF binding v1.2.0 4.2.1:
	//   "The `configOBUs` field SHALL contain zero or more OBUs..."
	//   "At most one Sequence Header OBU SHALL be present..."
	//   "If present, the Sequence Header OBU SHALL be the first OBU."
	//   "If a Sequence Header OBU is present in `configOBUs`, the values of the fields in
	//    `AV1CodecConfigurationRecord` SHALL match those of the Sequence Header OBU."
	if (_config_obus != nullptr && _config_obus->GetLength() > 0)
	{
		if (ValidateConfigObus() == false)
		{
			return false;
		}
	}

	_parsed = true;

	return true;
}

bool AV1DecoderConfigurationRecord::ValidateConfigObus()
{
	const auto *base   = _config_obus->GetDataAs<uint8_t>();
	const size_t total = _config_obus->GetLength();
	size_t offset	   = 0;
	size_t obu_index   = 0;
	bool seen_seq_hdr  = false;

	while (offset < total)
	{
		auto parsed = Av1Parser::ParseObuHeader(base + offset, total - offset);
		if (parsed.has_value() == false)
		{
			return false;
		}

		const auto &header	  = parsed->header;
		size_t payload_offset = offset + parsed->bytes_consumed;
		size_t payload_size	  = 0;

		// AV1 ISOBMFF binding v1.2.0: `configOBUs` is a size-delimited OBU sequence; every OBU
		// in the field SHALL set `obu_has_size_field = 1`. This is distinct from the in-band
		// low-overhead bitstream format walked by `Av1Parser::ExtractFirstSequenceHeaderObu()`,
		// which tolerates a leading `obu_has_size_field = 0` `TemporalDelimiter` (spec 5.6:
		// "Note: The temporal delimiter has an empty payload.") because its payload size is
		// statically zero. Inside `configOBUs` there is no such heuristic - absorbing the
		// remainder as one anonymous payload would silently swallow follow-up OBUs and bypass
		// the cross-check rules below, so reject immediately.
		if (header.has_size_field == false)
		{
			return false;
		}

		auto leb = Av1Parser::DecodeLeb128(base + payload_offset, total - payload_offset);
		if (leb.has_value() == false)
		{
			return false;
		}

		payload_offset += leb->bytes_consumed;

		if (leb->value > total - payload_offset)
		{
			return false;
		}

		payload_size = static_cast<size_t>(leb->value);

		if (header.type == Av1ObuType::SequenceHeader)
		{
			// Ordering rule: must be the first OBU in `configOBUs`.
			if (obu_index != 0)
			{
				return false;
			}
			// Cardinality rule: at most one Sequence Header OBU.
			if (seen_seq_hdr)
			{
				return false;
			}
			seen_seq_hdr = true;

			// Cross-check rule: Sequence Header OBU fields SHALL match the `av1C` fixed fields.
			//
			// AV1 ISOBMFF binding v1.2.0 section 2.3.2: "When the configOBUs field contains a
			// Sequence Header OBU, the values of the AV1CodecConfigurationRecord fields shall
			// match those of the OBU. Specifically: seq_profile, seq_level_idx_0, seq_tier_0,
			// high_bitdepth, twelve_bit, monochrome, chroma_subsampling_x, chroma_subsampling_y,
			// chroma_sample_position (when not zero), and initial_presentation_delay_minus_one,
			// when present, all shall match."
			auto summary = Av1Parser::ParseSequenceHeaderSummary(base + payload_offset, payload_size);
			if (summary.has_value() == false)
			{
				return false;
			}
			if (summary->seq_profile != _seq_profile)
			{
				return false;
			}
			if (summary->seq_level_idx_0 != _seq_level_idx_0)
			{
				return false;
			}
			if (summary->seq_tier_0 != _seq_tier_0)
			{
				return false;
			}
			if (summary->high_bitdepth != _high_bitdepth)
			{
				return false;
			}
			if (summary->twelve_bit != _twelve_bit)
			{
				return false;
			}
			if (summary->monochrome != _monochrome)
			{
				return false;
			}
			if (summary->chroma_subsampling_x != _chroma_subsampling_x)
			{
				return false;
			}
			if (summary->chroma_subsampling_y != _chroma_subsampling_y)
			{
				return false;
			}
			// Spec exception "(when not zero)" - skip the cross-check whenever the av1C value is 0.
			if ((_chroma_sample_position != 0) && (summary->chroma_sample_position != _chroma_sample_position))
			{
				return false;
			}

			// AV1 ISOBMFF binding v1.2.0 section 2.3.2: "initial_presentation_delay_minus_one,
			// when present, all shall match." The av1C `initial_presentation_delay_present`
			// flag must equal the Sequence Header's `initial_display_delay_present_for_op_0`,
			// and when both are 1 the `_minus_one` value must match.
			if (summary->initial_display_delay_present_for_op_0 != _initial_presentation_delay_present)
			{
				return false;
			}
			if ((_initial_presentation_delay_present != 0) &&
				(summary->initial_display_delay_minus_1_for_op_0 != _initial_presentation_delay_minus_one))
			{
				return false;
			}
		}

		offset = payload_offset + payload_size;
		obu_index++;
	}

	return true;
}

bool AV1DecoderConfigurationRecord::Parse(const std::shared_ptr<const ov::Data> &data)
{
	if (data == nullptr)
	{
		return false;
	}

	if (Parse(data->GetDataAs<uint8_t>(), data->GetLength()) == false)
	{
		return false;
	}

	// Preserve the caller's original buffer as the backing store; raw-pointer overload does not
	// touch `SetData()` so the conv layer is the single source of truth for `GetData()`.
	SetData(data);

	return true;
}

bool AV1DecoderConfigurationRecord::Equals(const std::shared_ptr<DecoderConfigurationRecord> &other)
{
	// AV1 ISOBMFF binding v1.2.0 section 2.3 defines `AV1CodecConfigurationRecord` as the
	// canonical serialized form of the av1C box. Two records are equal iff they would emit
	// identical bytes through that serialization, so compare the serialized buffer end-to-end
	// rather than a hand-picked subset of fixed fields. This catches differences in any of
	// `monochrome`, `chroma_subsampling_x/y`, `chroma_sample_position`,
	// `initial_presentation_delay_present`, `initial_presentation_delay_minus_one`, and the
	// `configOBUs` payload - all of which the previous weak comparison silently ignored.
	if (other == nullptr)
	{
		return false;
	}

	auto other_config = std::dynamic_pointer_cast<AV1DecoderConfigurationRecord>(other);
	if (other_config == nullptr)
	{
		return false;
	}

	auto self_data = GetData();
	auto other_data = other_config->GetData();
	if (self_data == nullptr || other_data == nullptr)
	{
		return false;
	}

	if (self_data->GetLength() != other_data->GetLength())
	{
		return false;
	}

	return std::memcmp(
			   self_data->GetDataAs<uint8_t>(),
			   other_data->GetDataAs<uint8_t>(),
			   self_data->GetLength()) == 0;
}

std::shared_ptr<const ov::Data> AV1DecoderConfigurationRecord::Serialize()
{
	ov::BitWriter bit(MIN_AV1DECODERCONFIGURATIONRECORD_SIZE + (_config_obus ? _config_obus->GetLength() : 0));

	bit.WriteBits(1, _marker);
	bit.WriteBits(7, _version);

	bit.WriteBits(3, _seq_profile);
	bit.WriteBits(5, _seq_level_idx_0);

	bit.WriteBits(1, _seq_tier_0);
	bit.WriteBits(1, _high_bitdepth);
	bit.WriteBits(1, _twelve_bit);
	bit.WriteBits(1, _monochrome);
	bit.WriteBits(1, _chroma_subsampling_x);
	bit.WriteBits(1, _chroma_subsampling_y);
	bit.WriteBits(2, _chroma_sample_position);

	bit.WriteBits(3, 0);
	bit.WriteBits(1, _initial_presentation_delay_present);

	if (_initial_presentation_delay_present)
	{
		bit.WriteBits(4, _initial_presentation_delay_minus_one);
	}
	else
	{
		bit.WriteBits(4, 0);
	}

	if (_config_obus != nullptr && _config_obus->GetLength() > 0)
	{
		if (bit.WriteData(_config_obus->GetDataAs<uint8_t>(), _config_obus->GetLength()) == false)
		{
			return nullptr;
		}
	}

	return bit.GetDataObject();
}

uint8_t AV1DecoderConfigurationRecord::Marker() const { return _marker; }
uint8_t AV1DecoderConfigurationRecord::Version() const { return _version; }
uint8_t AV1DecoderConfigurationRecord::SeqProfile() const { return _seq_profile; }
uint8_t AV1DecoderConfigurationRecord::SeqLevelIdx0() const { return _seq_level_idx_0; }
uint8_t AV1DecoderConfigurationRecord::SeqTier0() const { return _seq_tier_0; }
uint8_t AV1DecoderConfigurationRecord::HighBitdepth() const { return _high_bitdepth; }
uint8_t AV1DecoderConfigurationRecord::TwelveBit() const { return _twelve_bit; }
uint8_t AV1DecoderConfigurationRecord::Monochrome() const { return _monochrome; }
uint8_t AV1DecoderConfigurationRecord::ChromaSubsamplingX() const { return _chroma_subsampling_x; }
uint8_t AV1DecoderConfigurationRecord::ChromaSubsamplingY() const { return _chroma_subsampling_y; }
uint8_t AV1DecoderConfigurationRecord::ChromaSamplePosition() const { return _chroma_sample_position; }
uint8_t AV1DecoderConfigurationRecord::InitialPresentationDelayPresent() const { return _initial_presentation_delay_present; }
uint8_t AV1DecoderConfigurationRecord::InitialPresentationDelayMinusOne() const { return _initial_presentation_delay_minus_one; }

std::shared_ptr<ov::Data> AV1DecoderConfigurationRecord::ConfigObus() const
{
	return _config_obus;
}

void AV1DecoderConfigurationRecord::SetSeqProfile(uint8_t value)
{
	_seq_profile = value;
	UpdateData();
}

void AV1DecoderConfigurationRecord::SetSeqLevelIdx0(uint8_t value)
{
	_seq_level_idx_0 = value;
	UpdateData();
}

void AV1DecoderConfigurationRecord::SetSeqTier0(uint8_t value)
{
	_seq_tier_0 = value;
	UpdateData();
}

void AV1DecoderConfigurationRecord::SetHighBitdepth(uint8_t value)
{
	_high_bitdepth = value;
	UpdateData();
}

void AV1DecoderConfigurationRecord::SetTwelveBit(uint8_t value)
{
	_twelve_bit = value;
	UpdateData();
}

void AV1DecoderConfigurationRecord::SetMonochrome(uint8_t value)
{
	_monochrome = value;
	UpdateData();
}

void AV1DecoderConfigurationRecord::SetChromaSubsamplingX(uint8_t value)
{
	_chroma_subsampling_x = value;
	UpdateData();
}

void AV1DecoderConfigurationRecord::SetChromaSubsamplingY(uint8_t value)
{
	_chroma_subsampling_y = value;
	UpdateData();
}

void AV1DecoderConfigurationRecord::SetChromaSamplePosition(uint8_t value)
{
	_chroma_sample_position = value;
	UpdateData();
}

void AV1DecoderConfigurationRecord::SetInitialPresentationDelay(bool present, uint8_t minus_one)
{
	_initial_presentation_delay_present = present ? 1 : 0;
	_initial_presentation_delay_minus_one = present ? (minus_one & 0x0F) : 0;
	UpdateData();
}

void AV1DecoderConfigurationRecord::SetConfigObus(const std::shared_ptr<ov::Data> &config_obus)
{
	_config_obus = config_obus;
	UpdateData();
}
