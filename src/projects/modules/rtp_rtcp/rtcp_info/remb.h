#pragma once
#include "base/ovlibrary/ovlibrary.h"
#include "rtcp_info.h"
#include "../rtcp_packet.h"

// https://datatracker.ietf.org/doc/html/draft-alvestrand-rmcat-remb-03
//     0                   1                   2                   3
//     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//    |V=2|P| FMT=15  |   PT=206      |             length            |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//    |                  SSRC of packet sender                        |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//    |                  SSRC of media source                         |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//    |  Unique identifier 'R' 'E' 'M' 'B'                            |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//    |  Num SSRC     | BR Exp    |  BR Mantissa                      |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//    |   SSRC feedback                                               |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//    |  ...                                                          |

#define MIN_REMB_RTCP_SIZE 16

class REMB : public RtcpInfo
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
		return static_cast<uint8_t>(PSFBFMT::AFB);
	}

	bool HasPadding() const override
	{
		return false;
	}
	
	// Common Feedback
	uint32_t GetSrcSsrc(){return _src_ssrc;}
	void SetSrcSsrc(uint32_t ssrc){_src_ssrc = ssrc;}
	uint32_t GetMediaSsrc(){return _media_ssrc;}
	void SetMediaSsrc(uint32_t ssrc){_media_ssrc = ssrc;}

	int64_t GetBitrateBps() const {return _bitrate_bps;}

private:
	uint32_t _src_ssrc = 0;
	uint32_t _media_ssrc = 0;

	// REMB
	int64_t _bitrate_bps = 0;
	std::vector<uint32_t> _ssrcs;
};