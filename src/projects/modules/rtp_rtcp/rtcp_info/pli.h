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

//
// PLI (https://datatracker.ietf.org/doc/html/rfc4585#section-6.3.1)
// PLI does not require parameters.  Therefore, the length field MUST be
// 2, and there MUST NOT be any Feedback Control Information.

class PLI : public RtcpInfo
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
		return static_cast<uint8_t>(PSFBFMT::PLI);
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

private:
	// FEEDBACK
	uint32_t _src_ssrc = 0;
	uint32_t _media_ssrc = 0;
};