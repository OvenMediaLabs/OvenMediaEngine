#include "rtp_packetizing_manager.h"
#include "rtp_packetizer_vp8.h"
#include "rtp_packetizer_h264.h"
#include "rtp_packetizer_h265.h"

#include <base/ovlibrary/converter.h>

std::shared_ptr<RtpPacketizingManager> RtpPacketizingManager::Create(cmn::MediaCodecId type)
{
	switch(type)
	{
		case cmn::MediaCodecId::Vp8:
			return std::make_shared<RtpPacketizerVp8>();

		case cmn::MediaCodecId::H264:
			return std::make_shared<RtpPacketizerH264>();

		case cmn::MediaCodecId::H265:
			return std::make_shared<RtpPacketizerH265>();

		default:
			// Not supported
			break;
	}

	loge("rtp_rtcp", "Cannot create %s RTP packetizer", cmn::GetCodecIdString(type));

	return nullptr;
}
