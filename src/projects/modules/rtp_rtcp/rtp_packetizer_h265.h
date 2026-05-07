/*
 * We referenced this code from owt-deps-webrtc(https://github.com/open-webrtc-toolkit/owt-deps-webrtc). 
 */

#pragma once

#include "rtp_packet.h"
#include "rtp_packetizing_manager.h"
#include <deque>
#include <memory>
#include <queue>
#include <string>

#define H265_NAL_HEADER_SIZE	2
#define H265_FU_HEADER_SIZE		1
#define H265_LENGTH_FIELD_SIZE	2

enum HevcNalHdrMasks 
{
	kHevcFBit = 0x80,
	kHevcTypeMask = 0x7E,
	kHevcLayerIDHMask = 0x1,
	kHevcLayerIDLMask = 0xF8,
	kHevcTIDMask = 0x7,
	kHevcTypeMaskN = 0x81,
	kHevcTypeMaskInFuHeader = 0x3F
};

// Bit masks for FU headers.
enum HevcFuDefs 
{ 
	kHevcSBit = 0x80, 
	kHevcEBit = 0x40, 
	kHevcFuTypeBit = 0x3F 
};

enum HevcRTPNaluType
{
	kHevcAp = 48,
	kHevcFu = 49
};

class RtpPacketizerH265 : public RtpPacketizingManager 
{
public:
	RtpPacketizerH265();
	~RtpPacketizerH265() override;

	size_t SetPayloadData(size_t max_payload_len, size_t last_packet_reduction_len, const RTPVideoTypeHeader *rtp_type_header, FrameType frame_type,
							const uint8_t* payload_data, size_t payload_size, const FragmentationHeader* fragmentation) override;

	bool NextPacket(RtpPacket* rtp_packet) override;

private:
	struct Fragment 
	{
		Fragment(const uint8_t* buffer, size_t length)
			: buffer(buffer), length(length) 
		{
		}
		Fragment(const Fragment& fragment)
			: buffer(fragment.buffer), length(fragment.length) 
		{
		}
		~Fragment() = default;

		const uint8_t* buffer = nullptr;
		size_t length = 0;
	};

	struct PacketUnit 
	{
		PacketUnit(const Fragment& source_fragment,
		           bool first_fragment,
		           bool last_fragment,
		           bool aggregated,
		           uint16_t header)
			: source_fragment(source_fragment),
			  first_fragment(first_fragment),
			  last_fragment(last_fragment),
			  aggregated(aggregated),
			  header(header) {}

		const Fragment source_fragment;
		bool first_fragment;
		bool last_fragment;
		bool aggregated;
		uint16_t header;
	};

	bool GeneratePackets();
	void PacketizeFuA(size_t fragment_index);
	size_t PacketizeAp(size_t fragment_index);	
	bool IsAggregationPossible(size_t fragment_index) const;	
	bool PacketizeSingleNalu(size_t fragment_index);
	void NextSingleUnitPacket(RtpPacket* rtp_packet);
	void NextAggregatePacket(RtpPacket* rtp_packet, bool last);
	void NextFragmentPacket(RtpPacket* rtp_packet);

	size_t _max_payload_len;
	size_t _last_packet_reduction_len;
	size_t _num_packets_left;
	H26XPacketizationMode _packetization_mode;
	std::deque<Fragment> _input_fragments;
	std::queue<PacketUnit> _packets;
};