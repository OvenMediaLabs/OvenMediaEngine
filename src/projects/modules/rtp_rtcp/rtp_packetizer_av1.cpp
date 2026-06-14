#include "rtp_packetizer_av1.h"

#include <algorithm>
#include <cstring>

#include <modules/bitstream/av1/av1_parser.h>

#define OV_LOG_TAG "RtpPacketizerAV1"

// Aggregation header bit masks (AV1 RTP Specification).
#define AV1_AGG_Z 0x80
#define AV1_AGG_Y 0x40
#define AV1_AGG_W_SHIFT 4
#define AV1_AGG_W_MASK 0x30
#define AV1_AGG_N 0x08

// obu_header: obu_has_size_field is bit 1 (forbidden|type(4)|extension_flag|has_size_field|reserved).
#define AV1_OBU_HAS_SIZE_FIELD 0x02

// One OBU element per AV1 RTP payload plus its possible length prefix; W is 2 bits, and the last element
// of a packet omits its length, so at most 3 elements fit one packet.
static constexpr size_t kAggregationHeaderSize = 1;
static constexpr size_t kMaxObuElementsPerPacket = 3;

size_t RtpPacketizerAV1::SetPayloadData(size_t max_payload_len, size_t last_packet_reduction_len, const RTPVideoTypeHeader * /* rtp_type_header */, FrameType /* frame_type */,
										const uint8_t *payload_data, size_t payload_size, const FragmentationHeader * /* fragmentation */)
{
	_max_payload_len		   = max_payload_len;
	_last_packet_reduction_len = last_packet_reduction_len;
	_obus.clear();
	_packets.clear();
	_next_packet			   = 0;
	_new_coded_video_sequence  = false;

	if (payload_data == nullptr || payload_size == 0)
	{
		return 0;
	}

	if (BuildObuList(payload_data, payload_size) == false)
	{
		return 0;
	}

	if (GeneratePackets() == false)
	{
		return 0;
	}

	return _packets.size();
}

bool RtpPacketizerAV1::BuildObuList(const uint8_t *data, size_t size)
{
	size_t offset = 0;
	while (offset < size)
	{
		Av1ObuSpan span;
		if (Av1Parser::ReadObu(data, size, offset, span) == false)
		{
			logte("Malformed AV1 OBU stream while packetizing");
			return false;
		}

		if (span.header.type == Av1ObuType::TemporalDelimiter)
		{
			// Dropped: the RTP framing conveys temporal-unit boundaries (AV1 RTP spec).
			offset = span.next_offset;
			continue;
		}

		// A low-overhead OBU stream is size-delimited. Without a size field, ReadObu consumes the rest
		// of the buffer as this OBU's payload and any following OBUs are lost; fail loud instead.
		if (span.header.has_size_field == false)
		{
			logte("AV1 OBU (type %s) without size field in a low-overhead stream; dropping temporal unit",
				  EnumToString(span.header.type));
			return false;
		}

		if (span.header.type == Av1ObuType::SequenceHeader)
		{
			_new_coded_video_sequence = true;
		}

		Obu obu;
		obu.header_size = span.header.extension_flag ? 2 : 1;
		// Carry the OBU without its size field; the depacketizer restores obu_has_size_field.
		obu.header[0] = static_cast<uint8_t>(data[span.obu_offset] & ~AV1_OBU_HAS_SIZE_FIELD);
		if (obu.header_size == 2)
		{
			obu.header[1] = data[span.obu_offset + 1];
		}
		obu.payload		 = (span.payload_size > 0) ? (data + span.payload_offset) : nullptr;
		obu.payload_size = span.payload_size;

		_obus.push_back(obu);

		offset = span.next_offset;
	}

	return _obus.empty() == false;
}

