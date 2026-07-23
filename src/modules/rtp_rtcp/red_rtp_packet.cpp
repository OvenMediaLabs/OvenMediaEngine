#include "red_rtp_packet.h"

RedRtpPacket::RedRtpPacket()
{
}

RedRtpPacket::RedRtpPacket(uint8_t red_payload_type, RtpPacket &src)
	: RtpPacket(src)
{
	_block_pt = PayloadType();
	SetPayloadType(red_payload_type);

	_payload_offset = _payload_offset + RED_HEADER_SIZE;
	if(_data->GetLength() < _payload_offset)
	{
		_data->SetLength(_payload_offset);
		_buffer = _data->GetWritableDataAs<uint8_t>();
	}

	_buffer[_payload_offset - RED_HEADER_SIZE] = _block_pt;

	SetPayload(src.Payload(), src.PayloadSize());
}

RedRtpPacket::RedRtpPacket(RedRtpPacket &src)
	: RtpPacket(src)
{
	_block_pt = src._block_pt;
}

RedRtpPacket::~RedRtpPacket()
{

}

//Implement RED as part of the RTP header to reduce memory copying and improve performance.
void RedRtpPacket::PackageAsRed(uint8_t red_payload_type)
{
	// TODO(Getroot): Check validation
	// This function should be used after SetCsrcs, Extensions and before AllocatePayload.

	_block_pt = PayloadType();

	SetPayloadType(red_payload_type);

	// Increase 1 bytes for RED
	_payload_offset = _payload_offset + RED_HEADER_SIZE;
	_data->SetLength(_payload_offset);
	_buffer = _data->GetWritableDataAs<uint8_t>();

	// Write payload type at the end of the rtp header
	_buffer[_payload_offset - RED_HEADER_SIZE] = _block_pt;
}

void RedRtpPacket::PackageAsRtp()
{
	_payload_offset = _payload_offset - RED_HEADER_SIZE;
	_payload_size = _payload_size + RED_HEADER_SIZE;
}

uint8_t RedRtpPacket::BlockPT() const
{
	return _block_pt;
}