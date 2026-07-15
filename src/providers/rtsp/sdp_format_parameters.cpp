#include "sdp_format_parameters.h"

SdpFormatParameters::Type Mpeg4SdpFormatParameters::GetType() const
{
    return Type::Mpeg4;
}

SdpFormatParameters::Type H264SdpFormatParameters::GetType() const
{
    return Type::H264;
}
