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

#include "av1_types.h"

class Av1Parser
{
public:
	// AV1 spec 4.10.5: a `leb128()` value occupies at most 8 bytes.
	static constexpr size_t LEB128_MAX_SIZE = 8;

	/// Decode a `LEB128`-encoded unsigned integer per AV1 spec section 4.10.5.
	///
	/// Reads up to 8 bytes from `data`. The terminating byte is the first one whose continuation bit
	/// (`0x80`) is clear.
	///
	/// @param data Pointer to the first `LEB128` byte.
	///
	/// @param size Number of bytes available at `data`.
	///
	/// @return `DecodedLeb128` on success, `std::nullopt` if the buffer was exhausted before a terminator.
	static std::optional<DecodedLeb128> DecodeLeb128(const uint8_t *data, size_t size);

	/// Encode an unsigned integer as `LEB128` per AV1 spec section 4.10.5.
	///
	/// Writes 7 bits per byte, little-endian, setting the continuation bit (`0x80`) on every byte
	/// but the last. At most 8 bytes (the AV1 limit).
	///
	/// @param value Value to encode.
	///
	/// @param buffer Output buffer; must hold at least `LEB128_MAX_SIZE` (8) bytes.
	///
	/// @return Number of bytes written (1..8), or 0 if `buffer` is null or `value` does not fit in
	/// AV1's 8-byte leb128 range.
	static size_t EncodeLeb128(uint64_t value, uint8_t *buffer);

	/// Parse `obu_header()` (and `obu_extension_header()` when `extension_flag` is set) per AV1 spec
	/// sections 5.3.2 / 5.3.3.
	///
	/// The size field (when `has_size_field` is true) is NOT consumed here.
	///
	/// @param reader Must contain the OBU header byte (and the extension byte if `extension_flag` is set).
	///
	/// @return Populated `Av1ObuHeader` on success, `std::nullopt` on failure.
	static std::optional<Av1ObuHeader> ParseObuHeader(BitReader &reader);

	/// Convenience overload of `ParseObuHeader` operating on a raw byte pointer.
	///
	/// @param data Pointer to the first OBU header byte.
	///
	/// @param size Number of bytes available at `data`.
	///
	/// @return `ParsedObuHeader` (header + bytes consumed, 1 or 2) on success, `std::nullopt` on failure.
	static std::optional<ParsedObuHeader> ParseObuHeader(const uint8_t *data, size_t size);

	/// Read one OBU at `offset`: header (via `ParseObuHeader`), `obu_size` (via `DecodeLeb128`), and
	/// the payload bounds / next-OBU offset. The single OBU-walk primitive reused by the scanners
	/// below and by `AV1DecoderConfigurationRecord`. An OBU with `obu_has_size_field == 0` takes its
	/// payload as the rest of the buffer (zero for a TemporalDelimiter); callers that require a size
	/// field (e.g. `configOBUs`) check `out.header.has_size_field`.
	///
	/// @return `false` on a malformed header / `obu_size`, otherwise `true` with `out` populated.
	static bool ReadObu(const uint8_t *data, size_t size, size_t offset, Av1ObuSpan &out);

	/// Walk an AV1 OBU bytestream and return the payload of the first `OBU_SEQUENCE_HEADER`.
	///
	/// Accepts both the `configOBUs` payload of an `AV1CodecConfigurationBox` (per AV1 ISOBMFF binding)
	/// and a raw in-band OBU bytestream from a media packet. The returned buffer contains only the bytes
	/// AFTER the OBU header and optional `obu_size` field.
	///
	/// @param config_obus Buffer containing one or more concatenated AV1 OBUs.
	///
	/// @return Payload bytes of the first sequence header OBU, or `nullptr` if absent or buffer is malformed.
	static std::shared_ptr<const ov::Data> ExtractFirstSequenceHeaderObu(const std::shared_ptr<const ov::Data> &config_obus);

	/// Return true if the OBU bytestream's coded frame is a `KEY_FRAME` (AV1 spec `frame_type`).
	///
	/// Uses the in-band sequence header (if present) for `reduced_still_picture_header`, then reads
	/// `frame_type` from the first Frame / FrameHeader OBU. The presence of an `OBU_SEQUENCE_HEADER`
	/// alone is NOT a valid key-frame test.
	static bool IsKeyFrame(const uint8_t *data, size_t size);
	static bool IsKeyFrame(const std::shared_ptr<const ov::Data> &data)
	{
		return (data != nullptr) && IsKeyFrame(data->GetDataAs<uint8_t>(), data->GetLength());
	}

	/// Return true if the OBU bytestream contains an `OBU_SEQUENCE_HEADER`.
	static bool HasSequenceHeaderObu(const uint8_t *data, size_t size);
	static bool HasSequenceHeaderObu(const std::shared_ptr<const ov::Data> &data)
	{
		return (data != nullptr) && HasSequenceHeaderObu(data->GetDataAs<uint8_t>(), data->GetLength());
	}

	/// Return the first `OBU_SEQUENCE_HEADER` as a complete, size-delimited OBU (header + `obu_size` +
	/// payload) - suitable for `configOBUs` or for prepending to a key frame that lacks an in-band
	/// sequence header. Returns `nullptr` if absent, or if the OBU is not size-delimited
	/// (`obu_has_size_field == 0`), since the AV1 ISOBMFF binding requires `obu_has_size_field == 1`.
	static std::shared_ptr<const ov::Data> ExtractFirstSequenceHeaderObuRaw(const std::shared_ptr<const ov::Data> &config_obus);

	/// Parse the leading mandatory fields of a `sequence_header_obu()` payload per AV1 spec section 5.5.1.
	///
	/// Only diagnostic-friendly fields are populated (`seq_profile`, `seq_level_idx_0`,
	/// `max_frame_width`, `max_frame_height`, ...).
	///
	/// @param payload Pointer to the OBU payload (bytes after `obu_header` / `obu_size`).
	///
	/// @param size Number of bytes available at `payload`.
	///
	/// @return `std::nullopt` if the buffer is shorter than the mandatory prefix.
	static std::optional<Av1SequenceHeaderSummary> ParseSequenceHeaderSummary(const uint8_t *payload, size_t size);

	/// Convenience overload of `ParseSequenceHeaderSummary` operating on a raw byte pointer.
	///
	/// @param payload Buffer containing the OBU payload (bytes after `obu_header` / `obu_size`).
	///
	/// @return `std::nullopt` if the buffer is shorter than the mandatory prefix.
	static std::optional<Av1SequenceHeaderSummary> ParseSequenceHeaderSummary(const std::shared_ptr<const ov::Data> &payload)
	{
		if (payload == nullptr)
		{
			return std::nullopt;
		}

		return ParseSequenceHeaderSummary(payload->GetDataAs<uint8_t>(), payload->GetLength());
	}
};
