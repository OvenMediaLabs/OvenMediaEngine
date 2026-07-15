#pragma once

#include "rtp_packet.h"
#include "rtcp_packet.h"

class RtpPacketizerInterface : public ov::EnableSharedFromThis<RtpPacketizerInterface>
{
public:
    // RTP Packet을 전송한다.
    virtual bool        OnRtpPacketized(std::shared_ptr<RtpPacket> packet) = 0;
};
