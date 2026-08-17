#pragma once

#include "../rtsp_library.h"
#include "rtp.h"
#include "../sdp_format_parameters.h"

#include <base/ovlibrary/byte_ordering.h>
#include <base/ovlibrary/bit_reader.h>
#include <base/info/media_extradata.h>

template<typename T>
class RtpMpeg4Track : public T
{
protected:
    struct AccessUnit
    {
        uint16_t size_;
        uint16_t index_;
    };

public:
    using T::T;

    void SetSdpFormatParameters(const Mpeg4SdpFormatParameters &mpeg4_sdp_format_parameters)
    {
        mpeg4_sdp_format_parameters_ = mpeg4_sdp_format_parameters;
    }

protected:
    Mpeg4SdpFormatParameters mpeg4_sdp_format_parameters_;
};

constexpr inline bool Mpeg4SamplingRateIndex(uint32_t sampling_rate, uint8_t &sampling_rate_index)
{
    switch(sampling_rate)
    {
    case 44100:
        sampling_rate_index = 4;
        return true;
    case 48000:
        sampling_rate_index = 3;
        return true;
    }
    return false;
}
