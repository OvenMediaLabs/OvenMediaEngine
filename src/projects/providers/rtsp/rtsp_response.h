#pragma once

#include <stdint.h>

class RtspResponse
{
public:
    RtspResponse(uint16_t status, uint32_t cseq);

private:
    uint16_t status_;
    uint32_t cseq_;
};
