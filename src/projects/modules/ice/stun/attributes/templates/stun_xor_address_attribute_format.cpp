//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "stun_xor_address_attribute_format.h"

#include "../../stun_message.h"
#include "../../stun_private.h"

StunXorAddressAttributeFormat::StunXorAddressAttributeFormat(StunAttributeType type, int length)
	: StunAddressAttributeFormat(type, length)
{
}

// In-place XORing
bool DoXOROnAddress(const size_t length, const uint8_t transaction_id[OV_STUN_TRANSACTION_ID_LENGTH], uint8_t *address)
{
	// XOR the IP address with OV_STUN_MAGIC_COOKIE and transaction id
	//
	// Example:
	//   - IP address: 1234:5678:90ab:cdef:1234:5678:90ab:cdef
	//             ==> 1234:5678:90ab:cdef:1234:5678:90ab:cdef
	//               0x1234 5678 90ab cdef 1234 5678 90ab cdef
	//   - Magic cookie: 0x2112A442
	//   - Transaction ID: "0123456789AB"
	//                   => 0 1 2 3 4 5 6 7 8 9 A B
	//                    0x303132333435363738394142
	//
	//  => M/C + TX-ID = 0x2112A442303132333435363738394142
	//                     ~~~~~~~~........................
	//      ~: Magic cookie
	//      .: Transaction ID
	//
	// Operation:
	//   IP Address:  1234567890abcdef1234567890abcdef
	//   MC + TX-ID : 2112a442303132333435363738394142
	//                ================================
	//                3326f23aa09affdc2601604fa8928cad
	//
	if (length != (sizeof(OV_STUN_MAGIC_COOKIE) + OV_STUN_TRANSACTION_ID_LENGTH))
	{
		logtc("Could not XOR - length is mismatched: length: %zu != cookie + TX ID: %zu",
			  length, (sizeof(OV_STUN_MAGIC_COOKIE) + OV_STUN_TRANSACTION_ID_LENGTH));
		OV_ASSERT2(false);

		return false;
	}

	uint8_t second_operand[length];
	const auto cookie = ov::HostToNetwork32(OV_STUN_MAGIC_COOKIE);
	::memcpy(&second_operand[0], &cookie, sizeof(cookie));
	::memcpy(&second_operand[static_cast<off_t>(sizeof(cookie))], transaction_id, OV_STUN_TRANSACTION_ID_LENGTH);

	for (off_t index = 0; index < static_cast<off_t>(length); index++)
	{
		address[index] ^= second_operand[index];
	}

	return true;
}

bool StunXorAddressAttributeFormat::Parse(const StunMessage *stun_message, ov::ByteStream &stream)
{
	if (StunAddressAttributeFormat::Parse(stun_message, stream))
	{
		//  0                   1                   2                   3
		//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		// |x x x x x x x x|    Family     |         X-Port                |
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		// |                X-Address (Variable)
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		//
		//         Figure 6: Format of XOR-MAPPED-ADDRESS Attribute

		// X-Port is computed by taking the mapped port in host byte order,
		// XOR'ing it with the most significant 16 bits of the magic cookie, and then the converting the result to network byte order.
		_address.SetPort(static_cast<uint16_t>(_address.Port() ^ (OV_STUN_MAGIC_COOKIE >> 16)));

		switch (_address.GetFamily())
		{
			case ov::SocketFamily::Inet:
				_address.ToIn4Addr()->s_addr ^= ov::HostToNetwork32(OV_STUN_MAGIC_COOKIE);
				break;

			case ov::SocketFamily::Inet6: {
				// If the IP address family is IPv6,
				// X-Address is computed by taking the mapped IP address in host byte order,
				// XOR'ing it with the concatenation of the magic cookie and the 96-bit transaction ID,
				// and converting the result to network byte order
				if (DoXOROnAddress(_address.GetInAddrLength(), stun_message->GetTransactionId(), _address.ToIn6Addr()->s6_addr))
				{
					break;
				}

				return false;
			}

			default:
				logtw("Unknown family: %d", _address.GetFamily());
				_length = 0;
				return false;
		}

		_address.UpdateFromStorage();
		_address.SetHostname(_address.GetIpAddress());
	}

	return true;
}

bool StunXorAddressAttributeFormat::SerializeXoredAddress(const StunMessage *stun_message, ov::ByteStream &stream) const noexcept
{
	switch (_address.GetFamily())
	{
		case ov::SocketFamily::Inet: {
			auto addr = (_address.ToIn4Addr())->s_addr ^ ov::HostToNetwork32(OV_STUN_MAGIC_COOKIE);
			return stream.Write(static_cast<void *>(&addr), _address.GetInAddrLength());
		}

		case ov::SocketFamily::Inet6: {
			const auto length = _address.GetInAddrLength();
			uint8_t addr[length];
			::memcpy(addr, (_address.ToIn6Addr())->s6_addr, length);

			if (DoXOROnAddress(length, stun_message->GetTransactionId(), addr))
			{
				return stream.Write(addr, length);
			}

			break;
		}

		default:
			break;
	}

	return false;
}

bool StunXorAddressAttributeFormat::Serialize(const StunMessage *stun_message, ov::ByteStream &stream) const noexcept
{
	if (_address.IsValid() == false)
	{
		logte("Could not serialize XOR-MAPPED-ADDRESS: Invalid address: %s", _address.ToString().CStr());
		return false;
	}

	// Serialize common STUN attributes
	// (DO NOT call StunAddressAttributeFormat::Serialize() - To prevent serialize the address without XORing)
	return StunAttribute::Serialize(stun_message, stream) &&
		   stream.Write8(0x00) &&
		   stream.Write8(static_cast<uint8_t>(GetFamily())) &&
		   stream.WriteBE16(static_cast<uint16_t>(_address.Port() ^ (OV_STUN_MAGIC_COOKIE >> 16))) &&
		   SerializeXoredAddress(stun_message, stream);
}

ov::String StunXorAddressAttributeFormat::ToString() const
{
	return StunAddressAttributeFormat::ToString(StringFromType(GetType()));
}