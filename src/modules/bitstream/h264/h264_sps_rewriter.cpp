//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2026 OvenMedia Labs. All rights reserved.
//
//==============================================================================

#include "h264_sps_rewriter.h"

#include <modules/bitstream/nalu/nal_unit_insertor.h>

#include "h264_parser.h"

#define OV_LOG_TAG "H264SpsRewriter"

std::shared_ptr<ov::Data> H264SpsRewriter::EnablePicStructPresent(const std::shared_ptr<const ov::Data> &sps_nal)
{
	if ((sps_nal == nullptr) || (sps_nal->GetLength() < 2))
	{
		return nullptr;
	}

	// Parsed as it stands: NalUnitBitstreamParser skips emulation prevention bytes itself, so the
	// bit position it reports indexes this buffer. Unescaping first would make it strip a second
	// round of 0x03 bytes, and an RBSP may legitimately contain 00 00 03.
	H264SPS sps;
	if (H264Parser::ParseSPS(sps_nal->GetDataAs<uint8_t>(), sps_nal->GetLength(), sps) == false)
	{
		// Runs per access unit; DecideSeiPlacement reports the condition once per track
		logtd("Could not parse the SPS");
		return nullptr;
	}

	if (sps.IsPicStructPresent() == true)
	{
		// Already set
		return nullptr;
	}

	const size_t bit_pos = sps.GetPicStructPresentFlagBitPos();
	if (bit_pos == 0)
	{
		// No VUI, so the flag is not coded. Inserting one would change the length.
		logtd("Could not enable pic_struct_present_flag: the SPS has no VUI");
		return nullptr;
	}

	const size_t byte_index = bit_pos / 8;
	if (byte_index >= sps_nal->GetLength())
	{
		logtd("pic_struct_present_flag position %zu is outside the SPS (%zu bytes)",
			  bit_pos, sps_nal->GetLength());
		return nullptr;
	}

	auto patched = sps_nal->Clone();
	auto *buffer = patched->GetWritableDataAs<uint8_t>();

	// Bits run from the most significant bit of each byte. Setting one can only make the byte
	// less zero, so it cannot introduce a sequence that needs escaping.
	buffer[byte_index] |= static_cast<uint8_t>(0x80 >> (bit_pos % 8));

	// Confirm the recorded bit position was right
	H264SPS patched_sps;
	if ((H264Parser::ParseSPS(patched->GetDataAs<uint8_t>(), patched->GetLength(), patched_sps) == false) ||
		(patched_sps.IsPicStructPresent() == false))
	{
		logte("The patched SPS does not parse back with pic_struct_present_flag set");
		return nullptr;
	}

	// An RBSP, not a NAL: NalUnitInsertor escapes what it is given
	return NalUnitInsertor::RemoveEmulationPreventionBytes(patched);
}
