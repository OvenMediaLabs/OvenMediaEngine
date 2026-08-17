/*
 * We referenced this code from owt-deps-webrtc(https://github.com/open-webrtc-toolkit/owt-deps-webrtc). 
 */

#include "rtp_packetizer_h265.h"
#include "base/ovlibrary/byte_io.h"

RtpPacketizerH265::RtpPacketizerH265()
	: _num_packets_left(0)
{
}

RtpPacketizerH265::~RtpPacketizerH265() 
{

}

size_t RtpPacketizerH265::SetPayloadData(size_t max_payload_len, size_t last_packet_reduction_len, const RTPVideoTypeHeader *rtp_type_header, FrameType frame_type,
										const uint8_t* payload_data, size_t payload_size, const FragmentationHeader* fragmentation) 
{
	_max_payload_len = max_payload_len;
	_last_packet_reduction_len = last_packet_reduction_len;
	_packetization_mode = rtp_type_header->h26X.packetization_mode;

	for(size_t i = 0; i < fragmentation->GetCount(); ++i) 
	{
		const uint8_t* buffer = &payload_data[fragmentation->fragmentation_offset[i]];
		size_t length = fragmentation->fragmentation_length[i];
		_input_fragments.push_back(Fragment(buffer, length));
	}

	if(!GeneratePackets()) 
	{
		_num_packets_left = 0;
		while (!_packets.empty()) 
		{
			_packets.pop();
		}
		return 0;
	}
	return _num_packets_left;
}

bool RtpPacketizerH265::GeneratePackets() 
{
	for (size_t i = 0; i < _input_fragments.size();) 
	{
		switch (_packetization_mode) 
		{
			// TODO: Need to check if SingleNALUnit mode is used in H.265
			// for now, only use PacketizeSingleNalu for H.265, similar to H.264
			case H26XPacketizationMode::SingleNalUnit:
				if(!PacketizeSingleNalu(i))
				{
					return false;
				}
				++i;
				break;
			case H26XPacketizationMode::NonInterleaved:
				size_t fragment_len = _input_fragments[i].length;

				if (i + 1 == _input_fragments.size()) 
				{
					fragment_len += _last_packet_reduction_len;
				}

				if (fragment_len > _max_payload_len) 
				{
					PacketizeFuA(i);
					++i;
				} 
				else if (IsAggregationPossible(i))
				{
					i = PacketizeAp(i);
				}
				else
				{
					PacketizeSingleNalu(i);
					++i;
				}
				break;
		}
	}
	return true;
}

bool RtpPacketizerH265::IsAggregationPossible(size_t fragment_index) const
{
	if (fragment_index + 1 >= _input_fragments.size())
	{
		return false;
	}

	size_t space_left = _max_payload_len;
	
	// First fragment: Payload Header(2) + Length Field(2) + Data
	const size_t first_needed = H265_NAL_HEADER_SIZE + H265_LENGTH_FIELD_SIZE + _input_fragments[fragment_index].length;

	if (first_needed > space_left)
	{
		return false;
	}
	space_left -= first_needed;

	// Second fragment: Length field(2) + Data
	const size_t next_fragment_index = fragment_index + 1;
	size_t next_needed = H265_LENGTH_FIELD_SIZE + _input_fragments[next_fragment_index].length;

	// If Last fragment, also calculate for _last_packet_reduction_len.
	if ((next_fragment_index + 1) == _input_fragments.size())
	{
		next_needed += _last_packet_reduction_len;
	}

	return space_left >= next_needed;
}

void RtpPacketizerH265::PacketizeFuA(size_t fragment_index) 
{
	const Fragment& fragment = _input_fragments[fragment_index];
	bool is_last_fragment = fragment_index + 1 == _input_fragments.size();
	size_t payload_left = fragment.length - H265_NAL_HEADER_SIZE;
	size_t offset = H265_NAL_HEADER_SIZE;
	size_t per_packet_capacity = _max_payload_len - (H265_FU_HEADER_SIZE + H265_NAL_HEADER_SIZE);

	size_t extra_len = is_last_fragment ? _last_packet_reduction_len : 0;

	size_t num_packets = (payload_left + extra_len + (per_packet_capacity - 1)) / per_packet_capacity;
	size_t payload_per_packet = (payload_left + extra_len) / num_packets;
	size_t num_larger_packets = (payload_left + extra_len) % num_packets;
#if DEBUG	
	size_t fragmented_packets = 0;
#endif

	_num_packets_left += num_packets;
	while (payload_left > 0) 
	{
		if (num_packets == num_larger_packets)
		{
			++payload_per_packet;
		}
		size_t packet_length = payload_per_packet;
		if (payload_left <= packet_length) 
		{
			packet_length = payload_left;
			if (num_packets == 2) 
			{
				--packet_length;
			}
		}

		// NAL Header
		uint16_t header = (fragment.buffer[0] << 8) | fragment.buffer[1];
		_packets.push(PacketUnit(Fragment(fragment.buffer + offset, packet_length),
		                         offset - H265_NAL_HEADER_SIZE == 0 /* first */,
		                         payload_left == packet_length /* last */, 
								 false /* aggregated */, header));
		offset += packet_length;
		payload_left -= packet_length;
		--num_packets;
#if DEBUG
		++fragmented_packets;
#endif
	}

#if DEBUG
	logt("RtpPacketizerH265", "Packetized one fragment into %zu FU-A packets, total size: %zu", fragmented_packets, fragment.length);
#endif
}

