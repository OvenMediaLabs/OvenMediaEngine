//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "stun_message.h"

#include <base/ovcrypto/ovcrypto.h>
#include <base/ovlibrary/ovlibrary.h>

#include "modules/ice/ice.h"
#include "modules/ice/stun/attributes/stun_fingerprint_attribute.h"
#include "stun_private.h"

static const uint32_t StunTypeMask = 0x0110;
static const size_t StunAttributeHeaderSize = 4;

// STUN Message Integrity HMAC length.
static const size_t StunMessageIntegritySize = 20;

StunMessage::StunMessage()
	: _parsed(false),

	  _type(0),
	  _message_length(0),
	  _magic_cookie(OV_STUN_MAGIC_COOKIE)
{
	::memset(_transaction_id, 0, sizeof(_transaction_id));
}

StunMessage::~StunMessage()
{
}

bool StunMessage::Parse(ov::ByteStream &stream)
{
	_parsed = true;

	// Fingerprint를 처리하는 과정에서 stream의 offset이 변경되므로, 별도의 stream 생성
	ov::ByteStream copied_stream(stream);

	// 헤더 길이 정보 분석 및 헤더를 먼저 읽음
	_parsed = _parsed && ParseHeader(stream);

	// STUN 패킷인지를 빠르게 판단하기 위해, finger print를 먼저 계산해봄 (CRC는 stream의 뒷 부분을 탐색하여 계산하기 때문에, 복사해놓음 stream을 사용함)
	//_parsed = _parsed && ValidateFingerprint(copied_stream);

	// 나머지 attribute들 파싱
	_parsed = _parsed && ParseAttributes(stream);

	return _parsed;
}

bool StunMessage::ParseHeader(ov::ByteStream &stream)
{
	logtt("Trying to check STUN header length...");

	size_t remained = stream.Remained();

	if ((static_cast<int>(remained) >= DefaultHeaderLength()) == false)
	{
		_last_error_code = LastErrorCode::NOT_ENOUGH_DATA;
		return false;
	}

	// The message length MUST contain the size, in bytes, of the message
	// not including the 20-byte STUN header. Since all STUN attributes are
	// padded to a multiple of 4 bytes, the last 2 bits of this field are
	// always zero. This provides another way to distinguish STUN packets
	// from packets of other protocols. (RFC 5389)

	// Deprecated by getroot (21.01.29)
	// When using this function to extract STUN packets from a TCP stream, the data may not be multiple of 4bytes.
	/*
	if((remained & 0b11) != 0)
	{
		// This is not a stun packet
		_last_error_code = LastErrorCode::INVALID_DATA;
		return false;
	}
	*/

	// RFC5389, section 6
	// All STUN messages MUST start with a 20-byte header followed by zero
	// or more Attributes.  The STUN header contains a STUN message type,
	// 		magic cookie, transaction ID, and message length.
	//
	// 0                   1                   2                   3
	// 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	// |0 0|     STUN Message Type     |         Message Length        |
	// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	// |                         Magic Cookie                          |
	// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	// |                                                               |
	// |                     Transaction ID (96 bits)                  |
	// |                                                               |
	// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	//
	// Figure 2: Format of STUN Message Header

	// The incoming data is in big endian, so we can not use memcpy(), and we have to assign them one by one

	uint16_t type = stream.ReadBE16();

	// Validate MSB
	if ((type & 0xC000) != 0)
	{
		// This is not a stun packet
		_last_error_code = LastErrorCode::INVALID_DATA;
		return false;
	}

	logtt("Trying to parse STUN header...");

	SetType(type);

	// Message Length
	_message_length = stream.ReadBE16();
	// Magic Cookie
	_magic_cookie = stream.ReadBE32();

	if (_magic_cookie != OV_STUN_MAGIC_COOKIE)
	{
		// According to RFC5389, magic cookie values are always fixed

		// If this STUN message is of the form shown in RFC3489, the magic cookie value may not match
		// If this happens, we need to implement the RFC3489 format
		logtw("Invalid magic cookie found: %08X", _magic_cookie);
		_last_error_code = LastErrorCode::INVALID_DATA;
		return false;
	}

	// Transaction ID
	if (stream.Read<>(_transaction_id, OV_COUNTOF(_transaction_id)) != OV_COUNTOF(_transaction_id))
	{
		logtw("Could not read transaction ID");
		_last_error_code = LastErrorCode::INVALID_DATA;
		return false;
	}

	if (stream.Remained() < _message_length)
	{
		logtt("Message is too short: %d (expected: %d)", stream.Remained(), _message_length);
		_last_error_code = LastErrorCode::NOT_ENOUGH_DATA;
		return false;
	}

	logtt("STUN header is parsed successfully");
	return true;
}

