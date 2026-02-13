//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "data.h"

#include <stdint.h>

#include "./assert.h"
#include "./dump_utilities.h"

namespace ov
{
	Data::Data()
		: Data(0)
	{
	}

	Data::Data(size_t capacity)
	{
		Reserve(capacity);
	}

	Data::Data(const void *data, size_t length, bool reference_only)
	{
		if (data == nullptr)
		{
			OV_ASSERT2(false);
			return;
		}

		if (reference_only)
		{
			_reference_data = data;
			_length = length;
		}
		else
		{
			bool result = Reserve(length);
			result = result && Append(data, length);

			OV_ASSERT2(result);
		}
	}

	Data::Data(const Data &data)
	{
		_reference_data = data._reference_data;
		if (data._allocated_data != nullptr)
		{
			_allocated_data = std::make_shared<std::vector<uint8_t>>();
			Append(&data);
		}
		_offset = data._offset;
		_length = data._length;
	}

	Data::Data(Data &&data) noexcept
	{
		std::swap(_reference_data, data._reference_data);
		std::swap(_allocated_data, data._allocated_data);
		std::swap(_offset, data._offset);
		std::swap(_length, data._length);
	}

	std::shared_ptr<Data> Data::Clone() const
	{
		return std::const_pointer_cast<Data>(Subdata(0L));
	}

	std::shared_ptr<const Data> Data::SubdataInternal(off_t offset, size_t length) const
	{
		if (offset < 0)
		{
			// If [offset] is negative, cut the data from the back.
			offset = GetLength() + offset;
		}

		if (offset < 0)
		{
			// Offset is too small
			OV_ASSERT(false, "Invalid offset: %jd", static_cast<intmax_t>(offset));
			return nullptr;
		}

		auto instance = std::make_shared<Data>();

		size_t current_length = GetLength();

		if (current_length < (offset + length))
		{
			OV_ASSERT(false, "offset + length (%zu) must be smaller than %zd", (offset + length), current_length);
			return nullptr;
		}

		off_t new_offset = _offset + offset;

		OV_ASSERT2(new_offset >= 0);

		if (_reference_data != nullptr)
		{
			// Refer _reference_data
			instance->_reference_data = _reference_data;
		}
		else
		{
			// Refer _allocated_data
			instance->_allocated_data = _allocated_data;
		}

		instance->_offset = new_offset;
		instance->_length = length;

		return instance;
	}

	std::shared_ptr<Data> Data::Subdata(off_t offset, size_t length)
	{
		return std::const_pointer_cast<Data>(SubdataInternal(offset, length));
	}

	std::shared_ptr<const Data> Data::Subdata(off_t offset, size_t length) const
	{
		return SubdataInternal(offset, length);
	}

	std::shared_ptr<Data> Data::Subdata(off_t offset)
	{
		if (offset >= 0)
		{
			OV_ASSERT(GetLength() >= static_cast<size_t>(offset), "Invalid offset: %jd (length: %zu)", offset, GetLength());
		}
		else
		{
			OV_ASSERT(GetLength() > static_cast<size_t>(-1LL * offset), "Invalid offset: %jd (length: %zu)", -1LL * offset, GetLength());
		}

		return Subdata(offset, (offset >= 0) ? (GetLength() - offset) : (static_cast<size_t>(-1 * offset)));
	}

	std::shared_ptr<const Data> Data::Subdata(off_t offset) const
	{
		return Subdata(offset, (offset >= 0) ? (GetLength() - offset) : (static_cast<size_t>(-1 * offset)));
	}

	Data &Data::operator=(const Data &data)
	{
		// Do not need to call Detach(). Just overwrite member variables

		// ov::Data supports COW (Copy-on-write), so we just assign the variables of data to member variables.
		_reference_data = data._reference_data;
		_allocated_data = data._allocated_data;
		_offset = data._offset;
		_length = data._length;

		return *this;
	}

	bool Data::operator==(const Data &data) const
	{
		return IsEqual(data);
	}

	bool Data::operator==(const Data *data) const
	{
		return IsEqual(data);
	}

	bool Data::operator==(const std::shared_ptr<const Data> &data) const
	{
		if (data != nullptr)
		{
			return IsEqual(data.get());
		}

		return false;
	}

	bool Data::IsEqual(const void *data, size_t length) const
	{
		if (GetLength() == length)
		{
			if (length == 0)
			{
				return true;
			}

			return ::memcmp(GetData(), data, length) == 0;
		}

		return false;
	}

	bool Data::IsEqual(const Data &data) const
	{
		return IsEqual(data.GetData(), data.GetLength());
	}

	bool Data::IsEqual(const Data *data) const
	{
		if (data == nullptr)
		{
			return false;
		}

		return IsEqual(data->GetData(), data->GetLength());
	}

	bool Data::IsEqual(const std::shared_ptr<const Data> &data) const
	{
		return IsEqual(data->GetData(), data->GetLength());
	}

	bool Data::IsEqual(const std::shared_ptr<Data> &data) const
	{
		return IsEqual(data->GetData(), data->GetLength());
	}

