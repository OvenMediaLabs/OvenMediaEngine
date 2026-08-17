#include "nal_unit_insertor.h"

#include <modules/bitstream/h264/h264_parser.h>
#include <modules/bitstream/nalu/nal_unit_fragment_header.h>

#define OV_LOG_TAG "NalUnitInsertor"

const uint8_t START_CODE_3B[3] = {0x00, 0x00, 0x01};
const uint8_t START_CODE_4B[4] = {0x00, 0x00, 0x00, 0x01};

std::optional<std::tuple<std::shared_ptr<ov::Data>, std::shared_ptr<FragmentationHeader>>> NalUnitInsertor::Insert(
	const std::shared_ptr<const ov::Data> src_data,
	const std::shared_ptr<ov::Data> new_nal,
	const cmn::BitstreamFormat format)
{
	if (src_data == nullptr)
	{
		logte("Invalid argument");
		return std::nullopt;
	}

	NalUnitFragmentHeader fragment_header;	
	if(NalUnitFragmentHeader::Parse(src_data, fragment_header) == false)
	{
		logte("Failed to parse NALU fragment header");
		return std::nullopt;
	}

	const FragmentationHeader* src_fragment = fragment_header.GetFragmentHeader();
	if(src_fragment == nullptr)
	{
		logte("Failed to get NALU fragment header");
		return std::nullopt;
	}

	return Insert(src_data, src_fragment, new_nal, format);	
}

std::optional<std::tuple<std::shared_ptr<ov::Data>, std::shared_ptr<FragmentationHeader>>> NalUnitInsertor::Insert(
	const std::shared_ptr<const ov::Data> src_data,
	const FragmentationHeader* src_fragment,
	const std::shared_ptr<ov::Data> new_nal,
	const cmn::BitstreamFormat format)
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

	std::shared_ptr<FragmentationHeader> new_fragment = std::make_shared<FragmentationHeader>();
	if (new_fragment == nullptr)
	{
		logte("Failed to create new fragment");
		return std::nullopt;
	}

	std::shared_ptr<ov::Data> new_data = std::make_shared<ov::Data>();
	if (new_data == nullptr)
	{
		logte("Failed to create new data");
		return std::nullopt;
	}

	// Assume that the start code length from the first fragment
	int32_t start_code_length = (src_fragment->GetFragment(0).has_value())?std::get<0>(src_fragment->GetFragment(0).value()) : 4;

	for (size_t i = 0; i < src_fragment->GetCount(); i++)
	{
		// Get Fragment
		auto fragment = src_fragment->GetFragment(i);
		if(!fragment.has_value())
		{
			continue;
		}
		size_t src_offset = std::get<0>(fragment.value());
		size_t nal_length = std::get<1>(fragment.value());

		if (format == cmn::BitstreamFormat::H264_ANNEXB || format == cmn::BitstreamFormat::H265_ANNEXB)
		{
			new_data->Append(start_code_length == 3 ? START_CODE_3B : START_CODE_4B, start_code_length);  // Start Code
		}
		else if (format == cmn::BitstreamFormat::H264_AVCC)
		{
			new_data->Append(&nal_length, sizeof(uint32_t));  // NALU length  (4byte)
		}

		// Append Fragment
		size_t nal_offset = new_data->GetLength();

		new_fragment->AddFragment(nal_offset, nal_length);

		// NALU
		new_data->Append(src_data->GetDataAs<char>() + src_offset, nal_length);
	}

	if (new_nal != nullptr)
	{
		// new_nal to Emulation Prevention Bytes(0x03)
		auto converted_nal = EmulationPreventionBytes(new_nal);

		size_t nal_length = converted_nal->GetLength();

		if (format == cmn::BitstreamFormat::H264_ANNEXB || format == cmn::BitstreamFormat::H265_ANNEXB)
		{
			new_data->Append(start_code_length == 3 ? START_CODE_3B : START_CODE_4B, start_code_length);  // Start Code
		}
		else if (format == cmn::BitstreamFormat::H264_AVCC)
		{
			new_data->Append(&nal_length, sizeof(uint32_t));  //  NALU length  (4byte)
		}

		// Append Fragment
		size_t nal_offset = new_data->GetLength();

		new_fragment->AddFragment(nal_offset, nal_length);

		// NALU
		new_data->Append(converted_nal);
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
	
	int8_t zeroCount  = 0;

	while (stream.Remained() > 0)
	{
		uint8_t byte = stream.Read8();

		if (zeroCount == 2 && byte == 0x03)
		{
			zeroCount = 0;
		}

		if (zeroCount == 2 && byte <= 0x03)
		{
			new_nal.Write8(0x03);
			zeroCount = 0;
		}

		if (byte == 0x00)
		{
			zeroCount++;
		}
		else
		{
			zeroCount = 0;
		}

		new_nal.Write8(byte);
	}


	return new_nal.GetDataPointer();;
}
