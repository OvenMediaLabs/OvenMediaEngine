#include "rtsp.h"

#include <string>

bool ExtractApplicationAndStreamNames(const std::string_view &rtsp_uri, std::string_view &application_name, std::string_view &stream_name)
{
    std::string_view path;
    if (ExtractPath(rtsp_uri, path) == false)
    {
        return false;
    }
    const auto application_name_end = path.find('/');
    if (application_name_end != std::string::npos)
    {
        application_name = path.substr(0, application_name_end);
        stream_name = path.substr(application_name_end + 1);
        return true;
    }
    return false;
}

bool ExtractPath(const std::string_view &rtsp_uri, std::string_view &rtsp_path)
{
    // With a properly formatted uri we should skip 3 / to get to the application name
    size_t skips = 3;
    size_t delimiter_position = 0;
    while (skips > 0)
    {
        delimiter_position = rtsp_uri.find('/', delimiter_position + 1);
        if (delimiter_position == std::string::npos)
        {
            break;
        }
        skips--;
    }
    if (delimiter_position == std::string::npos)
    {
        return false;
    }
    rtsp_path = rtsp_uri.substr(delimiter_position + 1);
    return true;
}

RtspMethod GetRtspMethod(const std::string_view &method)
{
    if (method == "OPTIONS")
    {
        return RtspMethod::Options;
    }
    else if (method == "ANNOUNCE")
    {
        return RtspMethod::Announce;
    }
    else if (method == "SETUP")
    {
        return RtspMethod::Setup;
    }
    else if (method == "RECORD")
    {
        return RtspMethod::Record;
    }
    else if (method == "TEARDOWN")
    {
        return RtspMethod::Teardown;
    }
    return RtspMethod::Unknown;
}