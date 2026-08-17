#include "rtsp_request.h"

#include <string>

RtspRequest::RtspRequest(std::vector<uint8_t> data) : data_(std::move(data))
{
}

const std::vector<uint8_t> &RtspRequest::GetData() const
{
    return data_;
}

const std::string_view &RtspRequest::GetMethod() const
{
    return method_;
}

const std::string_view &RtspRequest::GetUri() const 
{
    return uri_;
}

uint32_t RtspRequest::GetCSeq() const
{
    return cseq_;
}

size_t RtspRequest::GetContentLength()
{
    return content_length_;
}

const std::vector<uint8_t> &RtspRequest::GetBody() const
{
    return body_;
}

std::string_view RtspRequest::GetHeader(const std::string &header_name) const
{
    auto header_iterator = headers_.find(header_name);
    if (header_iterator != headers_.end())
    {
        return header_iterator->second;
    }
    return std::string_view();
}

const std::unordered_map<std::string_view, std::string_view> &RtspRequest::GetHeaders() const
{
    return headers_;
}

std::unique_ptr<RtspRequest> RtspRequest::Parse(std::vector<uint8_t> data)
{
    auto rtsp_request = std::make_unique<RtspRequest>(std::move(data));
    constexpr char line_end_marker[] = {'\r', '\n'};
    const uint8_t *line_start_position = rtsp_request->data_.data(),
        *request_end_position = rtsp_request->data_.data() + rtsp_request->data_.size(),
        *line_end_position = nullptr;
    bool is_status_line = true;
    bool continue_parsing = true;
    while (continue_parsing)
    {
        line_end_position = reinterpret_cast<uint8_t*>(memmem(line_start_position, request_end_position - line_start_position, line_end_marker, sizeof(line_end_marker)));
        if (line_end_position == nullptr)
        {
            line_end_position = request_end_position;
            continue_parsing = false;
        }
        std::string_view line(reinterpret_cast<const char*>(line_start_position), line_end_position - line_start_position);
        if (is_status_line)
        {
            auto space_position = line.find(' ');
            if (space_position == std::string_view::npos)
            {
                return nullptr;
            }
            rtsp_request->method_ = std::string_view(reinterpret_cast<const char*>(line_start_position), space_position);
            const auto uri_start_position = line.find_first_not_of(' ', space_position + 1);
            if (uri_start_position == std::string_view::npos)
            {
                return nullptr;
            }
            space_position = line.find(' ', uri_start_position + 1);
            if (space_position == std::string_view::npos)
            {
                return nullptr;
            }
            rtsp_request->uri_ = std::string_view(reinterpret_cast<const char*>(line_start_position) + uri_start_position, space_position - uri_start_position);
            is_status_line = false;
        }
        else
        {
            auto colon_position = line.find(':');
            if (colon_position != std::string_view::npos)
            {
                std::string_view header_name(reinterpret_cast<const char*>(line_start_position), colon_position);
                auto header_start_position = line.find_first_not_of(' ', colon_position + 1);
                if (header_start_position != std::string_view::npos)
                {
                    std::string_view header_value(reinterpret_cast<const char*>(line_start_position) + header_start_position, line.size() - header_start_position);
                    if (header_name == "CSeq")
                    {
                        rtsp_request->cseq_ = std::stoi(std::string(header_value.begin(), header_value.end()));
                    }
                    else if (header_name == "Content-Length")
                    {
                        rtsp_request->content_length_ = std::stoi(std::string(header_value.begin(), header_value.end()));
                    }
                    else
                    {
                        rtsp_request->headers_[header_name] = header_value;
                    }
                }
            }
        }
        line_start_position = line_end_position + sizeof(line_end_marker);
    }
    return rtsp_request;
}

void RtspRequest::SetBody(std::vector<uint8_t> body)
{
    body_ = std::move(body);
}