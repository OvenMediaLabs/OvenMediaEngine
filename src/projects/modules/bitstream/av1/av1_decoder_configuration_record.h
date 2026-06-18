//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/common_types.h>
#include <base/info/decoder_configuration_record.h>
#include <base/ovlibrary/ovlibrary.h>

// https://aomediacodec.github.io/av1-isobmff/#av1codecconfigurationbox-syntax
//
// aligned(8) class AV1CodecConfigurationRecord {
//     unsigned int(1) marker = 1;
//     unsigned int(7) version = 1;
//     unsigned int(3) seq_profile;
//     unsigned int(5) seq_level_idx_0;
//     unsigned int(1) seq_tier_0;
//     unsigned int(1) high_bitdepth;
//     unsigned int(1) twelve_bit;
//     unsigned int(1) monochrome;
//     unsigned int(1) chroma_subsampling_x;
//     unsigned int(1) chroma_subsampling_y;
//     unsigned int(2) chroma_sample_position;
//     unsigned int(3) reserved = 0;
//     unsigned int(1) initial_presentation_delay_present;
//     if (initial_presentation_delay_present) {
//         unsigned int(4) initial_presentation_delay_minus_one;
//     } else {
//         unsigned int(4) reserved = 0;
//     }
//     unsigned int(8) configOBUs[];
// }

#define MIN_AV1DECODERCONFIGURATIONRECORD_SIZE 4

class AV1DecoderConfigurationRecord : public DecoderConfigurationRecord
{
public:
	/// Test whether the record passes the mandatory `marker == 1` / `version == 1` checks per AV1 ISOBMFF binding.
	///
	/// @return `true` if the record is structurally well-formed.
	bool IsValid() const override;

	/// Build an RFC 6381 `codecs` parameter string for this record.
	///
	/// Example output: `av01.0.04M.08`.
	///
	/// @return The MIME `codecs` parameter.
	ov::String GetCodecsParameter() const override;

	/// Parse a serialized `AV1CodecConfigurationRecord` (`av1C` box) per AOMedia ISOBMFF binding.
	///
	/// @param data Pointer to the raw `av1C` blob bytes (first byte must be `0x81`).
	///
	/// @param length Number of bytes available at `data` (>= `MIN_AV1DECODERCONFIGURATIONRECORD_SIZE`).
	///
	/// @return `true` if the blob is well-formed and all fields were extracted.
	bool Parse(const uint8_t *data, size_t length);

	/// Convenience overload that accepts an `ov::Data`. Backing storage is preserved on success
	/// so `GetData()` returns the caller's original buffer.
	///
	/// @param data Raw `av1C` blob; must not be `nullptr` and must satisfy the size minimum.
	///
	/// @return `true` if the blob is well-formed and all fields were extracted.
	bool Parse(const std::shared_ptr<const ov::Data> &data) override;

	/// Compare this record against another for byte-for-byte equality of the serialized form.
	///
	/// @param other Other configuration record.
	///
	/// @return `true` if both records would serialize to identical bytes.
	bool Equals(const std::shared_ptr<DecoderConfigurationRecord> &other) override;

	/// Serialize this record back to its `av1C` byte form per AOMedia ISOBMFF binding.
	///
	/// @return Newly allocated buffer holding the serialized `av1C` blob.
	std::shared_ptr<const ov::Data> Serialize() override;

	uint8_t Marker() const;
	uint8_t Version() const;
	uint8_t SeqProfile() const;
	uint8_t SeqLevelIdx0() const;
	uint8_t SeqTier0() const;
	uint8_t HighBitdepth() const;
	uint8_t TwelveBit() const;
	uint8_t Monochrome() const;
	uint8_t ChromaSubsamplingX() const;
	uint8_t ChromaSubsamplingY() const;
	uint8_t ChromaSamplePosition() const;
	uint8_t InitialPresentationDelayPresent() const;
	uint8_t InitialPresentationDelayMinusOne() const;

	/// Whether a Sequence Header OBU was found in `configOBUs` and its `color_config()` captured.
	///
	/// When `false`, the color accessors below return their unspecified defaults and callers should
	/// fall back to their own default (the av1C fixed fields do not carry CICP). `color_config()` is
	/// only present in the in-band/`configOBUs` Sequence Header, not the av1C header itself.
	///
	/// @return `true` if `color_config()` was parsed from a `configOBUs` Sequence Header OBU.
	bool ColorConfigParsed() const;