bool RtpPacketizerAV1::GeneratePackets()
{
	if (_obus.empty())
	{
		return false;
	}

	if (_max_payload_len <= kAggregationHeaderSize + _last_packet_reduction_len)
	{
		logte("AV1 max payload length(%zu) is too small to packetize", _max_payload_len);
		return false;
	}

	// OBU element bytes available per packet (the last-packet reduction is applied to every packet so
	// the final packet is always within budget; it is normally zero here).
	const size_t budget = _max_payload_len - kAggregationHeaderSize - _last_packet_reduction_len;

	Packet cur;
	size_t used = 0;	// OBU element bytes used in `cur` (excludes the aggregation header)

	auto flush = [&]() {
		if (cur.fragments.empty() == false)
		{
			_packets.push_back(std::move(cur));
			cur = Packet();
			used = 0;
		}
	};

	size_t obu_index  = 0;
	size_t obu_offset = 0;	// bytes of _obus[obu_index] already emitted in earlier packets

	while (obu_index < _obus.size())
	{
		const size_t obu_total = _obus[obu_index].Size();
		const size_t remaining = obu_total - obu_offset;
		const bool	 fresh	   = (obu_offset == 0);

		// A 4th OBU element does not fit the 2-bit W field; start a new packet.
		if (cur.fragments.size() >= kMaxObuElementsPerPacket)
		{
			flush();
		}

		// Appending an element forces a length prefix onto the previous (currently last, length-omitted)
		// element of the packet.
		size_t promote_cost = cur.fragments.empty() ? 0 : Leb128Size(cur.fragments.back().length);

		if (used + promote_cost >= budget)
		{
			// No room to promote the previous element and still carry a byte of this one.
			flush();
			promote_cost = 0;
		}

		const size_t space = budget - used - promote_cost;	// data bytes for this (length-omitted last) element

		if (remaining <= space)
		{
			cur.fragments.push_back({obu_index, obu_offset, remaining, fresh == false, false});
			used += promote_cost + remaining;
			obu_index++;
			obu_offset = 0;
		}
		else
		{
			// Fill the packet; this element continues into the next packet.
			cur.fragments.push_back({obu_index, obu_offset, space, fresh == false, true});
			obu_offset += space;
			flush();
		}
	}

	flush();

	return _packets.empty() == false;
}

bool RtpPacketizerAV1::NextPacket(RtpPacket *packet)
{
	if (_next_packet >= _packets.size())
	{
		return false;
	}

	const Packet &plan = _packets[_next_packet];
	const size_t  w	   = plan.fragments.size();	// 1..3

	// Allocate the per-packet maximum and shrink to the bytes actually written below; GeneratePackets
	// already kept each packet within budget.
	uint8_t *buffer = packet->AllocatePayload(_max_payload_len);
	if (buffer == nullptr)
	{
		return false;
	}

	// Aggregation header.
	uint8_t agg = 0;
	if (plan.fragments.front().continuation)
	{
		agg |= AV1_AGG_Z;
	}
	if (plan.fragments.back().continues)
	{
		agg |= AV1_AGG_Y;
	}
	agg |= static_cast<uint8_t>(w << AV1_AGG_W_SHIFT) & AV1_AGG_W_MASK;
	if (_next_packet == 0 && _new_coded_video_sequence)
	{
		agg |= AV1_AGG_N;
	}
	buffer[0] = agg;

	size_t pos = kAggregationHeaderSize;
	for (size_t i = 0; i < w; i++)
	{
		const Fragment &frag = plan.fragments[i];

		if (i + 1 != w)
		{
			uint8_t		 leb[Av1Parser::LEB128_MAX_SIZE];
			const size_t leb_len = Av1Parser::EncodeLeb128(frag.length, leb);
			if (leb_len == 0)
			{
				return false;
			}
			::memcpy(buffer + pos, leb, leb_len);
			pos += leb_len;
		}

		CopyObuRange(_obus[frag.obu_index], frag.offset, frag.length, buffer + pos);
		pos += frag.length;
	}

	packet->SetPayloadSize(pos);
	packet->SetMarker(_next_packet + 1 == _packets.size());
	_next_packet++;

	return true;
}

void RtpPacketizerAV1::CopyObuRange(const Obu &obu, size_t offset, size_t length, uint8_t *dst) const
{
	const size_t end = offset + length;
	size_t		 pos = offset;

	// Header bytes (obu_header [+ obu_extension_header]).
	if (pos < obu.header_size)
	{
		const size_t h_end = std::min(end, obu.header_size);
		::memcpy(dst, obu.header + pos, h_end - pos);
		dst += h_end - pos;
		pos = h_end;
	}

	// Payload bytes.
	if (pos < end)
	{
		::memcpy(dst, obu.payload + (pos - obu.header_size), end - pos);
	}
}

size_t RtpPacketizerAV1::Leb128Size(uint64_t value)
{
	uint8_t scratch[Av1Parser::LEB128_MAX_SIZE];
	return Av1Parser::EncodeLeb128(value, scratch);
}
