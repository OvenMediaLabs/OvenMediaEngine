#include "rtsp_media_info.h"

std::shared_ptr<SdpFormatParameters> GetFormatParameters(const RtspMediaInfo &rtsp_media_info, uint8_t payload_type)
{
    auto iterator = rtsp_media_info.format_parameters_.find(payload_type);
    if (iterator != rtsp_media_info.format_parameters_.end())
    {
        return iterator->second;
    }
    return nullptr;
}