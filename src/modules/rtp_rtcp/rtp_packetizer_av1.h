#pragma once

#include <vector>

#include "rtp_packetizing_manager.h"
#include "rtp_rtcp_defines.h"

/*
	AV1 RTP Specification (https://aomediacodec.github.io/av1-rtp-spec/)

	The inverse of RtpDepacketizerAV1: take a low-overhead AV1 temporal unit (each OBU has
	obu_has_size_field == 1) and emit AV1 RTP payloads.

	[Aggregation header] (first byte of every payload)

	 0 1 2 3 4 5 6 7
	+-+-+-+-+-+-+-+-+
	|Z|Y| W |N|-|-|-|
	+-+-+-+-+-+-+-+-+

	* Z : first OBU element is a continuation of the previous packet's last OBU element
	* Y : last OBU element continues in the next packet
	* W : number of OBU elements in the packet (1..3 here; the last one omits its length field)
	* N : first packet of a new coded video sequence (set when the temporal unit has a sequence header)

	The Temporal Delimiter OBU is dropped (the RTP framing conveys temporal-unit boundaries). Each OBU
	element is carried with obu_has_size_field == 0 and no obu_size field; the depacketizer restores the
	size field. Elements larger than the MTU are fragmented across packets via the Z/Y bits.

	Dependency Descriptor / scalability are out of scope; the aggregation header alone is a valid payload.
*/

class RtpPacketizerAV1 : public RtpPacketizingManager
{
public:
	size_t SetPayloadData(size_t max_payload_len, size_t last_packet_reduction_len, const RTPVideoTypeHeader *rtp_type_header, FrameType frame_type,
						  const uint8_t *payload_data, size_t payload_size, const FragmentationHeader *fragmentation) override;

	bool NextPacket(RtpPacket *packet) override;

private:
	// One OBU element to transmit: the header (obu_has_size_field cleared) plus a pointer into the
	// source temporal unit for the payload. The obu_size field of the source OBU is not carried.
	struct Obu
	{
		uint8_t header[2] = {0, 0};	  // obu_header (+ obu_extension_header), obu_has_size_field cleared
		size_t header_size = 0;		  // 1 or 2
		const uint8_t *payload = nullptr;
		size_t payload_size = 0;

		size_t Size() const { return header_size + payload_size; }
	};

	// One contiguous slice of an OBU element placed in a packet.
	struct Fragment
	{
		size_t obu_index = 0;
		size_t offset = 0;	  // offset within the OBU element
		size_t length = 0;	  // bytes of the OBU element in this fragment
		bool continuation = false;	  // continues the previous packet's last element (Z when first in packet)
		bool continues = false;		  // continues into the next packet (Y when last in packet)
	};

	struct Packet
	{
		std::vector<Fragment> fragments;
	};

	// Split the temporal unit into OBU elements, dropping Temporal Delimiters; sets `_new_coded_video_sequence`.
	bool BuildObuList(const uint8_t *data, size_t size);

	// Lay the OBU elements out into packets that respect `max_payload_len` and the last-packet reduction.
	bool GeneratePackets();

	// Copy [offset, offset + length) of an OBU element (header bytes then payload) into `dst`.
	void CopyObuRange(const Obu &obu, size_t offset, size_t length, uint8_t *dst) const;

	// LEB128 byte count for `value` (reuses the shared encoder).
	static size_t Leb128Size(uint64_t value);

	std::vector<Obu> _obus;
	std::vector<Packet> _packets;
	size_t _next_packet = 0;

	size_t _max_payload_len = 0;
	size_t _last_packet_reduction_len = 0;
	bool _new_coded_video_sequence = false;
};
