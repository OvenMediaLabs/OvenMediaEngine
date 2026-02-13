//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "stun_address_attribute_format.h"

#include <arpa/inet.h>

#include "../../stun_private.h"

StunAddressAttributeFormat::StunAddressAttributeFormat(StunAttributeType type, int length)
	: StunAttribute(type, length)
{
}

bool StunAddressAttributeFormat::Parse(const StunMessage *stun_message, ov::ByteStream &stream)
{
	//  0                   1                   2                   3
	//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	// |0 0 0 0 0 0 0 0|    Family     |           Port                |
	// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	// |                                                               |
	// |                 Address (32 bits or 128 bits)                 |
	// |                                                               |
	// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	//
	//          Figure 5: Format of MAPPED-ADDRESS Attribute

	// Padding (1B) + Family (1B) + Port (2B)
	if (stream.IsRemained(sizeof(uint8_t) + sizeof(StunAddressFamily) + sizeof(uint16_t)) == false)
	{
		return false;
	}

	// Reserved
	stream.Skip<uint8_t>();

	// Family
	const auto family = static_cast<StunAddressFamily>(stream.Read8());

	if ((family != StunAddressFamily::IPv4) && (family != StunAddressFamily::IPv6))
	{
		logtw("Invalid family: %d", family);
		return false;
	}

	// Port
	const auto port = stream.ReadBE16();

	// Address
	const size_t address_length = (family == StunAddressFamily::IPv4) ? sizeof(in_addr) : sizeof(in6_addr);

	if (stream.IsRemained(address_length) == false)
	{
		logtw("Could not read address. (Family: %d, Data is not enough: %d)", family, stream.Remained());
		return false;
	}

	switch (family)
	{
		case StunAddressFamily::IPv4: {
			_address.SetFamily(ov::SocketFamily::Inet);
			if (stream.Read<in_addr>(_address) != 1)
			{
				logtw("Could not read IPv4 address");
				return false;
			}

			break;
		}

		case StunAddressFamily::IPv6:
			_address.SetFamily(ov::SocketFamily::Inet6);
			if (stream.Read<in6_addr>(_address) != 1)
			{
				logtw("Could not read IPv6 address");
				return false;
			}

			break;

		default:
			// This code never be executed, because it is checked above
			OV_ASSERT2(false);
			logtw("Invalid family: %d", family);
			return false;
	}

	_address.SetPort(port);
	_address.UpdateFromStorage();
	_address.SetHostname(_address.GetIpAddress());

	return true;
}

StunAddressFamily StunAddressAttributeFormat::GetFamily() const
{
	switch (_address.GetFamily())
	{
		case ov::SocketFamily::Inet:
			return StunAddressFamily::IPv4;

		case ov::SocketFamily::Inet6:
			return StunAddressFamily::IPv6;

		default:
			return StunAddressFamily::Unknown;
	}
}

uint16_t StunAddressAttributeFormat::GetPort() const
{
	return _address.Port();
}

const ov::SocketAddress &StunAddressAttributeFormat::GetAddress() const
{
	return _address;
}

bool StunAddressAttributeFormat::SetParameters(const ov::SocketAddress &address)
{
	_address = address;

	// Padding (1B) + Family (1B) + Port (2B) + Address (4B or 16B)
	_length = sizeof(uint8_t) + sizeof(StunAddressFamily) + sizeof(uint16_t) + _address.GetInAddrLength();

	return true;
}

bool StunAddressAttributeFormat::Serialize(const StunMessage *stun_message, ov::ByteStream &stream) const noexcept
{
	if (_address.IsValid() == false)
	{
		logte("Could not serialize MAPPED-ADDRESS: Invalid address: %s", _address.ToString().CStr());
		return false;
	}

	return StunAttribute::Serialize(stun_message, stream) &&
		   stream.Write8(0x00) &&
		   stream.Write8(static_cast<uint8_t>(GetFamily())) &&
		   stream.WriteBE16(GetPort()) &&
		   stream.Write(_address.ToInAddr(), _address.GetInAddrLength());
}

ov::String StunAddressAttributeFormat::ToString(const char *class_name) const
{
	return StunAttribute::ToString(class_name, ov::String::FormatString(", address: %s", _address.ToString().CStr()).CStr());
}

ov::String StunAddressAttributeFormat::ToString() const
{
	return ToString(StringFromType(GetType()));
}