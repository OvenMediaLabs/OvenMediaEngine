#pragma once

#include <unordered_map>
#include <unordered_set>
#include <stdint.h>
#include <vector>

/*
    Tracks SPS/PPS pairs, so that they can be retrieved for slice NAL units

    The sps_ map holds the SPS NAL unit payloads as they come (since SPS and PPS could arrive separately),
    but the pps_ set only holds known pps_ ids for fast lookups. When a connected SPS/PPS set arrives, the linked entry
    is put in pps_sps_
*/
class H264SpsPpsTracker
{
public:
    bool AddSps(const uint8_t *sps, size_t length);
    bool AddPps(const uint8_t *pps, size_t length);

    const std::pair<std::vector<uint8_t>, std::vector<uint8_t>> *GetPpsSps(uint32_t pps_id);

private:
    std::unordered_map<uint32_t, std::vector<uint8_t>> sps_;
    std::unordered_set<uint32_t> pps_;
    std::unordered_map<uint32_t, std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> pps_sps_; /* First pair element is PPS, second is SPS */
};