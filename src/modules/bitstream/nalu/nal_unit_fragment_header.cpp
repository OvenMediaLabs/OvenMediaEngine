#include "nal_unit_fragment_header.h"


NalUnitFragmentHeader::NalUnitFragmentHeader()
{

}

NalUnitFragmentHeader::~NalUnitFragmentHeader()
{

}

bool NalUnitFragmentHeader::Parse(const std::shared_ptr<const ov::Data> &data, NalUnitFragmentHeader &fragment_hdr)
{
	return NalUnitFragmentHeader::Parse( data->GetDataAs<uint8_t>(), data->GetLength(), fragment_hdr);
}

bool NalUnitFragmentHeader::Parse(const uint8_t *bitstream, size_t length, NalUnitFragmentHeader &fragment_hdr)
{
	size_t data_offset = 0;

	std::vector<std::pair<size_t, size_t>> offset_list;

	while ( data_offset < length )
	{
		size_t remain_data_size = length - data_offset;
		const uint8_t* data = bitstream + data_offset;

		if (remain_data_size >= 3 &&
				0x00 == data[0] &&
				0x00 == data[1] &&
				0x01 == data[2])
		{
			offset_list.emplace_back(data_offset, 3); // Offset, SIZEOF(START_CODE[3])
			data_offset += 3;
		}
		else if (remain_data_size >= 4 &&
				 0x00 == data[0] &&
				 0x00 == data[1] &&
				 0x00 == data[2] &&
				 0x01 == data[3])
		{
			offset_list.emplace_back(data_offset, 4); // Offset, SIZEOF(START_CODE[4])
			data_offset += 4;
		}
		else
		{
			data_offset += 1;
		}
	}

	fragment_hdr._fragment_header.Clear();

	for (size_t index = 0; index < offset_list.size(); ++index)
	{
		size_t nalu_offset = 0;
		size_t nalu_data_len = 0;

		if (index != offset_list.size() - 1)
		{
			nalu_offset = offset_list[index].first + offset_list[index].second;
			nalu_data_len = offset_list[index + 1].first - nalu_offset;
		}
		else
		{
			nalu_offset = offset_list[index].first + offset_list[index].second;
			nalu_data_len = length - nalu_offset;
		}

		fragment_hdr._fragment_header.fragmentation_offset.emplace_back(nalu_offset);
		fragment_hdr._fragment_header.fragmentation_length.emplace_back(nalu_data_len);
	}
	return true;
}