bool StunMessage::ParseAttributes(ov::ByteStream &stream)
{
	// Under normal circumstances, fingerprint attribute must be parsed.
	// OV_ASSERT2(_fingerprint_attribute != nullptr);

	// Since fingerprint attribute is calculated before other attributes, it is excluded.
	size_t minimum_length = StunAttribute::DefaultHeaderSize() +
							((_fingerprint_attribute != nullptr) ? _fingerprint_attribute->GetLength(true, false) : 0);

	// Parsing starts when the minimum data is found
	while (stream.Remained() >= minimum_length)
	{
		std::shared_ptr<StunAttribute> attribute = StunAttribute::CreateAttribute(this, stream);

		if (attribute == nullptr)
		{
			logtw("Could not parse attribute");
			return false;
		}

		_attributes.push_back(std::move(attribute));
	}

	return true;
}

std::shared_ptr<StunAttribute> StunMessage::ParseFingerprintAttribute(ov::ByteStream &stream)
{
	// Fingerprint attribute header + CRC (4B)
	int fingerprint_attribute_size = StunAttribute::DefaultHeaderSize() + sizeof(uint32_t);

	if (stream.IsRemained(DefaultHeaderLength() + fingerprint_attribute_size) == false)
	{
		return nullptr;
	}

	// There must be a fingerprint attribute at the fingerprint_attribute_size point from the end
	if (stream.SetOffset(stream.GetOffset() + stream.Remained() - fingerprint_attribute_size) == false)
	{
		return nullptr;
	}

	std::shared_ptr<StunAttribute> attribute = StunAttribute::CreateAttribute(this, stream);

	if ((attribute != nullptr) && (attribute->GetType() != StunAttributeType::Fingerprint))
	{
		logtw("Last attribute IS NOT FINGER-PRINT attribute(type: %d)", attribute->GetType());
		return nullptr;
	}

	return attribute;
}

bool StunMessage::WriteHeader(ov::ByteStream &stream)
{
	logtt("Trying to write STUN header...");

	bool result = true;

	// Message Type
	result = result && stream.WriteBE16(_type);
	// Message Length
	result = result && WriteMessageLength(stream);
	// Magic Cookie
	result = result && stream.WriteBE32(OV_STUN_MAGIC_COOKIE);
	// Transaction ID
	result = result && stream.Write<>(_transaction_id, OV_COUNTOF(_transaction_id));

	if (result)
	{
		logtt("STUN header is written");
	}
	else
	{
		logtw("Could not write STUN header");
	}

	return result;
}

bool StunMessage::WriteMessageLength(ov::ByteStream &stream)
{
	_message_length = 0;

	for (const auto &attribute : _attributes)
	{
		if (
			(attribute->GetType() == StunAttributeType::MessageIntegrity) ||
			(attribute->GetType() == StunAttributeType::Fingerprint))
		{
			continue;
		}

		int attribute_length = attribute->GetLength(true, true);
		_message_length += attribute_length;
	};

	stream.WriteBE16(_message_length);

	return true;
}

bool StunMessage::WriteAttributes(ov::ByteStream &stream)
{
	uint8_t padding[4] = {0};

	logtt("Trying to write attributes...");

	for (const auto &attribute : _attributes)
	{
		if (
			(attribute->GetType() == StunAttributeType::MessageIntegrity) ||
			(attribute->GetType() == StunAttributeType::Fingerprint))
		{
			continue;
		}

		if (attribute->Serialize(this, stream) == false)
		{
			return false;
		}

		ssize_t padding_count = 4 - ((attribute->GetLength(true, false)) % 4);

		if (padding_count < 4)
		{
			// zero-padding
			stream.Write<uint8_t>(padding, static_cast<size_t>(padding_count));
		}
	}

	return true;
}

