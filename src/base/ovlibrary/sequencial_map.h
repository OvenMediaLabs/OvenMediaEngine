//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <unordered_map>
#include <vector>
#include <utility>

#include "./assert.h"

namespace ov
{
	// This is a map that maintains the order in which `PushBack()` or `EmplaceBack()` was called
	template <typename Tkey, typename Tvalue>
	class SequencialMap
	{
	private:
		using KeyRefList = std::vector<std::reference_wrapper<const Tkey>>;

	public:
		using SizeType = typename KeyRefList::size_type;

		template <typename Titerator, typename Tvalue_map>
		class IteratorBase
		{
		public:
			IteratorBase(
				Titerator current,
				Tvalue_map &value_map)
				: _current(current),
				  _value_map(value_map)
			{
			}

			auto &operator*()
			{
				return *(_value_map.find(*_current));
			}

			auto operator->()
			{
				return _value_map.find(*_current).operator->();
			}

			IteratorBase<Titerator, Tvalue_map> &operator++()
			{
				++_current;
				return *this;
			}

			bool operator!=(const IteratorBase<Titerator, Tvalue_map> &other) const
			{
				return _current != other._current;
			}

		private:
			Titerator _current;

			Tvalue_map &_value_map;
		};

		using Iterator = IteratorBase<typename KeyRefList::iterator, std::unordered_map<Tkey, Tvalue>>;
		using ConstIterator = IteratorBase<typename KeyRefList::const_iterator, const std::unordered_map<Tkey, Tvalue>>;

	public:
		void PushBack(const Tkey &key, const Tvalue &value)
		{
			auto it = _value_map.find(key);

			if (it == _value_map.end())
			{
				_value_map[key] = value;
				it = _value_map.find(key);
				_key_ref_list.emplace_back(std::ref(it->first));
			}
			else
			{
				// Update
				it->second = value;
			}
		}

		template <typename... Targs>
		std::pair<Iterator, bool> EmplaceBack(Tkey &&key, Targs &&...args)
		{
			auto it = _value_map.find(key);
			bool inserted = false;

			if (it == _value_map.end())
			{
				auto new_it = _value_map.emplace(
					std::piecewise_construct,
					std::forward_as_tuple(std::forward<Tkey>(key)),
					std::forward_as_tuple(std::forward<Targs>(args)...));
				it = new_it.first;
				_key_ref_list.emplace_back(std::ref(it->first));

				inserted = true;
			}
			else
			{
				// Update value if key exists
				it->second = Tvalue(std::forward<Targs>(args)...);
			}

			return std::make_pair(Iterator(_key_ref_list.begin(), _value_map), inserted);
		}

		// Throws std::out_of_range if key does not exist
		const Tvalue &At(const Tkey &key) const
		{
			auto it = _value_map.find(key);

			if (it != _value_map.end())
			{
				return it->second;
			}

			throw std::out_of_range("Key does not exist");
		}

		// Throws std::out_of_range if key does not exist
		Tvalue &At(const Tkey &key)
		{
			return const_cast<Tvalue &>(std::as_const(*this).At(key));
		}

		SizeType Size() const
		{
			return _key_ref_list.size();
		}

		bool Empty() const
		{
			return _key_ref_list.empty();
		}

		void Clear()
		{
			_key_ref_list.clear();
			_value_map.clear();
		}

		bool Contains(const Tkey &key) const
		{
			return _value_map.find(key) != _value_map.end();
		}

		void Erase(const Tkey &key)
		{
			auto it = _value_map.find(key);

			if (it == _value_map.end())
			{
				return;
			}

			auto key_it = std::find(_key_ref_list.begin(), _key_ref_list.end(), key);
			if (key_it != _key_ref_list.end())
			{
				_key_ref_list.erase(key_it);
			}
			else
			{
				// If the key exists in _value_map, it must also exist in _key_list
				OV_ASSERT(false, "Key does not exist in _key_list");
			}

			_value_map.erase(it);
		}

		Tvalue &operator[](const Tkey &key)
		{
			auto it = _value_map.find(key);

			if (it != _value_map.end())
			{
				return it->second;
			}

			return
				// EmplaceBack() returns std::pair<Iterator, bool>,
				EmplaceBack(key).first->
				// and Iterator.second is the value
				second;
		}

		Tvalue &operator[](Tkey &&key)
		{
			auto it = _value_map.find(key);

			if (it != _value_map.end())
			{
				return it->second;
			}

			return
				// EmplaceBack() returns std::pair<Iterator, bool>,
				EmplaceBack(std::move(key)).first->
				// and Iterator.second is the value
				second;
		}

		ConstIterator CBegin() const
		{
			return ConstIterator(_key_ref_list.cbegin(), _value_map);
		}

		ConstIterator CEnd() const
		{
			return ConstIterator(_key_ref_list.cend(), _value_map);
		}

		Iterator Begin()
		{
			return Iterator(_key_ref_list.begin(), _value_map);
		}

		Iterator End()
		{
			return Iterator(_key_ref_list.end(), _value_map);
		}

		ConstIterator Begin() const
		{
			return CBegin();
		}

		ConstIterator End() const
		{
			return CEnd();
		}

		// For STL
		inline ConstIterator cbegin() const
		{
			return CBegin();
		}

		inline ConstIterator cend() const
		{
			return CEnd();
		}

		inline Iterator begin()
		{
			return Begin();
		}

		inline Iterator end()
		{
			return End();
		}

		inline ConstIterator begin() const
		{
			return Begin();
		}

		inline ConstIterator end() const
		{
			return End();
		}

	private:
		template <typename... Targs>
		typename std::unordered_map<Tkey, Tvalue>::iterator EmplaceBackInternal(const Tkey &key, Targs &&...args)
		{
			auto it = _value_map.find(key);

			if (it == _value_map.end())
			{
				_value_map.emplace(key, Tvalue());
				it = _value_map.find(key);
				_key_ref_list.emplace_back(std::ref(it->first));
			}

			return it;
		}

	private:
		KeyRefList _key_ref_list;
		std::unordered_map<Tkey, Tvalue> _value_map;
	};
}  // namespace ov
