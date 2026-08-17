#pragma once

#include <modules/bitstream/aac/aac_converter.h>

#include "rtp_depacketizing_manager.h"
#include "rtp_rtcp_defines.h"


class RtpDepacketizerMpeg4GenericAudio : public RtpDepacketizingManager
{
public:
	enum class Mode : uint8_t
	{
		AAC_lbr = 0,
		AAC_hbr
	};

	std::shared_ptr<ov::Data> ParseAndAssembleFrame(std::vector<std::shared_ptr<ov::Data>> payload_list) override;

	// OME does not support interleaving as it is an ultra-low latency streaming server.
	bool SetConfigParams(Mode mode, uint32_t size_length, uint32_t index_length, uint32_t index_delta_length, const std::shared_ptr<ov::Data> &config);

private:
	std::shared_ptr<ov::Data> Convert(const std::shared_ptr<const ov::Data> &payload, bool include_adts_header);

	// Default setting
	Mode		_mode = Mode::AAC_hbr;
	uint32_t	_size_length = 13; // bits
	uint32_t	_index_length = 3; // bits
	uint32_t	_index_delta_length = 3; // bits
	
	AudioSpecificConfig _aac_config; // AudioSpecificConfig(), as defined in ISO/IEC 14496-3.
};