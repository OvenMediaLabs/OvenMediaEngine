//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2024 OvenMedia Labs. All rights reserved.
//
//==============================================================================

#include "nal_unit_insertor.h"

#include <base/ovlibrary/byte_io.h>
#include <modules/bitstream/nalu/nal_unit_fragment_header.h>

#define OV_LOG_TAG "NalUnitInsertor"

const uint8_t START_CODE_3B[3] = {0x00, 0x00, 0x01};
const uint8_t START_CODE_4B[4] = {0x00, 0x00, 0x00, 0x01};

namespace
{
	bool IsAnnexB(const cmn::BitstreamFormat format)
	{
		return (format == cmn::BitstreamFormat::H264_ANNEXB) ||
			   (format == cmn::BitstreamFormat::H265_ANNEXB);
	}

	// The source's own start code length, read from the bytes in front of its first NAL unit.
	// 00 00 00 01 is a 4 byte start code, 00 00 01 a 3 byte one.
	int32_t DetectStartCodeLength(const std::shared_ptr<const ov::Data> &data, const size_t first_nal_offset)
	{
		if ((first_nal_offset >= 4) && (data->GetDataAs<uint8_t>()[first_nal_offset - 4] == 0x00))
		{
			return 4;
		}

		return (first_nal_offset >= 3) ? 3 : 4;
	}

	// Writes the start code (Annex B) or the 4 byte NAL length (length prefixed formats)
	void AppendNalPrefix(const std::shared_ptr<ov::Data> &data,
						 const cmn::BitstreamFormat format,
						 const int32_t start_code_length,
						 const size_t nal_length)
	{
		if (IsAnnexB(format))
		{
			data->Append(start_code_length == 3 ? START_CODE_3B : START_CODE_4B, start_code_length);
			return;
		}

		// ISO 14496-15: the NAL unit length is a big endian unsigned integer
		uint8_t length_field[sizeof(uint32_t)] = {0};
		ByteWriter<uint32_t>::WriteBigEndian(length_field, static_cast<uint32_t>(nal_length));
		data->Append(length_field, sizeof(length_field));
	}
}  // namespace

std::optional<std::tuple<std::shared_ptr<ov::Data>, std::shared_ptr<FragmentationHeader>>> NalUnitInsertor::Insert(
	const std::shared_ptr<const ov::Data> src_data,
	const std::shared_ptr<ov::Data> new_nal,
	const cmn::BitstreamFormat format,
	const size_t nal_index)
{
	if (src_data == nullptr)
	{
		logte("Invalid argument");
		return std::nullopt;
	}

	NalUnitFragmentHeader fragment_header;
	if (NalUnitFragmentHeader::Parse(src_data, fragment_header) == false)
	{
		logte("Failed to parse NALU fragment header");
		return std::nullopt;
	}

	const FragmentationHeader* src_fragment = fragment_header.GetFragmentHeader();
	if (src_fragment == nullptr)
	{
		logte("Failed to get NALU fragment header");
		return std::nullopt;
	}

	return Insert(src_data, src_fragment, new_nal, format, nal_index);
}

std::optional<std::tuple<std::shared_ptr<ov::Data>, std::shared_ptr<FragmentationHeader>>> NalUnitInsertor::Insert(
	const std::shared_ptr<const ov::Data> src_data,
	const FragmentationHeader* src_fragment,
	const std::shared_ptr<ov::Data> new_nal,
	const cmn::BitstreamFormat format,
	const size_t nal_index)
{
	return Rebuild(src_data, src_fragment, new_nal, format, nal_index, Operation::Insert);
}

std::optional<std::tuple<std::shared_ptr<ov::Data>, std::shared_ptr<FragmentationHeader>>> NalUnitInsertor::Replace(
	const std::shared_ptr<const ov::Data> src_data,
	const FragmentationHeader* src_fragment,
	const std::shared_ptr<ov::Data> new_nal,
	const cmn::BitstreamFormat format,
	const size_t nal_index)
{
	if (new_nal == nullptr)
	{
		logte("Replace needs a new NAL unit");
		return std::nullopt;
	}

	if (nal_index == APPEND_TO_END)
	{
		logte("Replace needs a concrete NAL index");
		return std::nullopt;
	}

	return Rebuild(src_data, src_fragment, new_nal, format, nal_index, Operation::Replace);
}

