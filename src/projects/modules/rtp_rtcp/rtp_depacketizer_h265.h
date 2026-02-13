//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include "rtp_depacketizing_manager.h"
#include "rtp_rtcp_defines.h"

#define NUH_HEADER(type, layer_id, temporal_id) (((type & 0x3F) << 9) | ((layer_id & 0x3F) << 3) | (temporal_id & 0x07))
#define NUH_FORBIDDEN(payload_hdr) 				((payload_hdr >> 15) & 0x01)
#define NUH_TYPE(payload_hdr) 					((payload_hdr >> 9) & 0x3F)
#define NUH_LAYER_ID(payload_hdr) 				((payload_hdr >> 3) & 0x3F)
#define NUH_TEMPORAL_ID(payload_hdr) 			(payload_hdr & 0x07)

#define FUH_SBIT(fu_hdr) 						((fu_hdr >> 7) & 0x01)
#define FUH_EBIT(fu_hdr) 						((fu_hdr >> 6) & 0x01)
#define FUH_TYPE(fu_hdr) 						(fu_hdr & 0x3F)

#define PAYLOAD_HEADER_SIZE 					2
#define FU_HEADER_SIZE 							1
#define LENGTH_FIELD_SIZE 						2

#define DONL_FIELD_SIZE 						2
#define DOND_FIELD_SIZE 						1

#define ANNEXB_START_PREFIX_LENGTH 				4

class RtpDepacketizerH265 : public RtpDepacketizingManager
{
public:
	std::shared_ptr<ov::Data> ParseAndAssembleFrame(std::vector<std::shared_ptr<ov::Data>> payload_list) override;
	std::shared_ptr<ov::Data> GetDecodingParameterSetsToAnnexB() override;
private:
	std::shared_ptr<ov::Data> ParseFUsAndConvertAnnexB(const std::shared_ptr<ov::Data> &payload);
	std::shared_ptr<ov::Data> ParseAPsAndConvertAnnexB(const std::shared_ptr<ov::Data> &payload);
	std::shared_ptr<ov::Data> ConvertSingleNaluToAnnexB(const std::shared_ptr<ov::Data> &payload);

	void AppendBitstream(std::shared_ptr<ov::Data>&bitstream, uint8_t nal_type, uint8_t nal_header[2], const void *payload, size_t payload_length);
	bool IsDecodingParmeterSets(uint8_t nal_unit_type);

};