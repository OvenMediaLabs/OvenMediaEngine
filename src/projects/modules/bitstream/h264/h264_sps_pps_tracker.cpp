#include <modules/bitstream/nalu/nal_unit_bitstream_parser.h>
#include <base/ovlibrary/log.h>

#include "h264_sps_pps_tracker.h"
#include "h264_common.h"

#define OV_LOG_TAG "H264SpsPpsTracker"

bool H264SpsPpsTracker::AddSps(const uint8_t *sps, size_t length)
{
    size_t bytes_remaining = length;
    if (bytes_remaining)
    {
        const auto nal_unit_type = *sps & kH264NalUnitTypeMask;
        --bytes_remaining;
        if (nal_unit_type == H264NalUnitType::Sps && bytes_remaining)
        {
            NalUnitBitstreamParser parser(sps + 1, bytes_remaining);
            uint32_t sps_id = 0;
            // Skip profile_idc, constraint bits, reserved bits and level_idc
            if (parser.Skip(24) && parser.ReadUEV(sps_id) )
            {
                if (sps_.find(sps_id) == sps_.end())
                {
                    sps_.emplace(sps_id, std::move(std::vector<uint8_t>(sps, sps + length)));
                }
                return true;
            }
        }
        else
        {
            logte("NAL unit is not a valid SPS");
        }
        
    }
    return false;
}

bool H264SpsPpsTracker::AddPps(const uint8_t *pps, size_t length)
{
    size_t bytes_remaining = length;
    if (bytes_remaining)
    {
        const auto nal_unit_type = *pps & kH264NalUnitTypeMask;
        --bytes_remaining;
        if (nal_unit_type == H264NalUnitType::Pps && bytes_remaining)
        {
            NalUnitBitstreamParser parser(pps + 1, bytes_remaining);
            uint32_t pps_id = 0;
            // Skip profile_idc, constraint bits, reserved bits and level_idc
            if (parser.ReadUEV(pps_id))
            {
                if (pps_sps_.find(pps_id) == pps_sps_.end())
                {
                    uint32_t sps_id;
                    if (parser.ReadUEV(sps_id) == false)
                    {
                        return false;
                    }
                    auto sps_iterator = sps_.find(sps_id);
                    if (sps_iterator == sps_.end())
                    {
                        return false;
                    }

                    pps_sps_.emplace(pps_id, std::make_pair(std::move(std::vector<uint8_t>(pps, pps + length)), sps_iterator->second));
                    pps_.emplace(pps_id);
                    return true;
                }
            }
        }
        else
        {
            logte("NAL unit is not a valid PPS");
        }
        
    }
    return false;
}

const std::pair<std::vector<uint8_t>, std::vector<uint8_t>> *H264SpsPpsTracker::GetPpsSps(uint32_t pps_id)
{
    const auto pps_sps_iterator = pps_sps_.find(pps_id);
    if (pps_sps_iterator != pps_sps_.end())
    {
        return &pps_sps_iterator->second;
    }
    return nullptr;
}
