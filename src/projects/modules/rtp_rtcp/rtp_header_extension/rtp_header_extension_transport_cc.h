#pragma once

// https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01

#include <base/ovlibrary/ovlibrary.h>
#include "rtp_header_extension.h"

//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |  ID   | L=1   |transport-wide sequence number | zero padding  |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

// a=extmap:3 http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01

class RtpHeaderExtensionTransportCc : public RtpHeaderExtension
{
public:
	RtpHeaderExtensionTransportCc()
		: RtpHeaderExtensionTransportCc(RTP_HEADER_EXTENSION_TRANSPORT_CC_ID)
	{	
	}

	RtpHeaderExtensionTransportCc(uint8_t id)
		: RtpHeaderExtension(id)
	{
		_data = std::make_shared<ov::Data>(_buffer, sizeof(_buffer), true);
		memset(_buffer, 0, sizeof(_buffer));
	}

	void SetSequenceNumber(uint16_t sequence_number)
	{
		_tw_sequence_number = sequence_number;
		ByteWriter<uint16_t>::WriteBigEndian(&_buffer[0], _tw_sequence_number);
		UpdateData();
	}

	uint16_t GetSequenceNumber() const
	{
		return _tw_sequence_number;
	}

	bool SetData(const std::shared_ptr<ov::Data> &data) override
	{
		// Parsing
		if(data->GetLength() != 2)
		{
			return false;
		}

		auto p = data->GetDataAs<uint8_t>();

		_tw_sequence_number = ByteReader<uint16_t>::ReadBigEndian(&p[0]);

		_data = data;

		return true;
	}

protected:
	std::shared_ptr<ov::Data> GetData(HeaderType type) override
	{
		return _data;
	}

private:

	std::shared_ptr<ov::Data>	_data;
	uint8_t _buffer[2];

	uint16_t _tw_sequence_number = 0;
};