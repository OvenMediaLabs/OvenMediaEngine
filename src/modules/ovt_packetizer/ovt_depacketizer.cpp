// Created by getroot on 19. 12. 12.
//

#include "ovt_depacketizer.h"

#include <base/ovlibrary/byte_io.h>

#include "ovt_wire.h"

#define OV_LOG_TAG "OvtDepacketizer"

OvtDepacketizer::OvtDepacketizer(MediaRole media_role)
	: _media_role(media_role)
{
	_packet_buffer = std::make_shared<ov::Data>(INIT_PACKET_BUFFER_SIZE);
	_media_packet_buffer.Reserve(INIT_PAYLOAD_BUFFER_SIZE);
}

OvtDepacketizer::~OvtDepacketizer()
{
}

bool OvtDepacketizer::AppendPacket(const void *data, size_t length)
{
	_packet_buffer->Append(data, length);
	return ParsePacket();
}

bool OvtDepacketizer::AppendPacket(const std::shared_ptr<const ov::Data> &packet)
{
	_packet_buffer->Append(packet);
	return ParsePacket();
}

bool OvtDepacketizer::ParsePacket()
{
	while (_packet_buffer->GetLength() >= OVT_FIXED_HEADER_SIZE)
	{
		// Parsing
		auto packet_mold = std::make_shared<OvtPacket>();

		// Parse header
		if (packet_mold->Load(*_packet_buffer) == false)
		{
			if (packet_mold->IsHeaderAvailable())
			{
				logtt("Buffer is not enough : Buffer size : %zu Required size : %u", _packet_buffer->GetLength(), packet_mold->PacketLength());
				// Not enough data to parse yet
				return true;
			}
			else
			{
				logte("Packet is invalid : buffer size (%zu) required size : %u", _packet_buffer->GetLength(), packet_mold->PacketLength());
				return false;
			}
		}

		if (_packet_buffer->GetLength() == packet_mold->PacketLength())
		{
			_packet_buffer->Clear();
		}
		else
		{
			_packet_buffer = _packet_buffer->Subdata(packet_mold->PacketLength());
		}

		// A payload type outside the table is dropped silently; a peer may send types this build predates
		auto payload_type = ovt::FromOvtWire<OvtPayloadType>(packet_mold->PayloadType());
		if (payload_type.has_value() == false)
		{
			continue;
		}

		switch (*payload_type)
		{
			case OvtPayloadType::MessageRequest:
				[[fallthrough]];
			case OvtPayloadType::MessageResponse:
				[[fallthrough]];
			case OvtPayloadType::Required:
				if (AppendMessagePacket(*payload_type, packet_mold) == false)
				{
					return false;
				}
				break;

			case OvtPayloadType::MediaPacket:
				// Nothing on this side takes media, so the fragment goes no further than here.
				// Reassembling it first would let a peer park up to the media buffer limit per connection.
				if (_media_role == MediaRole::MessagesOnly)
				{
					_dropped_media_fragments++;
					break;
				}

				if (AppendMediaPacket(packet_mold) == false)
				{
					return false;
				}
				break;
		}
	}

	return true;
}

bool OvtDepacketizer::IsAvailableMessage()
{
	return !_items.empty() && _items.front().payload_type != OvtPayloadType::MediaPacket;
}

bool OvtDepacketizer::IsNextMessage()
{
	// Same question as IsAvailableMessage() now that items share one ordered queue;
	// delegate so the type check has a single source of truth.
	return IsAvailableMessage();
}

bool OvtDepacketizer::IsAvailableMediaPacket()
{
	return !_items.empty() && _items.front().payload_type == OvtPayloadType::MediaPacket;
}

bool OvtDepacketizer::IsAvailable()
{
	return !_items.empty();
}

bool OvtDepacketizer::AppendMessagePacket(OvtPayloadType payload_type, const std::shared_ptr<OvtPacket> &packet)
{
	// Every message type shares this buffer, so fragments of two messages must not interleave
	if (_message_payload_type.has_value() && (*_message_payload_type != payload_type))
	{
		logte("Invalid message : a payload type %u fragment arrived inside a payload type %u message",
			  ov::ToUnderlyingType(payload_type), ov::ToUnderlyingType(*_message_payload_type));
		_message_buffer.Clear();
		_message_payload_type.reset();
		return false;
	}
	_message_payload_type = payload_type;

	_message_buffer.Append(packet->Payload(), packet->PayloadLength());

	if (_message_buffer.GetLength() > MAX_MESSAGE_BUFFER_SIZE)
	{
		logte("Invalid message : reassembled size (%zu) exceeds the limit (%d)", _message_buffer.GetLength(), MAX_MESSAGE_BUFFER_SIZE);
		_message_buffer.Clear();
		_message_payload_type.reset();
		return false;
	}

	if (packet->Marker())
	{
		// Validation
		if (_message_buffer.GetLength() <= 0)
		{
			logte("Invalid message : payload size is zero");
			_message_buffer.Clear();
			_message_payload_type.reset();
			return false;
		}

		_items.push(Item{payload_type, _message_buffer.Clone(), nullptr});

		_message_buffer.Clear();
		_message_payload_type.reset();
	}

	return true;
}

