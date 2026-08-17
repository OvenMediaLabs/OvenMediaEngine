//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <uuid/uuid.h>

#include "./ovlibrary.h"
#include "./string.h"

namespace ov
{
	class UUID
	{
	public:
		UUID() noexcept
		{
			::uuid_generate(_uuid);
			UpdateHash();
		}

		UUID(const UUID &uuid) noexcept
		{
			::uuid_copy(_uuid, uuid._uuid);
			UpdateHash();
		}

		UUID(const uuid_t &uuid) noexcept
		{
			::uuid_copy(_uuid, uuid);
			UpdateHash();
		}

		static std::optional<UUID> FromString(const char *uuid_str)
		{
			if (uuid_str == nullptr)
			{
				return std::nullopt;
			}

			uuid_t uuid;

			if (::uuid_parse(uuid_str, uuid) != 0)
			{
				return std::nullopt;
			}

			return uuid;
		}

		UUID &operator=(const UUID &other) noexcept
		{
			if (this != &other)
			{
				::uuid_copy(_uuid, other._uuid);
			}
			return *this;
		}

		bool operator==(const UUID &other) const noexcept
		{
			return ::uuid_compare(_uuid, other._uuid) == 0;
		}

		bool operator!=(const UUID &other) const noexcept
		{
			return ::uuid_compare(_uuid, other._uuid) != 0;
		}

		bool operator<(const UUID &other) const noexcept
		{
			return ::uuid_compare(_uuid, other._uuid) < 0;
		}

		String ToString() const
		{
			std::call_once(_once_flag, [this]() {
				::uuid_unparse(_uuid, _uuid_string);
			});

			return _uuid_string;
		}

		size_t Hash() const
		{
			return _hash;
		}

	private:
		void UpdateHash()
		{
			_hash = 0;

			for (size_t i = 0; i < sizeof(uuid_t); ++i)
			{
				_hash = (_hash * 31) + _uuid[i];
			}
		}

	private:
		uuid_t _uuid;
		mutable size_t _hash = 0;

		mutable char _uuid_string[UUID_STR_LEN]{};
		mutable std::once_flag _once_flag;

		friend std::hash<UUID>;
	};
}  // namespace ov

// Specialization for std::hash (for unordered_set, unordered_map, etc.)
namespace std
{
	template <>
	struct hash<ov::UUID>
	{
		size_t operator()(const ov::UUID &uuid) const
		{
			return uuid.Hash();
		}
	};
}  // namespace std
