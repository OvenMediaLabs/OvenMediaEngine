#pragma once

#include "rtsp_request.h"
#include "rtsp_request_consumer.h"

#include <base/ovlibrary/data.h>

#include <memory>

class RtspRequestParser
{
public:
    RtspRequestParser(RtspRequestConsumer &rtsp_request_consumer);

    bool AppendRequestData(const std::vector<uint8_t> &data);

private:
    RtspRequestConsumer &rtsp_request_consumer_;
    bool parse_request = true;
    size_t request_end_search_offset_ = 0;
    std::vector<uint8_t> data_;
    std::unique_ptr<RtspRequest> current_rtsp_request_;
};