bool OvtDepacketizer::AppendMediaPacket(const std::shared_ptr<OvtPacket> &packet)
{
	_media_packet_buffer.Append(packet->Payload(), packet->PayloadLength());

	if (_media_packet_buffer.GetLength() > MAX_MEDIA_PACKET_BUFFER_SIZE)
	{
		logte("Invalid media packet payload : reassembled size (%zu) exceeds the limit (%d)", _media_packet_buffer.GetLength(), MAX_MEDIA_PACKET_BUFFER_SIZE);
		_media_packet_buffer.Clear();
		return false;
	}

	// The last packet of MediaPacket
	if (packet->Marker())
	{
		// Validation
		if (_media_packet_buffer.GetLength() < MEDIA_PACKET_HEADER_SIZE)
		{
			logte("Invalid media packet payload : payload size is less than header size");
			_media_packet_buffer.Clear();
			return false;
		}

		auto buffer	   = _media_packet_buffer.GetDataAs<uint8_t>();
		auto track_id  = ByteReader<uint32_t>::ReadBigEndian(&buffer[0]);
		auto pts	   = ByteReader<uint64_t>::ReadBigEndian(&buffer[4]);
		auto dts	   = ByteReader<uint64_t>::ReadBigEndian(&buffer[12]);
		auto duration  = ByteReader<uint64_t>::ReadBigEndian(&buffer[20]);
		auto data_size = ByteReader<uint32_t>::ReadBigEndian(&buffer[32]);

		// `data_size` must equal the remaining length exactly, and every OVT1 edge checks the same,
		// so nothing can ever be appended after a media packet's payload.
		if (data_size != _media_packet_buffer.GetLength() - MEDIA_PACKET_HEADER_SIZE)
		{
			logte("Invalid media packet payload : payload size is invalid");
			_media_packet_buffer.Clear();
			return false;
		}

		// Receive-side boundary of the OVT wire table (`ovt_wire.h`); the send side is `OvtPacketizer`.
		auto media_type		  = ovt::WireEnum<cmn::MediaType>::Read(&buffer[28]);
		auto media_flag		  = ovt::WireEnum<MediaPacketFlag>::Read(&buffer[29]);
		auto bitstream_format = ovt::WireEnum<cmn::BitstreamFormat>::Read(&buffer[30]);
		auto packet_type	  = ovt::WireEnum<cmn::PacketType>::Read(&buffer[31]);

		auto media_packet	  = std::make_shared<MediaPacket>(
			media_type.value, track_id,
			_media_packet_buffer.Subdata(MEDIA_PACKET_HEADER_SIZE),
			pts, dts, duration,
			media_flag.value,
			bitstream_format.value,
			packet_type.value);

		// A byte the table had no entry for stays on the packet so a relay re-sends it unchanged
		media_packet->SetUnmappedWireValue(MediaPacket::WireField::MediaType, media_type.unmapped);
		media_packet->SetUnmappedWireValue(MediaPacket::WireField::Flag, media_flag.unmapped);
		media_packet->SetUnmappedWireValue(MediaPacket::WireField::BitstreamFormat, bitstream_format.unmapped);
		media_packet->SetUnmappedWireValue(MediaPacket::WireField::PacketType, packet_type.unmapped);

		_items.push(Item{OvtPayloadType::MediaPacket, nullptr, std::move(media_packet)});

		_media_packet_buffer.Clear();
	}

	return true;
}

std::optional<OvtDepacketizer::Message> OvtDepacketizer::PopMessage()
{
	if (!IsAvailableMessage())
	{
		return std::nullopt;
	}

	auto &item = _items.front();
	Message message{item.payload_type, std::move(item.message)};
	_items.pop();

	return message;
}

const std::shared_ptr<MediaPacket> OvtDepacketizer::PopMediaPacket()
{
	if (!IsAvailableMediaPacket())
	{
		return nullptr;
	}

	auto media_packet = _items.front().media_packet;
	_items.pop();

	return media_packet;
}