	/// AV1 `color_config()` CICP fields (ISO/IEC 23091-2), valid only when `ColorConfigParsed()`.
	/// Primaries/transfer/matrix are `*_UNSPECIFIED` (2) when the source omits `color_description`.
	/// `ColorRange()` is `0` (studio/limited) or `1` (full).
	uint8_t ColorDescriptionPresent() const;
	uint8_t ColorPrimaries() const;
	uint8_t TransferCharacteristics() const;
	uint8_t MatrixCoefficients() const;
	uint8_t ColorRange() const;

	/// Return the `configOBUs` payload (zero or more concatenated AV1 OBUs).
	///
	/// `nullptr` is returned when the record carries no `configOBUs` - e.g. a minimal `av1C` whose
	/// sequence header OBU is delivered in-band rather than inside the box. A successful `Parse` does
	/// NOT guarantee a non-null result.
	///
	/// @return The `configOBUs` byte buffer, or `nullptr` when the record has no `configOBUs`.
	std::shared_ptr<ov::Data> ConfigObus() const;

	void SetSeqProfile(uint8_t value);
	void SetSeqLevelIdx0(uint8_t value);
	void SetSeqTier0(uint8_t value);
	void SetHighBitdepth(uint8_t value);
	void SetTwelveBit(uint8_t value);
	void SetMonochrome(uint8_t value);
	void SetChromaSubsamplingX(uint8_t value);
	void SetChromaSubsamplingY(uint8_t value);
	void SetChromaSamplePosition(uint8_t value);

	/// Set `initial_presentation_delay_present` and (when `present`) the associated `initial_presentation_delay_minus_one`.
	///
	/// @param present `true` to encode the optional delay field.
	///
	/// @param minus_one Delay value minus one (only used when `present`).
	void SetInitialPresentationDelay(bool present, uint8_t minus_one);

	/// Replace the `configOBUs` payload with the given buffer.
	///
	/// @param config_obus Buffer of zero or more concatenated AV1 OBUs. The pointer is stored as-is; pass `nullptr` to clear.
	void SetConfigObus(const std::shared_ptr<ov::Data> &config_obus);

	/// Derive the effective bit depth from `high_bitdepth` and `twelve_bit` per AV1 spec section 5.5.2.
	///
	/// @return `8`, `10`, or `12`.
	uint8_t BitDepth() const;

private:
	/// Walk `_config_obus`, enforcing AV1 ISOBMFF binding v1.3.0 section 2.3.4 (Semantics) rules: a
	/// valid OBU sequence, at most one Sequence Header OBU, the Sequence Header (if present) as the
	/// first OBU, and its fields matching the fixed `av1C` fields (`seq_profile`, `seq_level_idx_0`,
	/// `seq_tier_0`, `high_bitdepth`, `twelve_bit`, `monochrome`, `chroma_subsampling_x/y`, and
	/// `chroma_sample_position` when non-zero).
	bool ValidateConfigObus();

	uint8_t _marker = 1;
	uint8_t _version = 1;
	uint8_t _seq_profile = 0;
	uint8_t _seq_level_idx_0 = 0;
	uint8_t _seq_tier_0 = 0;
	uint8_t _high_bitdepth = 0;
	uint8_t _twelve_bit = 0;
	uint8_t _monochrome = 0;
	uint8_t _chroma_subsampling_x = 1;
	uint8_t _chroma_subsampling_y = 1;
	uint8_t _chroma_sample_position = 0;
	uint8_t _initial_presentation_delay_present = 0;
	uint8_t _initial_presentation_delay_minus_one = 0;

	// AV1 spec 5.5.2 `color_config()` captured from the `configOBUs` Sequence Header OBU (the av1C
	// header itself carries no CICP). `_color_config_parsed` gates whether these hold real values.
	bool _color_config_parsed = false;
	uint8_t _color_description_present = 0;
	uint8_t _color_primaries = 2;			// CP_UNSPECIFIED
	uint8_t _transfer_characteristics = 2;	// TC_UNSPECIFIED
	uint8_t _matrix_coefficients = 2;		// MC_UNSPECIFIED
	uint8_t _color_range = 0;				// 0 = studio/limited, 1 = full

	std::shared_ptr<ov::Data> _config_obus;

	// Valid by default: a default-constructed record holds the lenient `0x81 0x00 0x00 0x00` fields
	// (marker/version = 1), which is a usable av1C. Parse() clears this on entry and sets it true
	// only on success, so a failed parse is observable as invalid via IsValid().
	bool _valid = true;
};
