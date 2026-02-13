//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "./string.h"
#include "./assert.h"
#include "./memory_utilities.h"
#include "./data.h"

#include <memory>
#include <algorithm>
#include <cstddef>

namespace ov
{
	class Data
	{
	public:
		// Default constructor
		Data();

		/// Constructs a instance with the capacity (preallocated)
		///
		/// @param capacity
		explicit Data(size_t capacity);

		/// Constructs a instance from another data
		///
		/// @param data data to copy/reference
		/// @param length length of data
		/// @param reference_only a flag to determine whether to copy data or not
		///
		/// @remarks
		/// If reference_only is true, there is a problem if the data is changed or released because the memory is referenced without allocating memory separately.
		/// If reference_only is false, it will not be affected if the data changes because it allocates a new memory and copies it there.
		Data(const void *data, size_t length, bool reference_only = false);

		// Copy constructor
		Data(const Data &data);

		// Move constructor
		Data(Data &&data) noexcept;

		virtual ~Data() = default;

		/// Create a new data from this instance. The newly created data is managed by copy-on-write method
		///
		/// @return
		std::shared_ptr<Data> Clone() const;

		/// Get the read-only pointer
		///
		/// @return read-only pointer
		inline const void *GetData() const
		{
			return static_cast<const uint8_t *>((_reference_data != nullptr) ? _reference_data : _allocated_data->data()) + _offset;
		}

		template<typename T>
		inline const T *GetDataAs() const
		{
			return reinterpret_cast<const T *>(GetData());
		}

		inline const uint8_t At(off_t index) const
		{
			return AtAs<uint8_t>(index);
		}

		template<typename T = uint8_t>
		inline const T AtAs(off_t index) const
		{
			if((index < 0) || ((index * sizeof(T)) >= GetLength()))
			{
				return T();
			}

			auto data = GetDataAs<const T>();

			return data[index];
		}

		/// Get the writable pointer
		///
		/// @return writable pointer
		inline void *GetWritableData()
		{
			if(Detach())
			{
				return _allocated_data->data() + _offset;
			}

			return nullptr;
		}

		template<typename T>
		inline T *GetWritableDataAs()
		{
			return reinterpret_cast<T *>(GetWritableData());
		}

		/// Data 인스턴스 안에 포함되어 있는 데이터의 크기
		///
		/// @return byte 단위의 데이터 크기
		inline size_t GetLength() const noexcept
		{
			return _length;
		}

		// For debugging
		inline size_t GetAllocatedDataSize() const
		{
			return (_allocated_data != nullptr) ? _allocated_data->size() : 0ULL;
		}

		inline bool SetLength(size_t length)
		{
			// Detach() will called in Reserve()
			if(Reserve(length))
			{
				_allocated_data->resize(length);
				_length = length;
				return true;
			}

			return false;
		}

		/// capacity byte만큼 데이터가 저장될 수 있는 공간을 미리 확보.
		///
		/// @param capacity 할당할 메모리 크기를 결정하기 위한 개수 (최소한 capacity만큼 메모리를 할당함)
		///
		/// @return 메모리가 정상적으로 할당되었는지 여부
		///
		/// @remark 기존 데이터는 그대로 유지되며, _reference_data가 설정되어 있을 경우엔 동작하지 않음.
		///         본 메서드가 호출되는 순간, cow가 발생함
		bool Reserve(size_t capacity);

		/// 할당되어 있는 메모리 크기를 얻어옴.
		///
		/// @return 할당되어 있는 메모리 크기
		inline size_t GetCapacity() const noexcept
		{
			return (_allocated_data != nullptr) ? _allocated_data->capacity() : 0;
		}

		/// 버퍼에 있는 데이터 모두 삭제
		///
		/// @return 성공적으로 삭제되었는지 여부
		///
		/// @remarks SetLength()를 통해 크기를 조절할 경우 실제 메모리가 해제되지는 않으므로,
		///          메모리 해제를 원한다면 Release()를 호출해야 함.
		bool Clear() noexcept;

		bool Insert(const void *data, off_t offset, size_t length);
		bool Insert(const Data *data, off_t offset);
		bool Insert(const std::shared_ptr<const Data> &data, off_t offset);

