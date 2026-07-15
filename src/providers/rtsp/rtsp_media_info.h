#pragma once

#include "sdp_format_parameters.h"

#include <base/info/media_track.h>
#include <base/info/media_extradata.h>

#include <unordered_map>
#include <string>

struct RtspMediaInfo
{
    std::unordered_map<uint8_t, MediaTrack> payloads_;
    std::unordered_map<uint8_t, std::shared_ptr<SdpFormatParameters>> format_parameters_;
    std::unordered_map<std::string, uint8_t> tracks_;
};

template<typename T>
struct SdpFormatType;

template<>
struct SdpFormatType<Mpeg4SdpFormatParameters>
{
    static const SdpFormatParameters::Type type = SdpFormatParameters::Type::Mpeg4;
};

template<>
struct SdpFormatType<H264SdpFormatParameters>
{
    static const SdpFormatParameters::Type type = SdpFormatParameters::Type::H264;
};

template<typename T>
T *GetFormatParameters(RtspMediaInfo &rtsp_media_info, uint8_t payload_type)
{
    auto iterator = rtsp_media_info.format_parameters_.find(payload_type);
    if (iterator == rtsp_media_info.format_parameters_.end())
    {
        return static_cast<T*>(rtsp_media_info.format_parameters_.emplace(payload_type, std::make_shared<T>()).first->second.get());
    }
    if (iterator->second && iterator->second->GetType() == SdpFormatType<T>::type)
    {
        return static_cast<T*>(iterator->second.get());
    }
    return nullptr;
}

std::shared_ptr<SdpFormatParameters> GetFormatParameters(const RtspMediaInfo &rtsp_media_info, uint8_t payload_type);
