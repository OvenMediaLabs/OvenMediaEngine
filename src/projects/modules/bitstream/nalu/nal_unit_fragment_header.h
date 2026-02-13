#pragma once

#include <base/ovlibrary/ovlibrary.h>

#include "base/mediarouter/media_buffer.h"
#include "base/mediarouter/media_type.h"

class NalUnitFragmentHeader
{
public:
	NalUnitFragmentHeader();
	~NalUnitFragmentHeader();


	static bool Parse(const std::shared_ptr<const ov::Data> &data, NalUnitFragmentHeader &fragment_hdr);
	static bool Parse(const uint8_t *bitstream, size_t length, NalUnitFragmentHeader &fragment_hdr);

	const FragmentationHeader *GetFragmentHeader() const
	{
		return &_fragment_header;
	}

	FragmentationHeader _fragment_header;
};
