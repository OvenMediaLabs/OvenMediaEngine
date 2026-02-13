//
// Created by getroot on 21. 01. 28.
//
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include "ice_packet_identifier.h"

//#define FIXED_STUN_HEADER_SIZE	20
//#define FIXED_TURN_CHANNEL_HEADER_SIZE	4
#define MINIMUM_PACKET_HEADER_SIZE	4

// It only demultiplexes the stream input to ICE/TCP. 
// Use identifier for packets that are input to UDP.

class IceTcpDemultiplexer
{
public:

	IceTcpDemultiplexer()
	{
		_buffer = std::make_shared<ov::Data>(65535);
	}

	// In the case of a turn channel data message, it parses the header and stores the application data.
	class Packet
	{
	public:
		Packet(IcePacketIdentifier::PacketType type, const std::shared_ptr<ov::Data> &data)
		{
			_type = type;
			_data = data;
		}

		IcePacketIdentifier::PacketType GetPacketType()
		{
			return _type;
		}

		std::shared_ptr<ov::Data> GetData()
		{
			return _data;
		}

	private:
		IcePacketIdentifier::PacketType _type = IcePacketIdentifier::PacketType::UNKNOWN;
		[[maybe_unused]] uint16_t _channel_number = 0;	// Only use if packet is from channel data message
		std::shared_ptr<ov::Data>	_data = nullptr;
	};

	bool AppendData(const void *data, size_t length);
	bool AppendData(const std::shared_ptr<const ov::Data> &data);

	bool IsAvailablePacket();
	std::shared_ptr<IceTcpDemultiplexer::Packet> PopPacket();

private:
	bool ParseData();

	enum class ExtractResult : int8_t
	{
		SUCCESS = 1,
		NOT_ENOUGH_BUFFER = 0,
		FAILED = -1
	};
	// 1 : success
	// 0 : not enough memory
	// -1 : error
	ExtractResult ExtractStunMessage();
	ExtractResult ExtractChannelMessage();

	std::shared_ptr<ov::Data> _buffer;
	std::queue<std::shared_ptr<IceTcpDemultiplexer::Packet>> _packets;
};