	bool Data::IsEmpty() const
	{
		return (GetLength() == 0);
	}

	bool Data::Detach()
	{
		if (_reference_data != nullptr)
		{
			// Copy from original data
			const void *original_data = _reference_data;
			off_t offset = _offset;
			size_t length = _length;

			_reference_data = nullptr;
			_offset = 0;
			_length = 0;

			return Reserve(length) && Append(static_cast<const uint8_t *>(original_data) + offset, length);
		}

		if (_allocated_data == nullptr)
		{
			OV_ASSERT2(false);
			return false;
		}

		if (_allocated_data.use_count() == 1)
		{
			// Nobody references _allocated_data. So do not need to copy the data
			if (_allocated_data->size() == GetLength())
			{
				return true;
			}

			// Need to shrink the vector size
		}

		// Copy data from <_offset> to <_offset + length>
		auto old_data = _allocated_data;
		auto old_offset = _offset;
		auto begin = old_data->begin() + _offset;
		auto end = begin + _length;

		// Reset the offset
		_offset = 0L;

		_allocated_data = std::make_shared<std::vector<uint8_t>>(begin, end);
		_allocated_data->reserve(old_data->capacity() - old_offset);

		return (_allocated_data != nullptr);
	}

	bool Data::Reserve(size_t capacity)
	{
		if ((_reference_data != nullptr) || (_allocated_data != nullptr))
		{
			if (Detach() == false)
			{
				// Could not copy data from _reference_data
				OV_ASSERT2(false);
				return false;
			}
		}
		else
		{
			_allocated_data = std::make_shared<std::vector<uint8_t>>();
		}

		_allocated_data->reserve(capacity);

		return true;
	}

	bool Data::Clear() noexcept
	{
		// Reallocate the buffer (this method is faster than Detach() & clear());
		_reference_data = nullptr;
		_allocated_data = std::make_shared<std::vector<uint8_t>>();
		_offset = 0;
		_length = 0;

		return true;
	}

	bool Data::Insert(const void *data, off_t offset, size_t length)
	{
		if ((data == nullptr) && (length > 0))
		{
			OV_ASSERT(false, "Invalid parameter: length is greater than 0, but data is NULL");
			return false;
		}

		if (offset > static_cast<off_t>(_length))
		{
			OV_ASSERT(false, "Invalid offset: %jd", offset);
			return false;
		}

		if (offset < 0)
		{
			offset = _length + offset;

			if (offset < 0)
			{
				OV_ASSERT(false, "Invalid offset: %jd", (offset - _length));
			}
		}

		if (Detach() == false)
		{
			return false;
		}

		auto source = static_cast<const uint8_t *>(data);

		_allocated_data->insert(_allocated_data->begin() + (_offset + offset), source, source + length);
		_length += length;

		return true;
	}

	bool Data::Insert(const Data *data, off_t offset)
	{
		return (data != nullptr) ? Insert(data->GetData(), offset, data->GetLength()) : false;
	}

	bool Data::Insert(const std::shared_ptr<const Data> &data, off_t offset)
	{
		return Insert(data.get(), offset);
	}

	bool Data::Append(const void *data, size_t length)
	{
		return Insert(data, GetLength(), length);
	}

	bool Data::Append(const Data *data)
	{
		return (data != nullptr) ? Append(data->GetData(), data->GetLength()) : false;
	}

	bool Data::Append(const std::shared_ptr<Data> &data)
	{
		return Append(data.get());
	}

	bool Data::Append(const std::shared_ptr<const Data> &data)
	{
		return Append(data.get());
	}

	bool Data::Erase(off_t offset, size_t length)
	{
		if (length == 0)
		{
			// Nothing to do
			return true;
		}

		if (Detach() == false)
		{
			return false;
		}

		if ((offset < 0) || (offset + length > _length))
		{
			OV_ASSERT(false, "Invalid offset: %jd, length: %zu (current length: %zu)", offset, length, _length);
			return false;
		}

		auto begin = _allocated_data->begin() + offset;

		_allocated_data->erase(begin, begin + length);
		_length -= length;

		OV_ASSERT(_length == _allocated_data->size(), "length: %zu, allocated size: %zu", _length, _allocated_data->size());

		return true;
	}

	String Data::Dump(size_t max_bytes) const noexcept
	{
		return Dump(nullptr, 0L, max_bytes, nullptr);
	}

	String Data::Dump(const char *title, const char *line_prefix) const noexcept
	{
		return Dump(title, 0L, 1024L, line_prefix);
	}

	String Data::Dump(const char *title, off_t offset, size_t max_bytes, const char *line_prefix) const noexcept
	{
		return ov::Dump(GetData(), GetLength(), title, offset, max_bytes, line_prefix);
	}

	String Data::ToString() const
	{
		return String(GetDataAs<const char>(), GetLength());
	}

	String Data::ToHexString(size_t length) const
	{
		return ov::ToHexString(GetDataAs<const uint8_t>(), std::min(GetLength(), length));
	}

	String Data::ToHexString() const
	{
		return ov::ToHexString(GetDataAs<const uint8_t>(), GetLength());
	}
}  // namespace ov