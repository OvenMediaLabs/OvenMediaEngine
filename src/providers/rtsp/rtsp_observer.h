#pragma once

#include "rtsp_media_info.h"

#include <base/ovlibrary/ovlibrary.h>
#include <config/config.h>
#include <base/info/application.h>

#include <memory>

class RtspObserver : public ov::EnableSharedFromThis<RtspObserver>
{
public:

    virtual bool OnStreamAnnounced(const ov::String &app_name, 
        const ov::String &stream_name,
        const RtspMediaInfo& rtsp_media_info,
        info::application_id_t &application_id,
        uint32_t &stream_id) = 0;

    virtual bool OnVideoData(info::application_id_t application_id,
        uint32_t stream_id,
        uint8_t track_id,
        uint32_t timestamp,
        const std::shared_ptr<std::vector<uint8_t>> &data,
        uint8_t flags, 
        std::unique_ptr<FragmentationHeader> fragmentation_header) = 0;

    virtual bool OnAudioData(info::application_id_t application_id,
        uint32_t stream_id,
        uint8_t track_id,
        uint32_t timestamp,
        const std::shared_ptr<std::vector<uint8_t>> &data) = 0;

    virtual bool OnStreamTeardown(info::application_id_t application_id, 
        uint32_t stream_id) = 0;
};
