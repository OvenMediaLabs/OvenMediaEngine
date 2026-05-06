//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "stun_datastructure.h"

#include <algorithm>

#include <base/ovlibrary/ovlibrary.h>
#include <base/ovsocket/ovsocket.h>

#include "modules/ice/stun/attributes/stun_attribute.h"
#include "modules/ice/stun/attributes/stun_error_code_attribute.h"

class StunMessage
{
public:
	enum LastErrorCode : uint8_t
	{
		NOT_USED = 0,
		SUCCESS,
		INVALID_DATA,
		NOT_ENOUGH_DATA
	};

	StunMessage();
	virtual ~StunMessage();

	bool Parse(ov::ByteStream &stream);
	bool ParseHeader(ov::ByteStream &stream);

	LastErrorCode GetLastErrorCode() const;
	bool IsValid() const;
	bool CheckIntegrity(const ov::String &password) const;
	bool GetUfrags(ov::String *local_ufrag, ov::String *remote_ufrag) const;

	static constexpr int DefaultHeaderLength()
	{
		// All STUN messages MUST start with a 20-byte header followed by zero or more Attributes

		// Type (2B) + Message Length (2B) + Magic Cookie (4B) + Transaction ID (12B) = 20B
		return sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint32_t) + (sizeof(uint8_t) * 12);
	}


	ov::String ToString() const;

	bool SetHeader(StunClass clazz, StunMethod method, const uint8_t transaction_id[OV_STUN_TRANSACTION_ID_LENGTH])
	{
		SetClass(clazz);
		SetMethod(method);
		SetTransactionId(transaction_id);
		return true;
	}

	bool SetErrorCodeAttribute(StunErrorCode error_code, ov::String extra_error_message = "")
	{
		auto attribute = std::make_unique<StunErrorCodeAttribute>();
		attribute->SetError(error_code, extra_error_message);
		return AddAttribute(std::move(attribute));
	}

	uint16_t GetType() const;
	// SetType() = SetClass() + SetMethod()
	void SetType(uint16_t type);
	StunClass GetClass() const;
	const char *GetClassString() const;
	void SetClass(StunClass clazz);
	StunMethod GetMethod() const;
	const char *GetMethodString() const;
	void SetMethod(StunMethod method);

	uint16_t GetMessageLength() const;
	uint32_t GetMagicCookie() const;

	const uint8_t *GetTransactionId() const;
	void SetTransactionId(const uint8_t transaction_id[OV_STUN_TRANSACTION_ID_LENGTH]);

	template<class T>
	const T *GetAttribute(StunAttributeType type) const
	{
		for(const auto &attribute : _attributes)
		{
			if(attribute->GetType() == type)
			{
				return static_cast<const T *>(attribute.get());
			}
		}

		return nullptr;
	}

	bool AddAttribute(std::shared_ptr<StunAttribute> attribute);

	// Generate MESSAGE-INTEGRITY and FINGERPRINT attributes
	std::shared_ptr<ov::Data> Serialize(const std::shared_ptr<const ov::Data> &integrity_key);
	// Generate only FINGERPRINT attribute
	std::shared_ptr<ov::Data> Serialize();

protected:
	
	bool ParseAttributes(ov::ByteStream &stream, off_t message_start_offset = 0);
	std::shared_ptr<StunAttribute> ParseFingerprintAttribute(ov::ByteStream &stream);

	bool WriteHeader(ov::ByteStream &stream);
	bool WriteMessageLength(ov::ByteStream &stream);
	bool WriteAttributes(ov::ByteStream &stream);
	bool WriteMessageIntegrityAttribute(ov::ByteStream &stream, const std::shared_ptr<const ov::Data> &integrity_key);
	bool WriteFingerprintAttribute(ov::ByteStream &stream);

	// spec: RFC 5389, section 7.3, 15.5
	bool CalculateFingerprint(ov::ByteStream &stream, ssize_t length, uint32_t *fingerprint) const;
	bool ValidateFingerprint(ov::ByteStream &stream);

	bool _parsed;

	uint16_t _type;
	uint16_t _message_length;
	uint32_t _magic_cookie;
	uint8_t _transaction_id[OV_STUN_TRANSACTION_ID_LENGTH];

	std::vector<std::shared_ptr<StunAttribute>> _attributes;

	std::shared_ptr<StunAttribute> _integrity_attribute;
	std::shared_ptr<StunAttribute> _fingerprint_attribute;

	// Raw STUN message bytes captured during Parse() for MESSAGE-INTEGRITY verification.
	std::shared_ptr<ov::Data> _raw_message;
	// Offset of MESSAGE-INTEGRITY attribute (from the beginning of the STUN header) within _raw_message; -1 if not present.
	off_t _integrity_attribute_offset = -1;

	// Last error codes
	LastErrorCode	_last_error_code = NOT_USED;
};