std::optional<std::tuple<std::shared_ptr<ov::Data>, std::shared_ptr<FragmentationHeader>>> NalUnitInsertor::Rebuild(
	const std::shared_ptr<const ov::Data> src_data,
	const FragmentationHeader* src_fragment,
	const std::shared_ptr<ov::Data> new_nal,
	const cmn::BitstreamFormat format,
	const size_t nal_index,
	const Operation operation)
{
	if (src_data == nullptr || src_fragment == nullptr)
	{
		logte("Invalid argument");
		return std::nullopt;
	}

	if (!(format == cmn::BitstreamFormat::H264_ANNEXB ||
		  format == cmn::BitstreamFormat::H264_AVCC ||
		  format == cmn::BitstreamFormat::H265_ANNEXB))
	{
		logte("Not supported bitstream format: %s", GetBitstreamFormatString(format));
		return std::nullopt;
	}

	// Nothing was parsed, so there is no access unit to rebuild. Writing the new NAL on its own
	// here would replace the coded picture with just that NAL.
	if (src_fragment->GetCount() == 0)
	{
		logte("Could not rebuild an access unit with no NAL unit");
		return std::nullopt;
	}

	if ((operation == Operation::Replace) && (nal_index >= src_fragment->GetCount()))
	{
		logte("Could not replace NAL unit at %zu, the access unit has %zu NAL units",
			  nal_index, src_fragment->GetCount());
		return std::nullopt;
	}

	auto new_fragment = std::make_shared<FragmentationHeader>();
	auto new_data	  = std::make_shared<ov::Data>();

	// Escaped once here so the final length is known before writing
	std::shared_ptr<ov::Data> converted_nal = nullptr;
	if (new_nal != nullptr)
	{
		converted_nal = EmulationPreventionBytes(new_nal);
		if (converted_nal == nullptr)
		{
			logte("Failed to apply emulation prevention bytes");
			return std::nullopt;
		}
	}

	const auto first_fragment = src_fragment->GetFragment(0);
	const int32_t start_code_length =
		first_fragment.has_value() ? DetectStartCodeLength(src_data, std::get<0>(first_fragment.value())) : 4;

	auto append_new_nal = [&]() {
		if (converted_nal == nullptr)
		{
			return;
		}

		AppendNalPrefix(new_data, format, start_code_length, converted_nal->GetLength());

		new_fragment->AddFragment(new_data->GetLength(), converted_nal->GetLength());
		new_data->Append(converted_nal);
	};

	for (size_t i = 0; i < src_fragment->GetCount(); i++)
	{
		auto fragment = src_fragment->GetFragment(i);
		if (fragment.has_value() == false)
		{
			continue;
		}
		size_t src_offset = std::get<0>(fragment.value());
		size_t nal_length = std::get<1>(fragment.value());

		if (i == nal_index)
		{
			append_new_nal();

			if (operation == Operation::Replace)
			{
				// The source NAL unit is dropped
				continue;
			}
		}

		AppendNalPrefix(new_data, format, start_code_length, nal_length);

		size_t nal_offset = new_data->GetLength();

		new_fragment->AddFragment(nal_offset, nal_length);

		new_data->Append(src_data->GetDataAs<char>() + src_offset, nal_length);
	}

	// APPEND_TO_END, or an index past the last NAL unit
	if ((operation == Operation::Insert) && (nal_index >= src_fragment->GetCount()))
	{
		append_new_nal();
	}

	return std::make_tuple(new_data, new_fragment);
}

std::shared_ptr<ov::Data> NalUnitInsertor::EmulationPreventionBytes(const std::shared_ptr<ov::Data> &nal)
{
	if (nal == nullptr)
	{
		return nullptr;
	}

	ov::ByteStream stream(nal);
	ov::ByteStream new_nal(nal->GetLength() + (nal->GetLength() / 2));

	int8_t zero_count = 0;

	while (stream.Remained() > 0)
	{
		uint8_t byte = stream.Read8();

		// ITU-T H.264 7.4.1.1: 0x00 0x00 followed by 0x00~0x03 gets an
		// emulation_prevention_three_byte. A literal 0x03 needs one too, or the decoder strips it.
		if ((zero_count == 2) && (byte <= 0x03))
		{
			new_nal.Write8(0x03);
			zero_count = 0;
		}

		if (byte == 0x00)
		{
			zero_count++;
		}
		else
		{
			zero_count = 0;
		}

		new_nal.Write8(byte);
	}

	return new_nal.GetDataPointer();
}

std::shared_ptr<ov::Data> NalUnitInsertor::RemoveEmulationPreventionBytes(const std::shared_ptr<const ov::Data> &nal)
{
	if (nal == nullptr)
	{
		return nullptr;
	}

	const auto *buffer	= nal->GetDataAs<uint8_t>();
	const size_t length = nal->GetLength();

	auto rbsp = std::make_shared<ov::Data>(length);

	size_t zero_count = 0;
	for (size_t i = 0; i < length; i++)
	{
		const uint8_t byte = buffer[i];

		// After two zero bytes a 0x03 is always an emulation_prevention_three_byte, so dropping it
		// unconditionally is the exact inverse of EmulationPreventionBytes()
		if ((zero_count == 2) && (byte == 0x03))
		{
			zero_count = 0;
			continue;
		}

		zero_count = (byte == 0x00) ? (zero_count + 1) : 0;

		rbsp->Append(&byte, 1);
	}

	return rbsp;
}
