#pragma once

#include "rtp_depacketizing_manager.h"
#include "rtp_rtcp_defines.h"
class RtpDepacketizerGenericAudio : public RtpDepacketizingManager
{
public:
	std::shared_ptr<ov::Data> ParseAndAssembleFrame(std::vector<std::shared_ptr<ov::Data>> payload_list) override;
};