#include "rtcp_packet_header.h"

#include <base/ovlibrary/byte_ordering.h>

RtcpPacketHeader RtcpPacketHeaderFromData(const uint8_t (&data)[RtcpPacketHeaderSize])
{
    RtcpPacketHeader rtcp_packet_header;
    rtcp_packet_header.version_ = (data[0] & 0b11000000) >> 6;
    rtcp_packet_header.padding_ = (data[0] & 0b100000) >> 5;
    rtcp_packet_header.report_count_ = (data[0] & 0b11111);
    rtcp_packet_header.packet_type = data[1];
    rtcp_packet_header.length_ = ntohs(*reinterpret_cast<const uint16_t*>(&data[2]));
    rtcp_packet_header.ssrc_ = ntohl(*reinterpret_cast<const uint32_t*>(&data[4]));
    return rtcp_packet_header;
}
