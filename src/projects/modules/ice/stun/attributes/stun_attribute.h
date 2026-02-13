//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "modules/ice/stun/stun_datastructure.h"

#include <vector>

#include <base/ovlibrary/ovlibrary.h>
#include <base/ovsocket/ovsocket.h>

class StunMessage;

class StunAttribute
{
public:
	// 헤더 상에 명시된 type 및 length
	explicit StunAttribute(StunAttributeType type, uint16_t type_number, size_t length);
	// type과 type_number가 같을 경우 편리하게 이 생성자로 호출
	explicit StunAttribute(StunAttributeType type, size_t length);
	StunAttribute(const StunAttribute &type);
	StunAttribute(StunAttribute &&type) noexcept;
	virtual ~StunAttribute();

	static constexpr size_t DefaultHeaderSize()
	{
		// Attribute + Length 정보의 길이
		return sizeof(uint16_t) + sizeof(uint16_t);
	}

	static std::shared_ptr<StunAttribute> CreateAttribute(const StunMessage *stun_message, ov::ByteStream &stream);
	static std::shared_ptr<StunAttribute> CreateAttribute(const StunMessage *stun_message, StunAttributeType type, int length);

	virtual bool Parse(const StunMessage *stun_message, ov::ByteStream &stream) = 0;

	// 알려지지 않은 타입일 경우, StunAttributeType::Unknown로 반환되며, 구체적인 타입 번호는 GetTypeNumber()로 알 수 있음
	StunAttributeType GetType() const noexcept;
	uint16_t GetTypeNumber() const noexcept;

	size_t GetLength(bool include_header = false, bool padding = true) const noexcept;

	virtual bool Serialize(const StunMessage *stun_message, ov::ByteStream &stream) const noexcept;

	static const char *StringFromType(StunAttributeType type) noexcept;
	const char *StringFromType() const noexcept;

	static const char *StringFromErrorCode(StunErrorCode code) noexcept;

	virtual ov::String ToString() const;

protected:
	ov::String ToString(const char *class_name, const char *suffix) const;

	StunAttributeType _type;
	uint16_t _type_number;

	// 길이가 고정인 것들은 생성될 때부터 길이가 0보다 큼
	size_t _length = 0;
};
