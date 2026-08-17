#pragma once

#include "rtp_rtcp_defines.h"
#include "rtp_packet.h"


class RtpDepacketizingManager
{
public:
	virtual ~RtpDepacketizingManager() = default;
	enum class SupportedDepacketizerType
	{
		H264,
		H265,
		VP8,
		AV1,
		OPUS,
		MPEG4_GENERIC_AUDIO
	};

	static std::shared_ptr<RtpDepacketizingManager> Create(SupportedDepacketizerType type);
	virtual std::shared_ptr<ov::Data> ParseAndAssembleFrame(std::vector<std::shared_ptr<ov::Data>> payload_list) = 0;
	virtual std::shared_ptr<ov::Data> GetDecodingParameterSetsToAnnexB() { return nullptr; };
public:
	void AddDecodingParameterSet(uint8_t type, const std::shared_ptr<ov::Data> &value);

protected:
	// Internal accessor; returns the raw map, so call only while holding _lock
	std::map<uint8_t, std::shared_ptr<ov::Data>>& GetDecodingParameterSets() OV_REQUIRES(_lock);

	// Guards _parameter_sets (the only cross-call shared state). Locked in
	// AddDecodingParameterSet (write) and GetDecodingParameterSetsToAnnexB (read);
	// GetDecodingParameterSets runs under the latter's lock
	mutable ov::Mutex _lock;

private:
	std::map<uint8_t, std::shared_ptr<ov::Data>> _parameter_sets OV_GUARDED_BY(_lock);

};
