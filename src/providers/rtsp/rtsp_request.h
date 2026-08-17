#pragma once

#include "rtsp.h"

#include <string_view>
#include <unordered_map>
#include <stdint.h>
#include <vector>
#include <memory>

class RtspRequest
{
public:
    RtspRequest(std::vector<uint8_t> data);

    static std::unique_ptr<RtspRequest> Parse(std::vector<uint8_t> data);

    const std::vector<uint8_t> &GetData() const;
    const std::string_view &GetMethod() const;
    const std::string_view &GetUri() const;
    uint32_t GetCSeq() const;
    size_t GetContentLength();
    std::string_view GetHeader(const std::string &header_name) const;
    const std::unordered_map<std::string_view, std::string_view> &GetHeaders() const;
    const std::vector<uint8_t> &GetBody() const;
    void SetBody(std::vector<uint8_t> body);

private:
    const std::vector<uint8_t> data_;
    std::string_view method_;
    std::string_view uri_;
    uint32_t cseq_ = 0;
    std::pair<uint8_t, uint8_t> version_;
    std::unordered_map<std::string_view, std::string_view> headers_;
    size_t content_length_ = 0;
    std::vector<uint8_t> body_;
};