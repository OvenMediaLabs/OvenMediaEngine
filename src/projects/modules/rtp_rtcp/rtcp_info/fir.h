#pragma once
#include "base/ovlibrary/ovlibrary.h"
#include "rtcp_info.h"
#include "../rtcp_packet.h"

// RFC 4585: Feedback format.
//
// Common packet format:
//
//    0                   1                   2                   3
//    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |V=2|P|   FMT   |       PT      |          length               |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 0 |                  SSRC of packet sender                        |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 4 |                  SSRC of media source                         |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   :            Feedback Control Information (FCI)                 :
//   :                                                               :
//
// FIR (https://tools.ietf.org/html/rfc5104#section-4.3.1.1)
//
// FCI:
//    0                   1                   2                   3
//    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |                              SSRC                             |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   | Seq nr.       |    Reserved                                   |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

class FIR : public RtcpInfo
{
public:
	///////////////////////////////////////////
	// Implement RtcpInfo virtual functions
	///////////////////////////////////////////
	bool Parse(const RtcpPacket &header) override;
	// RtcpInfo must provide raw data
	std::shared_ptr<ov::Data> GetData() const override;
	void DebugPrint() override;

	// RtcpInfo must provide packet type
	RtcpPacketType GetPacketType() const override
	{
		return RtcpPacketType::PSFB;
	}

	// If the packet type is one of the feedback messages (205, 206) child must provide fmt(format)
	uint8_t GetCountOrFmt() const override
	{
		return static_cast<uint8_t>(PSFBFMT::FIR);
	}

	bool HasPadding() const override
	{
		return false;
	}

	// FEEDBACK
	uint32_t GetSrcSsrc() const {return _src_ssrc;}
	void SetSrcSsrc(uint32_t ssrc){_src_ssrc = ssrc;}
	uint32_t GetMediaSsrc() const {return _media_ssrc;}
	void SetMediaSsrc(uint32_t ssrc){_media_ssrc = ssrc;}

	bool AddFirMessage(uint32_t media_ssrc, uint8_t seq_no)
	{
		_fir_message.push_back(std::pair<uint32_t, uint8_t>(media_ssrc, seq_no));
		return true;
	}

	size_t GetFirMessageCount() const
	{
		return _fir_message.size();
	}

private:
	// FEEDBACK
	uint32_t _src_ssrc = 0;
	uint32_t _media_ssrc = 0;

	// FCI
	// media ssrc, sequence number
	std::vector<std::pair<uint32_t, uint8_t>> _fir_message;
};