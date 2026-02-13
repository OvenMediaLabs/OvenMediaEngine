#pragma once

#include "rtp_depacketizing_manager.h"
#include "rtp_rtcp_defines.h"

#define NAL_FBIT 		0x80
#define NAL_NRI_MASK 	0x60
#define NAL_TYPE_MASK 	0x1F

#define FUA_SBIT		0x80
#define FUA_EBIT		0x40
#define FUA_RBIT		0x20

#define NAL_HEADER_SIZE 	1
#define FUA_HEADER_SIZE		2
#define LENGTH_FIELD_SIZE	2

#define ANNEXB_START_PREFIX_LENGTH	4

class RtpDepacketizerH264 : public RtpDepacketizingManager
{
public:
	std::shared_ptr<ov::Data> ParseAndAssembleFrame(std::vector<std::shared_ptr<ov::Data>> payload_list) override;
	std::shared_ptr<ov::Data> GetDecodingParameterSetsToAnnexB() override;

private:
	std::shared_ptr<ov::Data> ParseFuaAndConvertAnnexB(const std::shared_ptr<ov::Data> &payload, bool start=false);
	std::shared_ptr<ov::Data> ParseStapAAndConvertToAnnexB(const std::shared_ptr<ov::Data> &payload);
	std::shared_ptr<ov::Data> ConvertSingleNaluToAnnexB(const std::shared_ptr<ov::Data> &payload);

	bool IsDecodingParmeterSets(uint8_t nal_unit_type);

};