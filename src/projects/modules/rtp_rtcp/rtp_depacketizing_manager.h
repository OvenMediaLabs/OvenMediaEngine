#pragma once

#include "rtp_rtcp_defines.h"
#include "rtp_packet.h"

class RtpDepacketizingManager
{
public:
	enum class SupportedDepacketizerType
	{
		H264,
		H265,
		VP8,
		OPUS,
		MPEG4_GENERIC_AUDIO
	};

	static std::shared_ptr<RtpDepacketizingManager> Create(SupportedDepacketizerType type);
	virtual std::shared_ptr<ov::Data> ParseAndAssembleFrame(std::vector<std::shared_ptr<ov::Data>> payload_list) = 0;
	virtual std::shared_ptr<ov::Data> GetDecodingParameterSetsToAnnexB() { return nullptr; };
public:	
	std::map<uint8_t, std::shared_ptr<ov::Data>>& GetDecodingParameterSets();
	void AddDecodingParameterSet(uint8_t type, const std::shared_ptr<ov::Data> &value);


private:
	std::map<uint8_t, std::shared_ptr<ov::Data>> _parameter_sets;

};
