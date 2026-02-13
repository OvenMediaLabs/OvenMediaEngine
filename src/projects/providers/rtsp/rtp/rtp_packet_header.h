#pragma once

#include <base/ovlibrary/byte_ordering.h>

#include <stdint.h>
#include <utility>

/*

The structure below represents the RTP header part with the fixed fields
See https://tools.ietf.org/html/rfc3550#section-5.1

0                   1                   2                   3
0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|V=2|P|X|  CC   |M|     PT      |       sequence number         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           timestamp                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|           synchronization source (SSRC) identifier            |
+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

*/

constexpr size_t RtpPacketHeaderSize = 12;

#pragma pack(push, 1)
struct RtpPacketHeader
{
    uint8_t version_:2;
    uint8_t padding_:1;
    uint8_t extension_:1;
    uint8_t csrc_count_:4;
    uint8_t marker_:1;
    uint8_t payload_type_:7;
    uint16_t sequence_number_;
    uint32_t timestamp_;
    uint32_t ssrc_;
};
#pragma pack(pop)

static_assert(sizeof(RtpPacketHeader) == RtpPacketHeaderSize, "RTP header size must be equal to RtpPacketHeaderSize bytes");

RtpPacketHeader RtpPacketHeaderFromData(const uint8_t (&data)[RtpPacketHeaderSize]);