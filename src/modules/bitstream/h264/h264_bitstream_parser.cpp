#include "h264_bitstream_parser.h"
#include "h264_parser.h"

#define OV_LOG_TAG "H264BitstreamParser"

bool H264BitstreamParser::Parse(const std::shared_ptr<ov::Data> &bitstream, H264BitstreamParser::BitstreamFormat format)
{
    // initialize variables
    _last_slice_type = std::nullopt;

    switch (format)
    {
    case BitstreamFormat::AnnexB:
        return ParseAnnexB(bitstream);
    case BitstreamFormat::AVCC:
        return ParseAVCC(bitstream);
    }

    return false;
}

bool H264BitstreamParser::ParseAnnexB(const std::shared_ptr<ov::Data> &bitstream)
{
    auto data = bitstream->GetDataAs<uint8_t>();
    auto data_len = bitstream->GetLength();
    auto nalu_indexes = H264Parser::FindNaluIndexes(data, data_len);
    
    for (const auto& index : nalu_indexes)
    {
        auto nalu_data = data + index._payload_offset;
        auto nalu_len = index._payload_size;

        if (ParseNalu(nalu_data, nalu_len) == false)
        {
            return false;
        }
    }

    return true;
}

bool H264BitstreamParser::ParseAVCC(const std::shared_ptr<ov::Data> &bitstream)
{
    return false;
}

bool H264BitstreamParser::ParseNalu(const uint8_t *nalu_data, size_t nalu_len)
{
    H264NalUnitHeader nalu_header;
    if (H264Parser::ParseNalUnitHeader(nalu_data, nalu_len, nalu_header) == false)
    {
        return false;
    }

    // The 264 slice type can be found by simply parsing the slice header of the VCL.
    if (_config._parse_slice_type)
    {
        if (nalu_header.IsVideoSlice() == true)
        {
            return ParseSliceHeader(nalu_data, nalu_len);
        }
    }

    return true;
}

bool H264BitstreamParser::ParseSliceHeader(const uint8_t *nalu_data, size_t nalu_len)
{
    if (_config._parse_slice_type == false)
    {
        // No need to parse slice header
        return true;
    }

    NalUnitBitstreamParser parser(nalu_data, nalu_len);

	H264NalUnitHeader nal_header;
	if (H264Parser::ParseNalUnitHeader(parser, nal_header) == false)
	{
		return false;
	}

	if (nal_header.IsVideoSlice() == false)
	{
		return false;
	}

    uint32_t first_mb_in_slice;
	if (!parser.ReadUEV(first_mb_in_slice))
	{
		return false;
	}

    uint32_t slice_type;
	if (!parser.ReadUEV(slice_type))
	{
		return false;
	}
	if (slice_type > 9)
	{
		return false;
	}

    _last_slice_type = static_cast<H264SliceType>(slice_type % 5);

    return true;
}

std::optional<H264SliceType> H264BitstreamParser::GetLastSliceType() const
{
    return _last_slice_type;
}