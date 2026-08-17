#pragma once

#include "assert.h"
#include "base/ovlibrary/memory_view.h"

#include <stdint.h>
#include <vector>

class H264Extradata
{
public:
    std::vector<uint8_t> Serialize() const
    {
        size_t total_size = 2 * sizeof(size_t);
        {
            size_t sps_payload_size = 0;
            for (const auto &sps : sps_)
            {
                sps_payload_size += sps.size() + sizeof(size_t);
            }
            total_size += sps_payload_size;
        }
        {
            size_t pps_payload_size = 0;
            for (const auto &pps : pps_)
            {
                pps_payload_size += pps.size() + sizeof(size_t);
            }
            total_size += pps_payload_size;
        }
        std::vector<uint8_t> extradata(total_size);
        MemoryView memory_view(extradata.data(), total_size);
        memory_view << sps_ << pps_;
        OV_ASSERT2(memory_view.good());
        return extradata;
    }

    H264Extradata &AddSps(const std::vector<uint8_t> &sps)
    {
        sps_.emplace_back(sps);
        return *this;
    }

    H264Extradata &AddPps(const std::vector<uint8_t> &pps)
    {
        pps_.emplace_back(pps);
        return *this;
    }

    bool Deserialize(const std::vector<uint8_t> &extradata)
    {
        // As long as there is no writing to the memory view here, const_cast is safe
        MemoryView memory_view(const_cast<uint8_t*>(extradata.data()), extradata.size(), extradata.size());
        memory_view >> sps_ >> pps_;
        const bool result = memory_view.good() && memory_view.eof();
        OV_ASSERT2(result);
        return result;
    }

    const std::vector<std::vector<uint8_t>> &GetSps() const
    {
        return sps_;
    }

    const std::vector<std::vector<uint8_t>> &GetPps() const
    {
        return pps_;
    }
private:
    std::vector<std::vector<uint8_t>> sps_;
    std::vector<std::vector<uint8_t>> pps_;
};
