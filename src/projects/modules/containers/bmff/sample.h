//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <base/mediarouter/media_buffer.h>

namespace bmff
{
    struct Sample
    {
        // For CENC
        struct SubSample
        {
            SubSample(uint16_t clear_bytes, uint32_t cipher_bytes)
            {
                this->clear_bytes = clear_bytes;
                this->cipher_bytes = cipher_bytes;
            }

            uint16_t clear_bytes = 0;
            uint32_t cipher_bytes = 0;
        };

        struct SampleAuxInfo
        {
            int8_t GetSencAuxInfoSize() const
            {
                uint8_t per_sample_iv_size = 0;
                if (per_sample_iv)
                {
                    per_sample_iv_size = per_sample_iv->GetLength();
                }
                
                // for SENC
                // int(per_sample_IV_Size*8) + int(16) subsample_count + (unsigned int(16) clear_bytes + unsigned int(32) cipher_bytes)*subsample_count
                // return byte size
                uint8_t sub_sample_count_size = 2;
                if (_sub_samples.size() == 0)
                {
                    sub_sample_count_size = 0;
                }
            
                return per_sample_iv_size + sub_sample_count_size + ((2+4) * _sub_samples.size());
            }

            // Later, it can have more information, for example Per_Sample_IV_Size, etc.
            std::vector<SubSample> _sub_samples;
            std::shared_ptr<ov::Data> per_sample_iv;
        };

        Sample() = default;

        Sample(const std::shared_ptr<const MediaPacket> &media_packet)
        {
            _media_packet = media_packet;
        }

        Sample(const std::shared_ptr<const MediaPacket> &media_packet, const SampleAuxInfo &sai)
        {
            _media_packet = media_packet;
            _sai = sai;
        }

        std::shared_ptr<const MediaPacket> _media_packet;
        SampleAuxInfo _sai;
    };

    class Samples
    {
    public:
        bool AppendSample(const Sample &sample);
        // Get Data List
        const std::vector<Sample> &GetList() const;
        // Get Data At
        const Sample &GetAt(size_t index) const;
        void PopFront();
        // Get Start Timestamp
        int64_t GetStartTimestamp() const;
        // Get End Timestamp
        int64_t GetEndTimestamp() const;
        // Get Total Duration
        double GetTotalDuration() const;
        // Get Total Size
        uint32_t GetTotalSize() const;
        // Get Total Count
        uint32_t GetTotalCount() const;
        // Is Empty
        bool IsEmpty() const;
        // Is Independent
        bool IsIndependent() const;
    private:
        std::vector<Sample> _samples;

        int64_t _start_timestamp = 0;
        int64_t _end_timestamp = 0;
        double _total_duration = 0.0;
        uint64_t _total_size = 0;
        uint32_t _total_count = 0;
        bool _independent = false;
    };
}