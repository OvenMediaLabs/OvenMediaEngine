#include "rtsp_request_parser.h"

#include <string>

#define OV_LOG_TAG "RtspRequestParser"

RtspRequestParser::RtspRequestParser(RtspRequestConsumer &rtsp_request_consumer) : rtsp_request_consumer_(rtsp_request_consumer)
{
}

bool RtspRequestParser::AppendRequestData(const std::vector<uint8_t> &data)
{
    data_.insert(data_.end(), data.data(), data.data() + data.size());
    bool continue_parsing = true;
    while (continue_parsing)
    {
        if (parse_request)
        {
            constexpr char request_end_marker[] = {'\r', '\n', '\r', '\n' };
            const uint8_t *request_end_position = reinterpret_cast<uint8_t*>(memmem(data_.data() + request_end_search_offset_, data_.size() - request_end_search_offset_, request_end_marker, sizeof(request_end_marker)));
            if (request_end_position)
            {
                auto rtsp_request(RtspRequest::Parse(std::move(std::vector<uint8_t>(const_cast<const uint8_t*>(data_.data()), request_end_position))));
                off_t offset = request_end_position - data_.data();
                data_.erase(data_.begin(), data_.begin() + offset + sizeof(request_end_marker));
                if (rtsp_request && rtsp_request->GetContentLength())
                {
                    current_rtsp_request_ = std::move(rtsp_request);
                    parse_request = false;
                }
                else
                {
                    rtsp_request_consumer_.OnRtspRequest(*rtsp_request);
                }
            }
            else
            {
                request_end_search_offset_ = data_.size() - sizeof(request_end_marker) + 1;
                continue_parsing = false;
            }
        }
        else
        {
            OV_ASSERT2(current_rtsp_request_);
            if (current_rtsp_request_ && data_.size() >= current_rtsp_request_->GetContentLength())
            {
                auto rtsp_request = std::move(current_rtsp_request_);
                rtsp_request->SetBody(std::move(std::vector<uint8_t>(data_.data(), data_.data() + rtsp_request->GetContentLength())));
                data_.erase(data_.begin(), data_.begin() + rtsp_request->GetContentLength());
                parse_request = true;
                request_end_search_offset_ = 0;
                rtsp_request_consumer_.OnRtspRequest(*rtsp_request);
            }
            else
            {
                continue_parsing = false;
            }
            
        }
    }
    return data_.size() != 0;
}