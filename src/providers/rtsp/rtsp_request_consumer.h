#pragma once

#include "rtsp_request.h"

class RtspRequestConsumer
{
public:
    virtual ~RtspRequestConsumer() = default;

    virtual void OnRtspRequest(const RtspRequest &rtsp_request) = 0;
};