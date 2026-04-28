
#include "ice_tcp_demultiplexer.h"
#include "stun/stun_message.h"
#include "stun/channel_data_message.h"

bool IceTcpDemultiplexer::AppendData(const void *data, size_t length)
{	
	_buffer->Append(data, length);
	return ParseData();
}

bool IceTcpDemultiplexer::AppendData(const std::shared_ptr<const ov::Data> &data)
{
	_buffer->Append(data);
	return ParseData();
}

bool IceTcpDemultiplexer::IsAvailablePacket()
{
	return !_packets.empty();
}

std::shared_ptr<IceTcpDemultiplexer::Packet> IceTcpDemultiplexer::PopPacket()
{
	if(IsAvailablePacket() == false)
	{
		return nullptr;
	}

	auto packet = _packets.front();
	_packets.pop();

	return packet;
}

bool IceTcpDemultiplexer::ParseData()
{
	while(_buffer->GetLength() > MINIMUM_PACKET_HEADER_SIZE)
	{
		IceTcpDemultiplexer::ExtractResult result;

		if (_mode == Mode::RFC4571)
		{
			// RFC 6544 / RFC 4571: all packets are length-prefixed (2-byte big-endian)
			result = ExtractRfc4571Message();
		}
		else
		{
			// Only STUN and TURN Channel should be input packet types to IceTcpDemultiplexer. 
			// If another packet is input, it means a problem has occurred.

			auto type = IcePacketIdentifier::FindPacketType(_buffer);

			if(type == IcePacketIdentifier::PacketType::STUN)
			{
				result = ExtractStunMessage();
			}
			else if(type == IcePacketIdentifier::PacketType::TURN_CHANNEL_DATA)
			{
				result = ExtractChannelMessage();
			}
			else
			{
				// Critical error
				return false;
			}
		}

		// success
		if(result == ExtractResult::SUCCESS)
		{
			continue;
		}
		// retry later
		else if(result == ExtractResult::NOT_ENOUGH_BUFFER)
		{
			return true;
		}
		// error
		else if(result == ExtractResult::FAILED)
		{
			return false;
		}
	}

	return true;
}

IceTcpDemultiplexer::ExtractResult IceTcpDemultiplexer::ExtractStunMessage()
{
	ov::ByteStream stream(_buffer);
	StunMessage message;

	if(message.ParseHeader(stream) == false)
	{
		if(message.GetLastErrorCode() == StunMessage::LastErrorCode::NOT_ENOUGH_DATA)
		{
			// Not enough data, retry later
			return ExtractResult::NOT_ENOUGH_BUFFER;
		}
		else
		{	
			// Invaild data
			return ExtractResult::FAILED;
		}
	}

	uint32_t packet_size = StunMessage::DefaultHeaderLength() + message.GetMessageLength();
	auto data = _buffer->Subdata(0, packet_size);
	auto packet = std::make_shared<IceTcpDemultiplexer::Packet>(IcePacketIdentifier::PacketType::STUN, data);

	_packets.push(packet);

	_buffer = _buffer->Subdata(packet_size);

	return ExtractResult::SUCCESS;
}

IceTcpDemultiplexer::ExtractResult IceTcpDemultiplexer::ExtractChannelMessage()
{
	ChannelDataMessage message;

	if(message.LoadHeader(*_buffer) == false)
	{
		if(message.GetLastErrorCode() == ChannelDataMessage::LastErrorCode::NOT_ENOUGH_DATA)
		{
			return ExtractResult::NOT_ENOUGH_BUFFER;
		}
		else
		{
			return ExtractResult::FAILED;
		}
	}

	uint32_t packet_size = message.GetPacketLength();
	auto data = _buffer->Subdata(0, packet_size);
	auto packet = std::make_shared<IceTcpDemultiplexer::Packet>(IcePacketIdentifier::PacketType::TURN_CHANNEL_DATA, data);

	_packets.push(packet);
	_buffer = _buffer->Subdata(packet_size);

	return ExtractResult::SUCCESS;
}

// RFC 4571: 2-byte big-endian length prefix followed by the payload.
// Supports STUN, DTLS, RTP/RTCP (any packet type the ICE/DTLS stack may send).
IceTcpDemultiplexer::ExtractResult IceTcpDemultiplexer::ExtractRfc4571Message()
{
	constexpr size_t RFC4571_HEADER_SIZE = 2;

	if (_buffer->GetLength() < RFC4571_HEADER_SIZE)
	{
		return ExtractResult::NOT_ENOUGH_BUFFER;
	}

	const uint8_t *buf = static_cast<const uint8_t *>(_buffer->GetData());
	uint16_t payload_length = (static_cast<uint16_t>(buf[0]) << 8) | buf[1];

	// Guard against absurdly large (corrupt) frames
	if (payload_length > 65000)
	{
		return ExtractResult::FAILED;
	}

	const size_t total_length = RFC4571_HEADER_SIZE + payload_length;
	if (_buffer->GetLength() < total_length)
	{
		return ExtractResult::NOT_ENOUGH_BUFFER;
	}

	// The payload starts after the 2-byte length field.
	auto payload = _buffer->Subdata(RFC4571_HEADER_SIZE, payload_length);

	// Identify the inner packet type so the ICE engine can route it correctly.
	auto type = IcePacketIdentifier::FindPacketType(payload);
	auto packet = std::make_shared<IceTcpDemultiplexer::Packet>(type, payload);

	_packets.push(packet);
	_buffer = _buffer->Subdata(total_length);

	return ExtractResult::SUCCESS;
}
