#include "rtx_rtp_packet.h"
#include <base/ovlibrary/byte_io.h>

RtxRtpPacket::RtxRtpPacket(uint32_t rtx_ssrc, uint8_t rtx_payload_type, const RtpPacket &src)
	: RtpPacket(src)
{
	PackageAsRtx(rtx_ssrc, rtx_payload_type, src);
}

RtxRtpPacket::RtxRtpPacket(const RtxRtpPacket &src)
	: RtpPacket(src)
{
	_origin_payload_type = src._origin_payload_type;
	_origin_seq_no = src._origin_seq_no;
}

bool RtxRtpPacket::PackageAsRtx(uint32_t rtx_ssrc, uint8_t rtx_payload_type, const RtpPacket &src)
{
	// replace with rtx payload type
	_origin_payload_type = PayloadType();
	SetPayloadType(rtx_payload_type);
	SetSsrc(rtx_ssrc);
	SetTimestamp(src.Timestamp());

	// Put OSN
	_origin_seq_no = src.SequenceNumber();

	_payload_offset = _payload_offset + RTX_HEADER_SIZE;

	if (_data->GetLength() < _payload_offset)
	{
		_data->SetLength(_payload_offset);
		_buffer = _data->GetWritableDataAs<uint8_t>();
	}

	SetOriginalSequenceNumber(_origin_seq_no);
	
	// Rewrite payload
	SetPayload(src.Payload(), src.PayloadSize());

	return true;
}

void RtxRtpPacket::SetOriginalSequenceNumber(uint16_t seq_no)
{
	ByteWriter<uint16_t>::WriteBigEndian(&_buffer[_payload_offset - RTX_HEADER_SIZE], seq_no);
}

std::shared_ptr<RtpPacket> RtxRtpPacket::Unpack(const RtpPacket &rtx_packet, uint8_t original_payload_type, uint32_t original_ssrc)
{
	if (rtx_packet.PayloadSize() < RTX_HEADER_SIZE)
	{
		return nullptr;
	}

	uint16_t osn = ByteReader<uint16_t>::ReadBigEndian(rtx_packet.Payload());

	// Copy original payload to a temp buffer first since SetPayload memcpys
	// into the same buffer region (source/dest would overlap otherwise).
	std::vector<uint8_t> original_payload(rtx_packet.Payload() + RTX_HEADER_SIZE,
										   rtx_packet.Payload() + rtx_packet.PayloadSize());

	auto packet = std::make_shared<RtpPacket>(rtx_packet);
	packet->SetPayloadType(original_payload_type);
	packet->SetSsrc(original_ssrc);
	packet->SetSequenceNumber(osn);
	packet->SetPayload(original_payload.data(), original_payload.size());

	return packet;
}