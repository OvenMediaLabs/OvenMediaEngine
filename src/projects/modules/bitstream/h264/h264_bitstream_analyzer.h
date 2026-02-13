#pragma once

#include "h264_common.h"

#include <unordered_set>
#include <stdint.h>

class H264BitstreamAnalyzer
{
public:
	void ValidateNalUnit(const uint8_t *nal_unit_payload, size_t length, H264NalUnitType nal_unit_type, uint8_t nal_unit_header);
	void ValidateNalUnit(const uint8_t *nal_unit_payload, size_t length, uint8_t nal_unit_header);

private:
	std::unordered_set<uint8_t> known_sps_ids_;
	std::unordered_set<uint8_t> known_pps_ids_;
};