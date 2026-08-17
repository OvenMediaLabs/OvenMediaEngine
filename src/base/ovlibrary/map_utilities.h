//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <functional>
#include <optional>

namespace ov
{
	// A utilities for std::map
	namespace maputils
	{
		template <
			typename Tcontainer,
			typename Treduce = std::function<bool(Tcontainer &accumulator, const typename Tcontainer::key_type &key, const typename Tcontainer::mapped_type &value)>>
		Tcontainer Reduce(const Tcontainer &values, Treduce reducer)
		{
			Tcontainer accumulator;

			for (const auto &[key, value] : values)
			{
				if (reducer(accumulator, key, value) == false)
				{
					break;
				}
			}

			return accumulator;
		}

		template <
			typename Tcontainer,
			typename Tfilter = std::function<bool(const typename Tcontainer::key_type &key, const typename Tcontainer::mapped_type &value)>>
		Tcontainer Filter(const Tcontainer &values, Tfilter filter)
		{
			Tcontainer result;

			for (const auto &[key, value] : values)
			{
				if (filter(key, value))
				{
					result[key] = value;
				}
			}

			return result;
		}

		template <
			typename Tcontainer,
			typename Tfilter = std::function<bool(const typename Tcontainer::key_type &key, const typename Tcontainer::mapped_type &value)>>
		std::optional<typename Tcontainer::value_type> First(const Tcontainer &values, Tfilter filter = nullptr)
		{
			for (const auto &pair : values)
			{
				if ((filter == nullptr) || filter(pair.first, pair.second))
				{
					return pair;
				}
			}

			return std::nullopt;
		}

		template <
			typename Tcontainer,
			typename Tfilter = std::function<bool(const typename Tcontainer::key_type &key, const typename Tcontainer::mapped_type &value)>>
		std::optional<typename Tcontainer::mapped_type> FirstValue(const Tcontainer &values, Tfilter filter = nullptr)
		{
			auto value = First(values, filter);
			return value.has_value() ? std::optional(value.value().second) : std::nullopt;
		}

		template <
			typename Tcontainer,
			typename Tfilter = std::function<bool(const typename Tcontainer::key_type &key, const typename Tcontainer::mapped_type &value)>>
		std::optional<typename Tcontainer::value_type> Last(const Tcontainer &values, Tfilter filter = nullptr)
		{
			for (auto it = values.rbegin(); it != values.rend(); ++it)
			{
				const auto &[key, value] = *it;

				if ((filter == nullptr) || filter(key, value))
				{
					return {key, value};
				}
			}

			return std::nullopt;
		}

		template <
			typename Tcontainer,
			typename Tfilter = std::function<bool(const typename Tcontainer::key_type &key, const typename Tcontainer::mapped_type &value)>>
		std::optional<typename Tcontainer::mapped_type> LastValue(const Tcontainer &values, Tfilter filter = nullptr)
		{
			auto value = Last(values, filter);
			return value.has_value() ? std::optional(value.value().second) : std::nullopt;
		}
	}  // namespace map
}  // namespace ov