bool StunMessage::WriteMessageIntegrityAttribute(ov::ByteStream &stream, const std::shared_ptr<const ov::Data> &integrity_key)
{
	logtt("Trying to write message integrity attribute...");

	std::shared_ptr<StunMessageIntegrityAttribute> attribute = std::make_unique<StunMessageIntegrityAttribute>();

	// RFC5389 - 15.4. MESSAGE-INTEGRITY
	// ...
	// The text used as input to HMAC is the STUN message,
	// including the header, up to and including the attribute preceding the
	// MESSAGE-INTEGRITY attribute.  With the exception of the FINGERPRINT
	// attribute, which appears after MESSAGE-INTEGRITY, agents MUST ignore
	// all other attributes that follow MESSAGE-INTEGRITY
	// ...

	// Hash를 계산할 땐, STUN 헤더의 length에 integrity attribute 길이가 포함되어 있어야 함
	auto buffer = (uint8_t *)(stream.GetData()->GetWritableData());
	auto length_field = (uint16_t *)(buffer + sizeof(uint16_t));
	int attribute_length = attribute->GetLength(true, true);

	_message_length += attribute_length;
	*length_field = ov::HostToNetwork16(_message_length);

	uint8_t hash[OV_STUN_HASH_LENGTH];

	bool result = true;

	logtt("Computing hmac: key: %s (current message length: %d)\n%s", integrity_key->ToHexString().CStr(), _message_length, stream.GetData()->Dump().CStr());

	result = result && ov::MessageDigest::ComputeHmac(ov::CryptoAlgorithm::Sha1,
													  integrity_key->GetData(), integrity_key->GetLength(),
													  stream.GetData()->GetData(), stream.GetData()->GetLength(),
													  hash, OV_STUN_HASH_LENGTH);
	result = result && attribute->SetHash(hash);
	result = result && attribute->Serialize(this, stream);

	if (result)
	{
		_integrity_attribute = std::move(attribute);
	}

	return result;
}

bool StunMessage::WriteFingerprintAttribute(ov::ByteStream &stream)
{
	logtt("Trying to write fingerprint attribute...");

	std::shared_ptr<StunFingerprintAttribute> attribute = std::make_unique<StunFingerprintAttribute>();

	// When calculating Crc, the length of the STUN header must include the fingerprint attribute length.
	auto buffer = stream.GetData()->GetWritableDataAs<uint8_t>();
	auto length_field = reinterpret_cast<uint16_t *>(buffer + sizeof(uint16_t));
	int attribute_length = attribute->GetLength(true, true);

	_message_length += attribute_length;
	*length_field = ov::HostToNetwork16(_message_length);

	logtt("Calculating CRC for STUN message:\n%s", stream.GetData()->Dump().CStr());

	uint32_t crc = ov::Crc32::Calculate(stream.GetData());

	bool result = true;

	result = result && attribute->SetValue(crc ^ OV_STUN_FINGERPRINT_XOR_VALUE);
	result = result && attribute->Serialize(this, stream);

	if (result)
	{
		_fingerprint_attribute = std::move(attribute);
	}

	return result;
}

bool StunMessage::CalculateFingerprint(ov::ByteStream &stream, ssize_t length, uint32_t *fingerprint) const
{
	uint32_t calculated = 0;
	const auto buffer = stream.Read<>();

	// Since the current position is stored in the buffer pointer above, the rest of the data is skipped so that it is not read.
	if (stream.Skip(static_cast<size_t>(length)) == false)
	{
		return false;
	}

	calculated = ov::Crc32::Calculate(buffer, length);

	// xoring
	calculated ^= OV_STUN_FINGERPRINT_XOR_VALUE;

	if (fingerprint != nullptr)
	{
		*fingerprint = calculated;
	}

	return true;
}

bool StunMessage::ValidateFingerprint(ov::ByteStream &stream)
{
	logtt("Trying to validate fingerprint...");

	// stream에서 fingerprint attribute를 얻어옴 (가장 마지막 attribute)
	ov::ByteStream fingerprint_stream(stream);
	_fingerprint_attribute = ParseFingerprintAttribute(fingerprint_stream);

	if (_fingerprint_attribute != nullptr)
	{
		// 성공적으로 fingerprint attribute를 얻어왔다면, CRC를 계산함

		// CRC 계산 범위:
		// sizeof(STUN header) + sum(sizeof(Attributes)) - sizeof(Fingerprint attribute)
		// 즉, stream에서 fingerprint attribute의 길이만큼 제외한 나머지 영역에 대해 CRC 계산
		uint32_t crc;
		ssize_t crc_length = stream.Remained() - (_fingerprint_attribute->GetLength(true, true));

#if STUN_LOG_DATA
		logtt("Trying to calculate crc for data:\n%s", stream.Dump(crc_length).CStr());
#else	// STUN_LOG_DATA
		logtt("Trying to calculate crc...");
#endif	// STUN_LOG_DATA

		if (CalculateFingerprint(stream, crc_length, &crc))
		{
			// stream 데이터에 대해 CRC 계산이 완료되었다면, fingerprint attribute의 CRC와 대조해봄
			uint32_t fingerprint = static_cast<const StunFingerprintAttribute *>(_fingerprint_attribute.get())->GetValue();

			OV_ASSERT(crc == fingerprint, "Fingerprint DOES NOT MATCHED: Calculated CRC: %08X, Fingerprint attribute CRC: %08X", crc, fingerprint);

			if (crc == fingerprint)
			{
				logtt("Calculated CRC: %08X, Fingerprint attribute CRC: %08X", crc, fingerprint);
			}
			else
			{
				logtw("Fingerprint DOES NOT MATCHED: Calculated CRC: %08X, Fingerprint attribute CRC: %08X", crc, fingerprint);
			}

			if (crc != fingerprint)
			{
				_last_error_code = LastErrorCode::INVALID_DATA;
			}
			else
			{
				_last_error_code = LastErrorCode::SUCCESS;
			}

			return (crc == fingerprint);
		}
	}
	else
	{
		logtw("Could not find fingerprint attribute");
	}

	_last_error_code = LastErrorCode::INVALID_DATA;
	return false;
}

