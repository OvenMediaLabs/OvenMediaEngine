//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#pragma once

namespace ov
{
	// Wraps a value so that it converts neither to nor from another `StrongType` or a plain value implicitly.
	// `Ttag` only tells two types apart, so it can be a struct that is declared and never defined:
	//
	// ```
	//   using socket_id_t = ov::StrongType<uint64_t, struct SocketIdTag>;
	// ```
	//
	// It holds nothing but the value, so it compiles to the same code as the value itself.
	template <typename Tvalue, typename Ttag>
	class StrongType
	{
	public:
		constexpr StrongType() = default;

		constexpr explicit StrongType(Tvalue value)
			: _value(value)
		{
		}

		constexpr Tvalue GetValue() const
		{
			return _value;
		}

		constexpr bool operator==(const StrongType &other) const
		{
			return _value == other._value;
		}

		constexpr bool operator!=(const StrongType &other) const
		{
			return _value != other._value;
		}

		constexpr bool operator<(const StrongType &other) const
		{
			return _value < other._value;
		}

	private:
		Tvalue _value{};
	};
}  // namespace ov
