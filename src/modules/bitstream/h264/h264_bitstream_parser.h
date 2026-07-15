#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include "h264_common.h"

class H264BitstreamParser
{
public:
	struct Config
	{
        // Can be found by simply parsing the slice header of the VCL.
		bool _parse_slice_type = false;

        // Will be extended to support other configurations
	};

	enum class BitstreamFormat : uint8_t
	{
		AnnexB = 0,
		AVCC
	};

    H264BitstreamParser(const Config &config = {._parse_slice_type = false})
        : _config(config)
    {
    }

    void SetConfig(const Config &config)
    {
        _config = config;
    }

	// Parse NALU 
	bool Parse(const std::shared_ptr<ov::Data> &bitstream, BitstreamFormat format = BitstreamFormat::AnnexB);

    std::optional<H264SliceType> GetLastSliceType() const;

private:
    bool ParseAnnexB(const std::shared_ptr<ov::Data> &bitstream);
    bool ParseAVCC(const std::shared_ptr<ov::Data> &bitstream);

    bool ParseNalu(const uint8_t *nalu_data, size_t nalu_len);
    bool ParseSliceHeader(const uint8_t *nalu_data, size_t nalu_len);

    std::optional<H264SliceType> _last_slice_type = std::nullopt;

    Config _config;
};
