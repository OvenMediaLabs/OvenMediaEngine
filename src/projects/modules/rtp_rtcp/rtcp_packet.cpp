#include "rtcp_packet.h"

#include "rtcp_info/rtcp_private.h"
#include <base/ovlibrary/byte_io.h>
#include <sys/time.h>

bool RtcpPacket::Build(const std::shared_ptr<RtcpInfo> &info)
{
	auto info_data = info->GetData();
	if(info_data == nullptr)
	{
		return false;
	}

	_info = info;

	SetReportCount(info->GetCountOrFmt());
	SetType(info->GetPacketType());

	_payload_size = info_data->GetLength();
	_data = std::make_shared<ov::Data>(RTCP_DEFAULT_MAX_PACKET_SIZE);
	_data->SetLength(RTCP_HEADER_SIZE + _payload_size);
	uint8_t* buffer = _data->GetWritableDataAs<uint8_t>();

	// Build header

	// Version:2 | padding:0 | count_or_fmt
	uint8_t version = 2 << 6;
	uint8_t padding = info->HasPadding() ? 1 << 5 : 0; 
	
	buffer[0] = version | padding | info->GetCountOrFmt();
	buffer[1] = static_cast<uint8_t>(info->GetPacketType());

	// Payload length must be divided zero
	if(_payload_size % 4 != 0)
	{
		logte("Payload size of RTCP packet must be divided by 4");
		return false;
	}

	_length = _payload_size/4;
	ByteWriter<uint16_t>::WriteBigEndian(&buffer[2], _length);
	
	_payload = buffer + RTCP_HEADER_SIZE;
	
	memcpy(_payload, info_data->GetDataAs<uint8_t>(), info_data->GetLength());

	return true;
}

bool RtcpPacket::Parse(const uint8_t* buffer, const size_t buffer_size, size_t &block_size)
{
	if(buffer_size < RTCP_HEADER_SIZE)
	{
		logtt("RTCP block size is too small(%d bytes), block size must be bigger than 4 bytes", RTCP_HEADER_SIZE);
		return false;
	}

	_version = buffer[0] >> 6;
	if(_version != RTCP_VERSION)
	{
		logtt("Invalid RTCP header : version must be 2");
		return false;
	}

	_has_padding = (buffer[0] & 0x20) != 0;
	_count_or_format = buffer[0] & 0x1F;
	_type = static_cast<RtcpPacketType>(buffer[1]);

	// RFC3550
	// The length of this RTCP packet in 32-bit words minus one,      
	// including the header and any padding.  (The offset of one makes
	// zero a valid length and avoids a possible infinite loop in      
	// scanning a compound RTCP packet, while counting 32-bit words      
	// avoids a validity check for a multiple of 4.)

	// (length + 1) * 4 ==> RTCP block size	
	// == 4*length + 4*1 ==> RTCP block size
	// RTCP Header size = 4
	// RTCP block size - RTCP Header size(4) = payload size
	block_size = (ByteReader<uint16_t>::ReadBigEndian(&buffer[2]) + 1) * 4;
	if(buffer_size < block_size)
	{
		logtt("The rtcp block size is too small : block size(%d) must be bigger than (headers size + payload size : %d)", 
				buffer_size, RTCP_HEADER_SIZE + _payload_size);
		return false;
	}

	_payload_size = block_size - RTCP_HEADER_SIZE;
	if(_has_padding)
	{
		if(block_size == 0)
		{
			logtt("Invalid RTCP header : the header has padding but length is 0");
			return false;
		}

		_padding_size = buffer[block_size - 1];
		if(_padding_size == 0)
		{	
			logtt("Invalid RTCP header : the header has padding but padding size is 0");
			return false;
		}

		if(_padding_size > _payload_size)
		{
			logtt("Invalid RTCP header : too many padding bytes(%d), it must be less than payload size (%d)", 
				_padding_size, _payload_size);
			return false;
		}

		_payload_size -= _padding_size;
	}

	_data = std::make_shared<ov::Data>(buffer, RTCP_HEADER_SIZE + _payload_size);
	_payload = _data->GetWritableDataAs<uint8_t>() + RTCP_HEADER_SIZE;

	return true;
}