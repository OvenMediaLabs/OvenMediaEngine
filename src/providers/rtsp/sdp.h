#pragma once

#include "rtsp_media_info.h"

#include <stdint.h>
#include <vector>

bool ParseSdp(const std::vector<uint8_t> &sdp, RtspMediaInfo &rtsp_media_info);