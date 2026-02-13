#pragma once
#include "base/ovlibrary/ovlibrary.h"
#include "rtcp_info.h"
#include "../rtcp_packet.h"
#include "report_block.h"

//         0                   1                   2                   3
//         0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// header |V=2|P|    RC   |   PT=SR=200   |             length            |
//        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//        |                         SSRC of sender                        |
//        +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// sender |              NTP timestamp, most significant word             |
// info   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//        |             NTP timestamp, least significant word             |
//        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//        |                         RTP timestamp                         |
//        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//        |                     sender's packet count                     |
//        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//        |                      sender's octet count                     |
//        +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
class SenderReport : public RtcpInfo
{
public:
	SenderReport();
	~SenderReport();

	///////////////////////////////////////////
	// Implement RtcpInfo virtual functions
	///////////////////////////////////////////
	
	bool Parse(const RtcpPacket &packet) override;
	std::shared_ptr<ov::Data> GetData() const override;
	void DebugPrint() override;

	RtcpPacketType GetPacketType() const override
	{
		return RtcpPacketType::SR;
	}

	uint8_t GetCountOrFmt() const override
	{
		return _report_block_list.size();
	}

	bool HasPadding() const override
	{
		return false;
	}

	uint32_t	GetSenderSsrc(){return _sender_ssrc;}
	uint32_t	GetMsw(){return _msw;}
	uint32_t	GetLsw(){return _lsw;}
	uint32_t	GetTimestamp(){return _timestamp;}
	uint32_t	GetPacketCount(){return _packet_count;}
	uint32_t	GetOctetCount(){return _octet_count;}

	void SetSenderSsrc(uint32_t ssrc);
	void SetMsw(uint32_t msw);
	void SetLsw(uint32_t lsw);
	void SetTimestamp(uint32_t timestamp);
	void SetPacketCount(uint32_t count);
	void SetOctetCount(uint32_t count);

private:
	uint32_t	_sender_ssrc = 0;
	uint32_t	_msw = 0;
	uint32_t	_lsw = 0;
	uint32_t	_timestamp = 0;
	uint32_t	_packet_count = 0;
	uint32_t	_octet_count = 0;

	// Doen't use report block list now
	std::vector<std::shared_ptr<ReportBlock>>	_report_block_list;

	std::shared_ptr<ov::Data>	_data = nullptr;
	uint8_t* _buffer = nullptr;
};