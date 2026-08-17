#include "sender_report.h"
#include <base/ovlibrary/byte_io.h>

//    Sender report (SR) (RFC 3550).
//     0                   1                   2                   3
//     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//    |V=2|P|    RC   |   PT=SR=200   |             length            |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  0 |                         SSRC of sender                        |
//    +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//  4 |              NTP timestamp, most significant word             |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  8 |             NTP timestamp, least significant word             |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 12 |                         RTP timestamp                         |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 16 |                     sender's packet count                     |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 20 |                      sender's octet count                     |
// 24 +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

#define OV_LOG_TAG "RTCP-SR"
#define RTCP_SENDER_REPORT_BASE_SIZE	24

SenderReport::SenderReport()
{
	_data = std::make_shared<ov::Data>(RTCP_SENDER_REPORT_BASE_SIZE);
	_data->SetLength(RTCP_SENDER_REPORT_BASE_SIZE);
	_buffer = _data->GetWritableDataAs<uint8_t>();
}

SenderReport::~SenderReport()
{
	
}

bool SenderReport::Parse(const RtcpPacket &packet)
{
	if(packet.GetPayloadSize() != RTCP_SENDER_REPORT_BASE_SIZE)
	{
		logtw("RTCP SR size is not %d", RTCP_SENDER_REPORT_BASE_SIZE);
	}

	auto sr_data = packet.GetPayload();

	SetSenderSsrc(ByteReader<uint32_t>::ReadBigEndian(&sr_data[0]));
	SetMsw(ByteReader<uint32_t>::ReadBigEndian(&sr_data[4]));
	SetLsw(ByteReader<uint32_t>::ReadBigEndian(&sr_data[8]));
	SetTimestamp(ByteReader<uint32_t>::ReadBigEndian(&sr_data[12]));
	SetPacketCount(ByteReader<uint32_t>::ReadBigEndian(&sr_data[16]));
	SetOctetCount(ByteReader<uint32_t>::ReadBigEndian(&sr_data[20]));

	return true;
}

// RtcpInfo must provide raw data
std::shared_ptr<ov::Data> SenderReport::GetData() const
{
	return _data;
}

void SenderReport::DebugPrint()
{
	logti("[RTCP-SR] SSRC(%u) MSW(%u) LSW(%u) Timestamp(%u) Packet Count(%u) Octaet Count(%u)", 
		GetSenderSsrc(), GetMsw(), GetLsw(), GetTimestamp(), GetPacketCount(), GetOctetCount());
}

void SenderReport::SetSenderSsrc(uint32_t ssrc)
{
	_sender_ssrc = ssrc;
	ByteWriter<uint32_t>::WriteBigEndian(&_buffer[0], ssrc);
}

void SenderReport::SetMsw(uint32_t msw)
{
	_msw = msw;
    ByteWriter<uint32_t>::WriteBigEndian(&_buffer[4], msw);
}

void SenderReport::SetLsw(uint32_t lsw)
{
	_lsw = lsw;
    ByteWriter<uint32_t>::WriteBigEndian(&_buffer[8], lsw);
}

void SenderReport::SetTimestamp(uint32_t timestamp)
{
	_timestamp = timestamp;
    ByteWriter<uint32_t>::WriteBigEndian(&_buffer[12], timestamp);
}

void SenderReport::SetPacketCount(uint32_t count)
{
	_packet_count = count;
    ByteWriter<uint32_t>::WriteBigEndian(&_buffer[16], count);
}

void SenderReport::SetOctetCount(uint32_t count)
{
	_octet_count = count;
	ByteWriter<uint32_t>::WriteBigEndian(&_buffer[20], count);
}