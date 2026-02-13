//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "mpegts_splice_info.h"
#include <base/ovlibrary/bit_reader.h>
#include <base/ovlibrary/bit_writer.h>

namespace mpegts
{
	// Protected
	SpliceInfo::SpliceInfo(SpliceCommandType command_type)
	{
		_splice_command_type = static_cast<uint8_t>(command_type);
	}

	std::shared_ptr<ov::Data> SpliceInfo::Build()
	{
		auto splice_command = BuildSpliceCommand();
		if (splice_command == nullptr)
		{
			return nullptr;
		}

		_splice_command_length = splice_command->GetLength();

		ov::BitWriter stream(188);

		// SpliceInfo data
		stream.WriteBytes<uint8_t>(_protocol_version);
		stream.WriteBits(1, _encrypted_packet);
		stream.WriteBits(6, _encryption_algorithm);
		stream.WriteBits(33, _pts_adjustment);
		stream.WriteBytes<uint8_t>(_cw_index);
		stream.WriteBits(12, _tier);

		// Recalculate splice_command_length
		_splice_command_length = splice_command->GetLength();
		stream.WriteBits(12, _splice_command_length);
		stream.WriteBytes<uint8_t>(_splice_command_type);
		stream.WriteData(splice_command->GetDataAs<uint8_t>(), splice_command->GetLength());

		// descriptor loop length
		// Not supported
		_descriptor_loop_length = 0;
		stream.WriteBytes<uint16_t>(_descriptor_loop_length);

		return stream.GetDataObject();
	}
} // namespace mpegts