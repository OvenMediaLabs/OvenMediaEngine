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
// Caller (e.g. RtpNackGenerator) must add ids in ascending sequence order;
// raw uint16 sort is unsafe across 16-bit wrap.
std::shared_ptr<ov::Data> NACK::GetData() const
{
	if (_lost_ids.empty())
	{
		return nullptr;
	}

	// Group ids into FCI blocks: each block is PID + 16-bit BLP covering PID+1..PID+16.
	struct Fci
	{
		uint16_t pid;
		uint16_t blp;
	};
	std::vector<Fci> fcis;

	size_t i = 0;
	while (i < _lost_ids.size())
	{
		Fci fci{_lost_ids[i], 0};
		size_t j = i + 1;
		while (j < _lost_ids.size())
		{
			uint16_t diff = static_cast<uint16_t>(_lost_ids[j] - fci.pid);
			if (diff == 0)
			{
				j++;
				continue;
			}
			if (diff > 16)
			{
				break;
			}
			fci.blp |= static_cast<uint16_t>(1u << (diff - 1));
			j++;
		}
		fcis.push_back(fci);
		i = j;
	}

	auto data = std::make_shared<ov::Data>();
	data->SetLength(8 /*ssrc*2*/ + fcis.size() * 4 /*fci*/);
	ov::ByteStream stream(data.get());

	stream.WriteBE32(_src_ssrc);
	stream.WriteBE32(_media_ssrc);

	for (const auto &fci : fcis)
	{
		stream.WriteBE16(fci.pid);
		stream.WriteBE16(fci.blp);
	}

	return data;
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