size_t RtpPacketizerH265::PacketizeAp(size_t fragment_index) 
{
	// Aggregate fragments into one packet (AP).
	size_t payload_size_left = _max_payload_len;
	int aggregated_fragments = 0;
	size_t fragment_headers_length = 0;
	const Fragment* fragment = &_input_fragments[fragment_index];
	++_num_packets_left;

	// The header size for the first fragment is  (NAL header size +  length field size),
	// and for subsequent fragments, only the (length field size) is added.
	fragment_headers_length = H265_NAL_HEADER_SIZE + H265_LENGTH_FIELD_SIZE;

	while (payload_size_left >= fragment->length + fragment_headers_length &&
	       (fragment_index + 1 < _input_fragments.size() ||
	        payload_size_left >= fragment->length + fragment_headers_length + _last_packet_reduction_len)) 
	{
		uint16_t header = (fragment->buffer[0] << 8) | fragment->buffer[1];
		_packets.push(PacketUnit(*fragment, aggregated_fragments == 0 /* first */, false /* last */, true /* aggregated */, header));
		payload_size_left -= fragment->length;
		payload_size_left -= fragment_headers_length;
		++aggregated_fragments;

		// Next fragment.
		++fragment_index;
		if (fragment_index == _input_fragments.size())
		{
			break;
		}
		fragment = &_input_fragments[fragment_index];
		fragment_headers_length = H265_LENGTH_FIELD_SIZE;
	}
	_packets.back().last_fragment = true;

#if DEBUG
	logt("RtpPacketizerH265", "Packetized %d fragments into AP packet, total size: %zu", aggregated_fragments, _max_payload_len - payload_size_left);
#endif

	return fragment_index;
}

bool RtpPacketizerH265::PacketizeSingleNalu(size_t fragment_index) 
{
	// Add a single NALU to the queue, no aggregation.
	size_t payload_size_left = _max_payload_len;
	if (fragment_index + 1 == _input_fragments.size())
	{	
		payload_size_left -= _last_packet_reduction_len;
	}
	
	const Fragment* fragment = &_input_fragments[fragment_index];
	if (payload_size_left < fragment->length) 
	{
		return false;
	}
	
	uint16_t header = (fragment->buffer[0] << 8) | fragment->buffer[1];
	_packets.push(PacketUnit(*fragment, true /* first */, true /* last */, false /* aggregated */, header));
	++_num_packets_left;

#if DEBUG
	logt("RtpPacketizerH265", "Packetized one fragment into one single NALU packet, total size: %zu", fragment->length);
#endif

	return true;
}

bool RtpPacketizerH265::NextPacket(RtpPacket* rtp_packet) 
{
	if (_packets.empty()) 
	{
		return false;
	}

	PacketUnit packet = _packets.front();
	
	if (packet.first_fragment && packet.last_fragment) 
	{
		NextSingleUnitPacket(rtp_packet);
	} 
	else if (packet.aggregated) 
	{
		bool is_last_packet = _num_packets_left == 1;
		NextAggregatePacket(rtp_packet, is_last_packet);
	} 
	else 
	{
		NextFragmentPacket(rtp_packet);
	}
	
	rtp_packet->SetMarker(_packets.empty());
	--_num_packets_left;
	
	return true;
}

void RtpPacketizerH265::NextSingleUnitPacket(RtpPacket* rtp_packet)
{
	PacketUnit& packet = _packets.front();
	size_t bytes_to_send = packet.source_fragment.length;
	uint8_t* buffer = rtp_packet->AllocatePayload(bytes_to_send);
	memcpy(buffer, packet.source_fragment.buffer, bytes_to_send);
	_packets.pop();
	_input_fragments.pop_front();
}