StunMessage::LastErrorCode StunMessage::GetLastErrorCode() const
{
	return _last_error_code;
}

bool StunMessage::IsValid() const
{
	return _parsed && (_magic_cookie == OV_STUN_MAGIC_COOKIE);
}

bool StunMessage::CheckIntegrity(const ov::String &password) const
{
	// RFC 5389, section 15.4 참고
	return true;
}

bool StunMessage::GetUfrags(ov::String *first_ufrag, ov::String *second_ufrag) const
{
	const auto *user_name_attribute = GetAttribute<StunUserNameAttribute>(StunAttributeType::UserName);

	if (user_name_attribute == nullptr)
	{
		logtw("User name attribute not found");
		return false;
	}

	const ov::String &user_name = user_name_attribute->GetValue();

	std::vector<ov::String> tokens = user_name.Split(":");

	if (tokens.size() != 2)
	{
		// token은 반드시 2개여야 함
		logtw("Invalid user name: %s", user_name.CStr());
		return false;
	}

	if (first_ufrag != nullptr)
	{
		*first_ufrag = tokens[0];
	}

	if (second_ufrag != nullptr)
	{
		*second_ufrag = tokens[1];
	}

	return true;
}

ov::String StunMessage::ToString() const
{
	ov::String dump;

	dump.AppendFormat("========== STUN Message (%p) ==========\n", this);

	dump.AppendFormat("Parsing status: %s\n", _parsed ? "Parsed" : "Not parsed");
	dump.AppendFormat("Message Type: 0x%04X\n", _type);
	dump.AppendFormat("    Class: %s (0x%02X)\n", GetClassString(), GetClass());
	dump.AppendFormat("    Method: %s (0x%04X)\n", GetMethodString(), GetMethod());
	dump.AppendFormat("Message Length: %d (0x%04X)\n", _message_length, _message_length);
	dump.AppendFormat("Magic Cookie: 0x%08X\n", _magic_cookie);
	dump.AppendFormat("Transaction ID: %s\n", ov::ToHexString(&(_transaction_id[0]), OV_COUNTOF(_transaction_id)).CStr());
	dump.AppendFormat("Attributes: %d items", _attributes.size());

	for (const auto &attribute : _attributes)
	{
		dump.AppendFormat("\n    %s", attribute->ToString().CStr());
	};

	if (_integrity_attribute != nullptr)
	{
		dump.AppendFormat("\n    %s", _integrity_attribute->ToString().CStr());
	}

	if (_fingerprint_attribute != nullptr)
	{
		dump.AppendFormat("\n    %s", _fingerprint_attribute->ToString().CStr());
	}

	return dump;
}

uint16_t StunMessage::GetType() const
{
	return _type;
}

void StunMessage::SetType(uint16_t type)
{
	// In little endian, the type consist of these values:
	//
	// +--+--+--+--+--+--+--+--+--+--+--+--+--+--+
	// |13 12 11 10 09 08 07 06 05 04 03 02 01 00|
	// +--+--+--+--+--+--+--+--+--+--+--+--+--+--+
	// |M |M |M |M |M |C |M |M |M |C |M |M |M |M |
	// |11|10|9 |8 |7 |1 |6 |5 |4 |0 |3 |2 |1 |0 |
	// +--+--+--+--+--+--+--+--+--+--+--+--+--+--+
	//
	// Reference: https://tools.ietf.org/html/rfc5389#section-6

	// Message Class
	_type = type;
}

StunClass StunMessage::GetClass() const
{
	// RFC5389 - A class of 0b00 is a request, a class of 0b01 is an indication, a class of 0b10 is a success response, and a class of 0b11 is an error response.
	return (StunClass)((OV_GET_BIT(_type, 8) << 1) | (OV_GET_BIT(_type, 4) << 0));
}

