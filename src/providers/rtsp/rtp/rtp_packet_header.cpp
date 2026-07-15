#include "rtp_packet_header.h"

#include <base/ovlibrary/byte_ordering.h>

RtpPacketHeader RtpPacketHeaderFromData(const uint8_t (&data)[RtpPacketHeaderSize])
{
    RtpPacketHeader rtp_packet_header;
    rtp_packet_header.version_ = (data[0] & 0b11000000) >> 6;
    rtp_packet_header.padding_ = (data[0] & 0b100000) >> 5;
    rtp_packet_header.extension_ = (data[0] & 0b10000) >> 4;
    rtp_packet_header.csrc_count_ = (data[0] & 0b1111);
    rtp_packet_header.marker_ = (data[1] & 0b10000000) >> 7;
    rtp_packet_header.payload_type_ = (data[1] & 0b1111111);
    rtp_packet_header.sequence_number_ = ntohs(*reinterpret_cast<const uint16_t*>(&data[2]));
    rtp_packet_header.timestamp_ = ntohl(*reinterpret_cast<const uint32_t*>(&data[4]));
    rtp_packet_header.ssrc_ = ntohl(*reinterpret_cast<const uint32_t*>(&data[8]));
    return rtp_packet_header;
}