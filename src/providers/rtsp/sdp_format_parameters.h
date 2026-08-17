#pragma once

#include <utility>
#include <vector>
#include <stdint.h>

class SdpFormatParameters
{
public:
    enum class Type
    {
        None,
        Mpeg4,
        H264
    };

public:
    virtual ~SdpFormatParameters() = default;

    virtual Type GetType() const = 0;
};

class Mpeg4SdpFormatParameters : public SdpFormatParameters
{
public:
    // SdpFormatParameters
    Type GetType() const override;

public:
    // https://tools.ietf.org/html/rfc3640
    // Mandatory
    uint8_t profile_level_id_ = 0;
    std::vector<uint8_t> config_;
    // Optional
    uint8_t size_length_ = 0;
    uint8_t index_length_ = 0;
    uint8_t index_delta_length_ = 0;
    uint16_t constant_size_ = 0;
    uint16_t cts_delta_length_ = 0;
    uint16_t dts_delta_length_ = 0;
};

class H264SdpFormatParameters : public SdpFormatParameters
{
public:
    // SdpFormatParameters
    Type GetType() const override;

public:
    std::vector<uint8_t> sps_;
    std::vector<uint8_t> pps_;
};