// H.265 AP (Aggregation Packet) structure [RFC 7798]
//
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |      PayloadHdr (Type=48)     |      NALU 1 Size (16bit)      |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |           NALU 1 Data (NAL header + RBSP) ...                 |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |      NALU 2 Size (16bit)      |  NALU 2 Data ...              |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
// PayloadHdr (2 bytes):
//  +---------------+-----------------------+
//  |F| Type(6bit)  |LayerID(6bit)|TID(3bit)|
//  +---------------+-----------------------+
//    Type = 48 (kHevcAp)
//    F = OR of all F bits in the aggregated NAL units
//    LayerID = min of all LayerID values in the aggregated NAL units
//    TID = min of all TID values in the aggregated NAL units
void RtpPacketizerH265::NextAggregatePacket(RtpPacket* rtp_packet, bool last) 
{
	uint8_t* buffer = rtp_packet->AllocatePayload(last ? _max_payload_len - _last_packet_reduction_len : _max_payload_len);
	PacketUnit* packet = &_packets.front();

	uint8_t ap_f        = 0;
	uint8_t ap_layer_id = 0x3F;  // 6-bit max, replaced by min
	uint8_t ap_tid      = 0x07;  // 3-bit max, replaced by min

	size_t index = H265_NAL_HEADER_SIZE;
	bool is_last_fragment = packet->last_fragment;
	
	while (packet->aggregated) 
	{
		// Accumulate F (OR), LayerID (min), TID (min).
		uint8_t hdr_h = packet->header >> 8;
		uint8_t hdr_l = packet->header & 0xFF;
		ap_f |= (hdr_h & 0x80);
		uint8_t cur_layer_id = ((hdr_h & kHevcLayerIDHMask) << 5) | (hdr_l >> 3);
		uint8_t cur_tid      = hdr_l & 0x07;
		if (cur_layer_id < ap_layer_id) ap_layer_id = cur_layer_id;
		if (cur_tid      < ap_tid)      ap_tid      = cur_tid;

		const Fragment& fragment = packet->source_fragment;
		// Add NAL unit length field.
		ByteWriter<uint16_t>::WriteBigEndian(&buffer[index], fragment.length);
		index += H265_LENGTH_FIELD_SIZE;
		// Add NAL unit.
		memcpy(&buffer[index], fragment.buffer, fragment.length);
		index += fragment.length;

		_packets.pop();
		_input_fragments.pop_front();

		if (is_last_fragment)
		{	
			break;
		}
		packet = &_packets.front();
		is_last_fragment = packet->last_fragment;
	}

	// Write AP PayloadHdr with aggregated F, LayerID, TID.
	// byte0: F(1) | Type=kHevcAp(6) | LayerID_high(1)
	// byte1: LayerID_low(5) | TID(3)
	buffer[0] = ap_f | (kHevcAp << 1) | (ap_layer_id >> 5);
	buffer[1] = ((ap_layer_id & 0x1F) << 3) | ap_tid;
	rtp_packet->SetPayloadSize(index);
}

void RtpPacketizerH265::NextFragmentPacket(RtpPacket* rtp_packet) 
{
	PacketUnit* packet = &_packets.front();
	// NAL unit fragmented over multiple packets (FU).
	// We do not send original NALU header, so it will be replaced by the
	// PayloadHdr of the first packet.
	uint8_t payload_hdr_h = packet->header >> 8;  // 1-bit F, 6-bit type, 1-bit layerID highest-bit
	uint8_t payload_hdr_l = packet->header & 0xFF;
	uint8_t layer_id_h = payload_hdr_h & kHevcLayerIDHMask;
	uint8_t fu_header = 0;
	// S | E |6 bit type.
	fu_header |= (packet->first_fragment ? kHevcSBit : 0);
	fu_header |= (packet->last_fragment ? kHevcEBit : 0);
	uint8_t type = (payload_hdr_h & kHevcTypeMask) >> 1;
	fu_header |= type;
	// Now update payload_hdr_h with FU type.
	payload_hdr_h = (payload_hdr_h & kHevcTypeMaskN) | (kHevcFu << 1) | layer_id_h;

	const Fragment& fragment = packet->source_fragment;

	uint8_t* buffer = rtp_packet->AllocatePayload(H265_FU_HEADER_SIZE + H265_NAL_HEADER_SIZE + fragment.length);
	buffer[0] = payload_hdr_h;
	buffer[1] = payload_hdr_l;
	buffer[2] = fu_header;

	memcpy(buffer + H265_FU_HEADER_SIZE + H265_NAL_HEADER_SIZE, fragment.buffer, fragment.length);
	if (packet->last_fragment)
	{
		_input_fragments.pop_front();
	}
	_packets.pop();
}
