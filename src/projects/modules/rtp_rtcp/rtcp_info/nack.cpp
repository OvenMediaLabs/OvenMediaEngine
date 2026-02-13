#include "nack.h"
#include "rtcp_private.h"
#include <base/ovlibrary/byte_io.h>

bool NACK::Parse(const RtcpPacket &packet)
{
	const uint8_t *payload = packet.GetPayload();
	size_t payload_size = packet.GetPayloadSize();

	if(payload_size < static_cast<size_t>(8/*SSRC * 2*/ + 4/*FCI*/))
	{
		logtt("Payload is too small to parse NACK");
		return false;
	}

	SetSrcSsrc(ByteReader<uint32_t>::ReadBigEndian(&payload[0]));
	SetMediaSsrc(ByteReader<uint32_t>::ReadBigEndian(&payload[4]));

	size_t fci_count = (packet.GetPayloadSize() - 8) / 4;
	size_t offset = 8; /* ssrc * 2 */
	for(size_t i=0; i<fci_count; i++)
	{
		auto pid = ByteReader<uint16_t>::ReadBigEndian(&payload[offset]);
		auto blp = ByteReader<uint16_t>::ReadBigEndian(&payload[offset + 2]);

		// convert to id
		_lost_ids.push_back(pid);
		pid ++;

		for(uint16_t mask = blp; mask != 0; mask >>= 1, ++pid)
		{
			if(mask & 1)
			{
				_lost_ids.push_back(pid);
			}
		}

		offset += 4; /*fci size*/
	}

	return true;
}

// RtcpInfo must provide raw data
std::shared_ptr<ov::Data> NACK::GetData() const 
{
	return nullptr;
}

void NACK::DebugPrint()
{
	ov::String ids;
	
	for(size_t i=0; i<GetLostIdCount(); i++)
	{
		uint16_t id = GetLostId(i);
		ids.AppendFormat("%u/", id);
	}

	logtt("NACK >> %s", ids.CStr());
}