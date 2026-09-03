//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================

//  Bitstream writers shared by the parser test suites, so that no suite carries its own copy.
//  `ApplyEmulationPrevention()` is a wrapper over `NalUnitInsertor::EmulationPreventionBytes()`,
//  not a second implementation of the escaping rule.

#pragma once

#include <base/ovlibrary/bit_writer.h>
#include <base/ovlibrary/data.h>
#include <modules/bitstream/nalu/nal_unit_insertor.h>

#include <memory>
#include <vector>

namespace ome_test
{
	// ue(v) (ITU-T H.264 9.1, Rec. ITU-T H.265 9.2)
	inline void WriteUE(ov::BitWriter &writer, uint32_t value)
	{
		uint64_t code_num = static_cast<uint64_t>(value) + 1;

		int numbits		  = 0;
		while ((code_num >> (numbits + 1)) != 0)
		{
			numbits++;
		}

		if (numbits > 0)
		{
			writer.WriteBits(numbits, 0);
		}

		writer.WriteBits(numbits + 1, code_num);
	}

	// se(v)
	inline void WriteSE(ov::BitWriter &writer, int32_t value)
	{
		const auto code = (value <= 0)
							  ? static_cast<uint32_t>(-2 * static_cast<int64_t>(value))
							  : static_cast<uint32_t>((2 * static_cast<int64_t>(value)) - 1);

		WriteUE(writer, code);
	}

	// rbsp_trailing_bits(): stop bit '1' then zero-pad to a byte boundary
	inline void WriteTrailing(ov::BitWriter &writer)
	{
		writer.WriteBits(1, 1);

		while ((writer.GetBitCount() % 8) != 0)
		{
			writer.WriteBits(1, 0);
		}
	}

	inline std::vector<uint8_t> ToBytes(ov::BitWriter &writer)
	{
		return std::vector<uint8_t>(writer.GetData(), writer.GetData() + writer.GetDataSize());
	}

	// Turns an RBSP into an EBSP with the same escaper the encoders use
	inline std::vector<uint8_t> ApplyEmulationPrevention(const std::vector<uint8_t> &rbsp)
	{
		auto ebsp = NalUnitInsertor::EmulationPreventionBytes(
			std::make_shared<ov::Data>(rbsp.data(), rbsp.size()));

		const auto *bytes = ebsp->GetDataAs<uint8_t>();

		return std::vector<uint8_t>(bytes, bytes + ebsp->GetLength());
	}
}  // namespace ome_test
