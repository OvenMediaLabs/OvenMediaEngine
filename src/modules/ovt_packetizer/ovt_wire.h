//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/mediarouter/media_buffer.h>
#include <base/mediarouter/media_type.h>
#include <base/ovlibrary/byte_io.h>

#include <cstdint>
#include <optional>

#include "ovt_packet.h"

// OVT protocol values for the enums that cross the wire:
// the describe integers (`codecId`, `mediaType`, `sampleFormat`),
// and the media packet header bytes MH[28] .. MH[31].
// The table in `ovt_wire.cpp` is the protocol; the C++ enum order is irrelevant to it.
// Mapping one side only does not free the enums:
// every send site goes through `ToOvtWire()` and every receive site through `FromOvtWire()`.
namespace ovt
{
	// Send side. Every enumerator has a wire value, so nothing here is optional.
	uint8_t ToOvtWire(cmn::MediaType media_type);
	uint8_t ToOvtWire(MediaPacketFlag flag);
	uint8_t ToOvtWire(cmn::BitstreamFormat format);
	uint8_t ToOvtWire(cmn::PacketType packet_type);
	uint8_t ToOvtWire(cmn::MediaCodecId codec_id);
	uint8_t ToOvtWire(cmn::AudioSample::Format format);

	// Receive side. A byte without a table entry yields `nullopt`;
	// the caller leaves the enum `Unknown` and preserves the byte instead of casting it into the enum.
	template <typename T>
	std::optional<T> FromOvtWire(uint8_t wire);

	template <>
	std::optional<cmn::MediaType> FromOvtWire<cmn::MediaType>(uint8_t wire);
	// `MH[29]` is a bitmask: a byte with a known bit keeps that meaning and its unknown bits are
	// dropped; only a non-zero byte with no known bit stays unmapped
	template <>
	std::optional<MediaPacketFlag> FromOvtWire<MediaPacketFlag>(uint8_t wire);
	template <>
	std::optional<cmn::BitstreamFormat> FromOvtWire<cmn::BitstreamFormat>(uint8_t wire);
	template <>
	std::optional<cmn::PacketType> FromOvtWire<cmn::PacketType>(uint8_t wire);
	template <>
	std::optional<cmn::MediaCodecId> FromOvtWire<cmn::MediaCodecId>(uint8_t wire);
	template <>
	std::optional<cmn::AudioSample::Format> FromOvtWire<cmn::AudioSample::Format>(uint8_t wire);
	// `OvtPayloadType` enumerators are the wire values themselves; the receive side still goes through
	// this so an unknown payload type is dropped instead of cast into the enum.
	template <>
	std::optional<OvtPayloadType> FromOvtWire<OvtPayloadType>(uint8_t wire);

	// One enum byte of the media packet header after the table has read it.
	// `value` is `Unknown` when the table has no entry for the byte,
	// and `unmapped` then holds the byte itself so a relay re-sends it unchanged.
	template <typename T>
	struct WireEnum
	{
		T value;
		std::optional<uint8_t> unmapped;

		// Reads one header byte at `at` and resolves it through the table.
		// The byte is never cast into the enum: only a table entry changes the value.
		static WireEnum Read(const uint8_t *at)
		{
			auto wire	= ByteReader<uint8_t>::ReadBigEndian(at);
			auto mapped = FromOvtWire<T>(wire);

			return {mapped.value_or(T::Unknown),
					mapped.has_value() ? std::nullopt : std::optional<uint8_t>(wire)};
		}
	};

	// Whether a wire byte means this enumerator: true on a match, not a three-way comparison.
	// `T` is deduced from `value`, and a byte without a table entry matches nothing.
	template <typename T>
	bool CompareWireValue(uint8_t wire, T value)
	{
		return FromOvtWire<T>(wire) == value;
	}

	// Whether an OVT1 peer (up to v0.21.0.0) has a wire value for this enumerator; false marks one added
	// after that release. A codec without one is removed from what an origin sends an OVT1 edge;
	// a bitstream format or packet type without one is announced to OVT2 edges as a required token (PT 40).
	bool HasLegacyWireValue(cmn::MediaCodecId codec_id);
	bool HasLegacyWireValue(cmn::BitstreamFormat format);
	bool HasLegacyWireValue(cmn::PacketType packet_type);

	// Transport bitstream format of a codec on the OVT media path.
	// The OVT publisher only sees normalized streams, so the format is a function of the codec,
	// and it is fixed for good once the codec is introduced.
	// `nullopt`: the codec has no transport format (VP9/FLV/WHISPER),
	// or is not a codec (`None`, which data tracks carry).
	std::optional<cmn::BitstreamFormat> TransportBitstreamFormat(cmn::MediaCodecId codec_id);

	// Describe carries the wire value as a JSON integer.
	// OVT1 wrote it through `int8_t` (`codecId` through `uint8_t` since v0.21.0.0),
	// so -128 .. -1 are read as the same bytes as 128 .. 255;
	// anything outside both ranges has no table entry.
	template <typename T>
	std::optional<T> FromOvtWireInt(int64_t value)
	{
		if ((value < INT8_MIN) || (value > UINT8_MAX))
		{
			return std::nullopt;
		}

		return FromOvtWire<T>(static_cast<uint8_t>(value));
	}
}  // namespace ovt
