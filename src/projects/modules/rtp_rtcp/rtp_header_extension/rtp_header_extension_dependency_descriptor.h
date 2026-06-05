#pragma once

// https://aomediacodec.github.io/av1-rtp-spec/#dependency-descriptor-rtp-header-extension

#include <base/ovlibrary/ovlibrary.h>
#include "rtp_header_extension.h"

//  0                   1                   2
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |S|E| template_id |          frame_number         |  mandatory_descriptor_fields
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// | extended_descriptor_fields (variable, optional)               |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
// S = start_of_frame, E = end_of_frame. Only the mandatory fields are parsed:
// the extended fields encode the dependency template structure (scalability,
// resolution, decode targets) which OvenMediaEngine does not need for frame
// boundary detection. The id is negotiated dynamically, so it is supplied at
// construction rather than via a fixed OME extension id.
class RtpHeaderExtensionDependencyDescriptor : public RtpHeaderExtension
{
public:
	RtpHeaderExtensionDependencyDescriptor(uint8_t id)
		: RtpHeaderExtension(id)
	{
	}

	bool IsStartOfFrame() const { return _start_of_frame; }
	bool IsEndOfFrame() const { return _end_of_frame; }
	uint8_t GetTemplateId() const { return _template_id; }
	uint16_t GetFrameNumber() const { return _frame_number; }

	bool SetData(const std::shared_ptr<ov::Data> &data) override
	{
		// At least the first mandatory byte (S/E/template_id) is required.
		// frame_number occupies the next two bytes when the descriptor is the
		// full mandatory form.
		if (data == nullptr || data->GetLength() < 1)
		{
			return false;
		}

		auto p = data->GetDataAs<uint8_t>();
		_start_of_frame = (p[0] & 0x80) != 0;
		_end_of_frame = (p[0] & 0x40) != 0;
		_template_id = p[0] & 0x3F;
		if (data->GetLength() >= 3)
		{
			_frame_number = ByteReader<uint16_t>::ReadBigEndian(&p[1]);
		}

		_data = data;

		return true;
	}

protected:
	std::shared_ptr<ov::Data> GetData(HeaderType type) override
	{
		return _data;
	}

private:
	std::shared_ptr<ov::Data> _data;

	bool _start_of_frame = false;
	bool _end_of_frame = false;
	uint8_t _template_id = 0;
	uint16_t _frame_number = 0;
};
