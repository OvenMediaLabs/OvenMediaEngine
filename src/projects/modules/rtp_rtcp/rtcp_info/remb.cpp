#include "remb.h"
#include "rtcp_private.h"
#include <base/ovlibrary/byte_io.h>

bool REMB::Parse(const RtcpPacket &packet)
{
	const uint8_t *payload = packet.GetPayload();
	size_t payload_size = packet.GetPayloadSize();

	if(payload_size < static_cast<size_t>(MIN_REMB_RTCP_SIZE))
	{
		logtt("Payload is too small to parse remb");
		return false;
	}

	// Read Common Feedback
	size_t offset = 0; 

	SetSrcSsrc(ByteReader<uint32_t>::ReadBigEndian(&payload[offset]));
	offset += 4;
	SetMediaSsrc(ByteReader<uint32_t>::ReadBigEndian(&payload[4]));
	offset += 4;

	// Read REMB
	uint32_t unique_identifier = ByteReader<uint32_t>::ReadBigEndian(&payload[offset]);
	offset += 4;

	if (unique_identifier != 0x52454D42) // 'R' 'E' 'M' 'B'
	{
		logtt("Invalid unique identifier");
		return false;
	}

	uint8_t num_ssrc = ByteReader<uint8_t>::ReadBigEndian(&payload[offset]);
	if (payload_size < static_cast<size_t>(MIN_REMB_RTCP_SIZE + (num_ssrc * 4)))
	{
		logtt("Payload is too small to parse remb");
		return false;
	}

	offset += 1;

	uint8_t exp = ByteReader<uint8_t>::ReadBigEndian(&payload[offset]) >> 2;	// 6 bits, 2bit for mantissa
	uint64_t mantissa = ByteReader<uint24_t>::ReadBigEndian(&payload[offset]) & 0x3FFFF; // 18 bits
	offset += 3;

	_bitrate_bps = (mantissa << exp);

	// check overflow
	if (static_cast<uint64_t>(_bitrate_bps) >> exp != mantissa)
	{
		logtt("Overflow");
		return false;
	}

	// Read SSRC
	for (int i = 0; i < num_ssrc; i++)
	{
		uint32_t ssrc = ByteReader<uint32_t>::ReadBigEndian(&payload[offset]);
		offset += 4;

		_ssrcs.push_back(ssrc);
	}

	return true;
}

// RtcpInfo must provide raw data
std::shared_ptr<ov::Data> REMB::GetData() const 
{
	return nullptr;
}

void REMB::DebugPrint()
{
	logti("[REMB]");
	logti("SrcSsrc: %u", GetSrcSsrc());
	logti("MediaSsrc: %u", GetMediaSsrc());
	logti("Bitrate BPS: %u", GetBitrateBps());

	for (const auto &ssrc : _ssrcs)
	{
		logti("SSRC: %u", ssrc);
	}
}