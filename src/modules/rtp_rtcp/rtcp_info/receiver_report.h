#pragma once
#include "base/ovlibrary/ovlibrary.h"
#include "rtcp_info.h"
#include "../rtcp_packet.h"
#include "report_block.h"

class ReceiverReport : public RtcpInfo
{
public:
	///////////////////////////////////////////
	// Implement RtcpInfo virtual functions
	///////////////////////////////////////////
	bool Parse(const RtcpPacket &packet) override;
	// RtcpInfo must provide raw data
	std::shared_ptr<ov::Data> GetData() const override;
	void DebugPrint() override;

	// RtcpInfo must provide packet type
	RtcpPacketType GetPacketType() const override
	{
		return RtcpPacketType::RR;
	}

	uint8_t GetCountOrFmt() const override
	{
		return _report_blocks.size();
	}

	bool HasPadding() const override
	{
		return false;
	}
	
	// Receiver Report
	uint32_t GetSenderSsrc() const
	{
		return _sender_ssrc;
	}
	void SetSenderSsrc(uint32_t ssrc)
	{
		_sender_ssrc = ssrc;
	}

	size_t GetReportBlockCount()
	{
		return _report_blocks.size();
	}

	// begins with 0
	std::shared_ptr<ReportBlock> GetReportBlock(uint32_t index)
	{
		if(index > GetReportBlockCount() - 1)
		{
			return nullptr;
		}

		return _report_blocks[index];
	}

	bool AddReportBlock(const std::shared_ptr<ReportBlock> &block)
	{
		_report_blocks.push_back(block);
		return true;
	}

private:
	uint32_t _sender_ssrc = 0;
	std::vector<std::shared_ptr<ReportBlock>>	_report_blocks;
};