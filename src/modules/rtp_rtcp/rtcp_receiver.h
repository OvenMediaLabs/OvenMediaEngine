#pragma once
#include "base/ovlibrary/ovlibrary.h"
#include "rtcp_packet.h"

class RtcpReceiver
{
public:
	bool ParseCompoundPacket(const std::shared_ptr<const ov::Data> &packet);
	bool HasAvailableRtcpInfo();
	std::shared_ptr<RtcpInfo> PopRtcpInfo();

private:
	std::queue<std::shared_ptr<RtcpInfo>>		_rtcp_info_list;
};