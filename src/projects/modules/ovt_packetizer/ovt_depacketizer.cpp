// Created by getroot on 19. 12. 12.
//

#include <base/ovlibrary/byte_io.h>
#include "ovt_depacketizer.h"

#define OV_LOG_TAG		"OvtDepacketizer"

OvtDepacketizer::OvtDepacketizer()
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
	while(_packet_buffer->GetLength() >= OVT_FIXED_HEADER_SIZE)
	{
		// Parsing
		auto packet_mold = std::make_shared<OvtPacket>();

		// Parse header
		if(packet_mold->Load(*_packet_buffer) == false)
		{
			if(packet_mold->IsHeaderAvailable())
			{
				logtt("Buffer is not enough : Buffer size : %u Required size : %u", _packet_buffer->GetLength(), packet_mold->PacketLength());	
				// Not enough data to parse yet
				return true;
			}
			else
			{
				logte("Packet is invalid : buffer size (%u) required size : %u", _packet_buffer->GetLength(), packet_mold->PacketLength());
				return false;
			}
		}

		if(_packet_buffer->GetLength() == packet_mold->PacketLength())
		{
			_packet_buffer->Clear();
		}
		else
		{
			_packet_buffer = _packet_buffer->Subdata(packet_mold->PacketLength());
		}

		if(packet_mold->PayloadType() == OVT_PAYLOAD_TYPE_MESSAGE_REQUEST || 
			packet_mold->PayloadType() == OVT_PAYLOAD_TYPE_MESSAGE_RESPONSE)
		{
			if(AppendMessagePacket(packet_mold) == false)
			{
				return false;
			}
		}
		else if(packet_mold->PayloadType() == OVT_PAYLOAD_TYPE_MEDIA_PACKET)
		{
			if(AppendMediaPacket(packet_mold) == false)
			{
				return false;
			}
		}
	}

	return true;
}

bool OvtDepacketizer::IsAvailableMessage()
{
	return !_messages.empty();
}

bool OvtDepacketizer::IsAvailableMediaPacket()
{
	return !_media_packets.empty();
}

bool OvtDepacketizer::AppendMessagePacket(const std::shared_ptr<OvtPacket> &packet)
{
	//TODO(Getroot): Need to validate packet
	_message_buffer.Append(packet->Payload(), packet->PayloadLength());

	if(packet->Marker())
	{
		// Validation
		if(_message_buffer.GetLength() <= 0)
		{
			logte("Invalid message : payload size is zero");
			_message_buffer.Clear();
			return false;
		}

		auto message = _message_buffer.Clone();
		_messages.push(message);
		
		_message_buffer.Clear();
	}

	return true;
}

bool OvtDepacketizer::AppendMediaPacket(const std::shared_ptr<OvtPacket> &packet)
{
	_media_packet_buffer.Append(packet->Payload(), packet->PayloadLength());

	// The last packet of MediaPacket
	if(packet->Marker())
	{
		// Validation
		if(_media_packet_buffer.GetLength() < MEDIA_PACKET_HEADER_SIZE)
		{
			logte("Invalid media packet payload : payload size is less than header size");
			_media_packet_buffer.Clear();
			return false;
		}

		auto buffer = _media_packet_buffer.GetDataAs<uint8_t>();
		auto track_id = ByteReader<uint32_t>::ReadBigEndian(&buffer[0]);
		auto pts = ByteReader<uint64_t>::ReadBigEndian(&buffer[4]);
		auto dts = ByteReader<uint64_t>::ReadBigEndian(&buffer[12]);
		auto duration = ByteReader<uint64_t>::ReadBigEndian(&buffer[20]);
		auto media_type = static_cast<cmn::MediaType>(ByteReader<uint8_t>::ReadBigEndian(&buffer[28]));
		auto media_flag = static_cast<MediaPacketFlag>(ByteReader<uint8_t>::ReadBigEndian(&buffer[29]));
		auto bitstream_format = static_cast<cmn::BitstreamFormat>(ByteReader<uint8_t>::ReadBigEndian(&buffer[30]));
		auto packet_type = static_cast<cmn::PacketType>(ByteReader<uint8_t>::ReadBigEndian(&buffer[31]));
		auto data_size = ByteReader<uint32_t>::ReadBigEndian(&buffer[32]);

		if(data_size != _media_packet_buffer.GetLength() - MEDIA_PACKET_HEADER_SIZE)
		{
			logte("Invalid media packet payload : payload size is invalid");
			_media_packet_buffer.Clear();
			return false;
		}

		auto media_packet = std::make_shared<MediaPacket>(
			0,
			media_type, track_id,
			_media_packet_buffer.Subdata(MEDIA_PACKET_HEADER_SIZE),
			pts, dts,
			-1LL,
			MediaPacketFlag::Unknown,
			bitstream_format,
			packet_type);

		media_packet->SetFlag(media_flag);
		media_packet->SetDuration(duration);

		_media_packets.push(media_packet);

		_media_packet_buffer.Clear();
	}

	return true;
}

const std::shared_ptr<ov::Data> OvtDepacketizer::PopMessage()
{
	if(!IsAvailableMessage())
	{
		return nullptr;
	}

	auto message = _messages.front();
	_messages.pop();

	return message;
}

const std::shared_ptr<MediaPacket> OvtDepacketizer::PopMediaPacket()
{
	if(!IsAvailableMediaPacket())
	{
		return nullptr;
	}

	auto media_packet = _media_packets.front();
	_media_packets.pop();

	return media_packet;
}