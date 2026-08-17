#include "rtsp_response.h"

RtspResponse::RtspResponse(uint16_t status, uint32_t cseq) : status_(status),
    cseq_(cseq)
{
}