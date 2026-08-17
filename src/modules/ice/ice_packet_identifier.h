//
// Created by getroot on 21. 01. 28.
//
#pragma once

#include <base/ovlibrary/ovlibrary.h>

class IcePacketIdentifier
{
public:
	// https://www.rfc-editor.org/rfc/rfc7983.html
	enum class PacketType
	{
		STUN = 0,
		ZRTP,
		DTLS,
		TURN_CHANNEL_DATA,
		RTP_RTCP,
		UNKNOWN
	};

	static ov::String GetPacketTypeString(PacketType type)
	{
		switch(type)
		{
			case PacketType::STUN:
				return "STUN";
			case PacketType::ZRTP:
				return "ZRTP";
			case PacketType::DTLS:
				return "DTLS";
			case PacketType::TURN_CHANNEL_DATA:
				return "TURN Channel Data";
			case PacketType::RTP_RTCP:
				return "RTP/RTCP";
			case PacketType::UNKNOWN:
				return "Unknown";
			default:
				return "Unknown";
		}
	}

	static PacketType FindPacketType(const std::shared_ptr<const ov::Data> &data)
	{
		return IcePacketIdentifier::FindPacketType(*data);
	}

	static PacketType FindPacketType(const ov::Data &data)
	{
		// Classification
		if(data.GetLength() < 1)
		{
			// Not enough data
			return PacketType::UNKNOWN;
		}

		/* https://www.rfc-editor.org/rfc/rfc7983.html
					+----------------+
					|        [0..3] -+--> forward to STUN
					|                |
					|      [16..19] -+--> forward to ZRTP
					|                |
		packet -->  |      [20..63] -+--> forward to DTLS
					|                |
					|      [64..79] -+--> forward to TURN Channel
					|                |
					|    [128..191] -+--> forward to RTP/RTCP
					+----------------+
		*/

		// 

		auto first_byte = data.GetDataAs<uint8_t>()[0];

		if(first_byte >= 0 && first_byte <= 3)
		{
			// STUN
			return PacketType::STUN;
		}
		else if(first_byte >= 16 && first_byte <= 19)
		{
			// ZRTP
			return PacketType::ZRTP;
		}
		else if(first_byte >= 20 && first_byte <= 63)
		{
			// DTLS
			return PacketType::DTLS;
		}
		else if(first_byte >= 64 && first_byte <= 79)
		{
			// TURN Channel
			return PacketType::TURN_CHANNEL_DATA;
		}
		else if(first_byte >= 128 && first_byte <= 191)
		{
			// RTP/RTCP
			return PacketType::RTP_RTCP;
		}

		return PacketType::UNKNOWN;
	}
};