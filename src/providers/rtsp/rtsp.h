#pragma once

#include <base/ovlibrary/string.h>

#include <string_view>

enum class RtspMethod
{
    Unknown,
    Options,
    Announce,
    Setup,
    Record,
    Teardown,
    GetParameter
};

RtspMethod GetRtspMethod(const std::string_view &method);
bool ExtractApplicationAndStreamNames(const std::string_view &rtsp_uri, std::string_view &application_name, std::string_view &stream_name);
bool ExtractPath(const std::string_view &rtsp_uri, std::string_view &rtsp_path);

enum class RtpVideoFlags : uint8_t
{
    Keyframe = 1
};