const char *StunMessage::GetClassString() const
{
	switch (GetClass())
	{
		case StunClass::Request:
			return "Request";

		case StunClass::Indication:
			return "Indication";

		case StunClass::SuccessResponse:
			return "SuccessResponse";

		case StunClass::ErrorResponse:
			return "ErrorResponse";
	}

	return "(Unknown)";
}

void StunMessage::SetClass(StunClass clazz)
{
	auto class_value = static_cast<uint16_t>((OV_GET_BIT((uint16_t)clazz, 1) << 8) | (OV_GET_BIT((uint16_t)clazz, 0) << 4));

	// class 쪽 bit 초기화
	// STUN Message Type에서, M 부분만 남기기 위한 bit_mask = 0b11111011101111 (0x3EEF)
	_type = static_cast<uint16_t>((_type & 0b11111011101111) | class_value);
}

StunMethod StunMessage::GetMethod() const
{
	return (StunMethod)(
		// 13~09 번 째 bit 모음
		((_type & 0b0011111000000000) >> 2) |
		// 07~05 번 째 bit 모음
		((_type & 0b0000000011100000) >> 1) |
		// 03~00 번 째 bit 모음
		((_type & 0b0000000000001111) >> 0));
}

const char *StunMessage::GetMethodString() const
{
	switch (GetMethod())
	{
		case StunMethod::Binding:
			return "Binding";
		case StunMethod::Allocate:
			return "Allocate";
		case StunMethod::Refresh:
			return "Refresh";
		case StunMethod::Send:
			return "Send";
		case StunMethod::Data:
			return "Data";
		case StunMethod::CreatePermission:
			return "CreatePermission";
		case StunMethod::ChannelBind:
			return "ChannelBind";
	}

	return "(Unknown)";
}

void StunMessage::SetMethod(StunMethod method)
{
	//   BA9876543210 =>   DCBA9876543210
	// 0bXXXXXYYYZZZZ => 0bXXXXX0YYY0ZZZZ 로 변환해야 함
	uint16_t method_value =
		// BA987 => DCBA9
		(OV_GET_BITS(uint16_t, method, 7, 5) << 2) |
		// 654 => 765
		(OV_GET_BITS(uint16_t, method, 4, 3) << 1) |
		// 3210 => 3210
		OV_GET_BITS(uint16_t, method, 0, 4);

	// class 쪽 bit 초기화
	// STUN Message Type에서, C 부분만 남기기 위한 bit_mask = 0b00000100010000 (0x0110)
	_type = (_type & 0b00000100010000) | method_value;
}

uint16_t StunMessage::GetMessageLength() const
{
	return _message_length;
}

uint32_t StunMessage::GetMagicCookie() const
{
	return _magic_cookie;
}

// 길이: OV_STUN_TRANSACTION_ID_LENGTH
const uint8_t *StunMessage::GetTransactionId() const
{
	return _transaction_id;
}

void StunMessage::SetTransactionId(const uint8_t transaction_id[OV_STUN_TRANSACTION_ID_LENGTH])
{
	::memcpy(_transaction_id, transaction_id, sizeof(_transaction_id));
}

bool StunMessage::AddAttribute(std::shared_ptr<StunAttribute> attribute)
{
	if (_fingerprint_attribute != nullptr)
	{
		OV_ASSERT(false, "Could not add attribute to serialized message");
		return false;
	}

	if (attribute->GetType() == StunAttributeType::Fingerprint)
	{
		OV_ASSERT(false, "Fingerprint attribute will generate automatically");
		return false;
	}

	_attributes.push_back(std::move(attribute));

	return true;
}

// Generate only FINGERPRINT attribute
std::shared_ptr<ov::Data> StunMessage::Serialize()
{
	std::shared_ptr<ov::Data> data = std::make_shared<ov::Data>(65535);
	ov::ByteStream stream(data.get());

	bool result = true;

	result = result && WriteHeader(stream);
	result = result && WriteAttributes(stream);
	result = result && WriteFingerprintAttribute(stream);

	return result ? data : nullptr;
}

std::shared_ptr<ov::Data> StunMessage::Serialize(const std::shared_ptr<const ov::Data> &integrity_key)
{
	std::shared_ptr<ov::Data> data = std::make_shared<ov::Data>(65535);
	ov::ByteStream stream(data.get());

	bool result = true;

	result = result && WriteHeader(stream);
	result = result && WriteAttributes(stream);
	result = result && WriteMessageIntegrityAttribute(stream, integrity_key);
	result = result && WriteFingerprintAttribute(stream);

	return result ? data : nullptr;
}