		bool Append(const void *data, size_t length);
		bool Append(const Data *data);
		bool Append(const std::shared_ptr<Data> &data);
		bool Append(const std::shared_ptr<const Data> &data);

		template<typename T, typename = std::enable_if_t<std::is_fundamental_v<T>>>
		bool Append(const T &data)
		{
			return Append(&data, sizeof(T));
		}

		bool Erase(off_t offset, size_t length);

		/// this 데이터의 일부 영역만 참조하는 Data instance 생성
		///
		/// @param offset 원본 데이터에서 참조할 offset. 만약 offset이 음수라면, <GetLength() + offset> 부분부터 참조하는 instance를 생성함
		/// @param length 원본 데이터에서 참조할 바이트 수
		///
		/// @return 생성된 Data instance
		///
		/// @remarks
		/// 다른 Data instance가 입력될 경우, 기본적으로 copy-on-write 모드로 동작함.
		/// 즉, 원본 데이터나 this에 아무런 조작을 하지 않을 경우 포인터만 보관하고 있으며,
		/// 다른 Data instance의 데이터 혹은 this instance의 데이터가 변경되면 그 때 메모리 복사가 이루어짐.
		std::shared_ptr<Data> Subdata(off_t offset, size_t length);
		std::shared_ptr<const Data> Subdata(off_t offset, size_t length) const;
		std::shared_ptr<Data> Subdata(off_t offset);
		std::shared_ptr<const Data> Subdata(off_t offset) const;

		Data &operator =(const Data &data);

		bool operator ==(const Data &data) const;
		bool operator ==(const Data *data) const;
		bool operator ==(const std::shared_ptr<const Data> &data) const;

		bool IsEqual(const void *data, size_t length) const;
		bool IsEqual(const Data &data) const;
		bool IsEqual(const Data *data) const;
		bool IsEqual(const std::shared_ptr<const Data> &data) const;
		bool IsEqual(const std::shared_ptr<Data> &data) const;

		bool IsEmpty() const;

		String Dump(size_t max_bytes = 1024) const noexcept;
		String Dump(const char *title, const char *line_prefix) const noexcept;
		String Dump(const char *title, off_t offset = 0, size_t max_bytes = 1024, const char *line_prefix = nullptr) const noexcept;
		String ToString() const;
		String ToHexString(size_t length) const;
		String ToHexString() const;

	protected:
		std::shared_ptr<const Data> SubdataInternal(off_t offset, size_t length) const;

		/// Called to separate from the origin data
		///
		/// @return true on success, false on failure
		bool Detach();

		const void *_reference_data = nullptr;

		// Allocated data. If this data is subdata, _current_data and _data can be different.
		std::shared_ptr<std::vector<uint8_t>> _allocated_data = nullptr;
		// Offset from _allocated_data
		off_t _offset = 0;

		// Length of data
		// _length =
		// if(_allocated_data != nullptr)
		//     _allocated_data->size() - _offset
		// else
		//     <length of _reference_data>
		size_t _length = 0;
	};

	template<typename T>
	bool Serialize(ov::Data &data, const std::vector<T> &vector)
	{
		// TODO: implement network byte order
		size_t size = vector.size();
		return data.Append(&size, sizeof(size)) && ((size == 0) || (data.Append(vector.data(), size * sizeof(T))));
	}


	template<typename T>
	bool Deserialize(const uint8_t *&bytes, size_t &length, std::vector<T> &vector, size_t &bytes_consumed)
	{
		// TODO: implement network byte order
		const uint8_t *position = bytes;
		size_t bytes_remaining = length;

		if (bytes_remaining < sizeof(size_t))
		{
			return false;
		}
		const size_t size = *reinterpret_cast<const size_t*>(position);
		position += sizeof(size_t);
		bytes_remaining -= sizeof(size_t);
		
		if (bytes_remaining < size * sizeof(T))
		{
			return false;
		}
		vector.insert(vector.end(), reinterpret_cast<const T*>(position), reinterpret_cast<const T*>(position) + size);
		position += size * sizeof(T);
		bytes_remaining -= size * sizeof(T);

		bytes_consumed += length - bytes_remaining;
		length = bytes_remaining;
		bytes = position;
		return true;
	}
}