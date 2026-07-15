//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "assert.h"	 // NOLINT
#include "byte_ordering.h"
#include "data.h"

#define OV_DECLARE_READ_FUNCTION(type, name, func)  \
	inline type name() noexcept                     \
	{                                               \
		return func(*Read<type>());                 \
	}                                               \
                                                    \
	template <typename T>                           \
	inline T name##As() noexcept                    \
	{                                               \
		return static_cast<T>(func(*Read<type>())); \
	}

#define OV_DECLARE_WRITE_FUNCTION(type, name, func) \
	inline bool name(type value) noexcept           \
	{                                               \
		return WriteInternal<type>(func(value));    \
	}

namespace ov
{
	class ByteStream
	{
	public:
		// Create Data itself
		explicit ByteStream(size_t reserve_size = 0);

		/// Reads or writes data from ```data```
		///
		/// @param data Data to manipulate

		/// @remarks If data is created in reference mode, only readable (false is returned when Write() is called)
		explicit ByteStream(Data *data);
		explicit ByteStream(const std::shared_ptr<Data> &data);

		/// Reads from ```data```
		///
		/// @param data Data to read
		explicit ByteStream(const Data *data);
		explicit ByteStream(const std::shared_ptr<const Data> &data);

		/// Copy ctor
		///
		/// @param stream Stream instance to copy
		ByteStream(const ByteStream &stream);

		/// Move ctor
		///
		/// @param stream Stream instance to move
		ByteStream(ByteStream &&stream) noexcept = default;

		~ByteStream();

		/// T 타입의 데이터를 count개 만큼 읽되, 현재 위치(offset)는 변경하지 않음
		///
		/// @tparam T 읽을 데이터 타입
		/// @param buffer 읽은 데이터가 저장될 버퍼
		/// @param count 읽을 데이터 개수
		///
		/// @return 읽은 데이터 개수 (0 ~ count 사이)
		template <typename T = uint8_t>
		inline size_t Peek(T *buffer, size_t count) const
		{
			size_t peek_count = std::min(Remained<T>(), count);

			if (peek_count > 0)
			{
				if (buffer != nullptr)
				{
					::memcpy(buffer, CurrentBuffer<T>(), sizeof(T) * peek_count);
				}
			}

			return peek_count;
		}

		/// T 타입의 데이터를 1개 읽되, 현재 위치(offset)는 변경하지 않음
		/// @tparam T 읽을 데이터 타입
		/// @param buffer 읽은 데이터가 저장될 버퍼
		///
		/// @return 읽은 데이터 개수 (0 ~ 1 사이)
		template <typename T = uint8_t>
		inline size_t Peek(T *buffer) const
		{
			return Peek<T>(buffer, 1);
		}

		template <typename T = uint8_t>
		inline T Peek() const
		{
			T value;

			if (Peek<T>(&value) == sizeof(T))
			{
				return value;
			}

			return T();
		}

		/// T 타입의 데이터를 count개 읽은 만큼 offset을 변경함
		///
		/// @tparam T skip할 데이터 타입
		/// @param count skip할 데이터 개수
		///
		/// @return skip된 데이터 개수 (0 ~ count 사이)
		template <typename T = uint8_t>
		inline size_t Skip(size_t count)
		{
			return Read<T>(nullptr, count);
		}

		/// T 타입의 데이터 1개 만큼 offset을 변경함
		///
		/// @tparam T skip할 데이터 타입
		///
		/// @return skip된 데이터 개수 (0 ~ 1 사이)
		template <typename T = uint8_t>
		inline size_t Skip()
		{
			return Read<T>(nullptr, 1);
		}

		/// T 타입의 데이터를 count개 만큼 읽음. 읽은 뒤엔 현재 위치(offset)이 변경됨
		///
		/// @tparam T 읽을 데이터 타입
		/// @param buffer 읽은 데이터가 저장될 버퍼
		/// @param count 읽을 데이터 개수
		///
		/// @return 읽은 데이터 개수 (0 ~ count 사이)
		template <typename T = uint8_t>
		inline size_t Read(T *buffer, size_t count)
		{
			size_t copied = Peek(buffer, count);

			_offset += sizeof(T) * copied;

			return copied;
		}

		/// T 타입의 데이터를 1개 읽음. 읽은 뒤엔 현재 위치(offset)이 변경됨
		///
		/// @tparam T 읽을 데이터 타입
		/// @param buffer 읽은 데이터가 저장될 버퍼
		///
		/// @return 읽은 데이터 개수 (0 ~ 1 사이)
		template <typename T = uint8_t>
		inline size_t Read(T *buffer)
		{
			size_t copied = Peek(buffer, 1);

			_offset += sizeof(T) * copied;

			return copied;
		}

		/// 데이터를 T 타입으로 읽음. 읽은 뒤엔 현재 위치가 sizeof(T) 만큼 변경됨
		///
		/// @tparam T 읽을 데이터 타입
		///
		/// @return T 타입의 데이터
		template <typename T = uint8_t>
		inline const T *Read()
		{
			if (Remained<T>() == 0L)
			{
				// 남은 데이터가 없는 상태
				return nullptr;
			}

			const T *buffer = CurrentBuffer<T>();
			_offset += sizeof(T);

			return buffer;
		}

		void ReadBE(uint8_t &value)
		{
			value = *Read();
		}

		void ReadBE(uint16_t &value)
		{
			value = ReadBE16();
		}

		void ReadBE(uint32_t &value)
		{
			value = ReadBE32();
		}

		void ReadBE(uint64_t &value)
		{
			value = ReadBE64();
		}

		// byte order를 고려하여 읽을 수 있는 유틸리티 함수
		// 사용 방법)
		// uint8_t b = stream.Read8();
		// uint16_t little_endian_16 = stream.ReadLE16();

		// Host ordering

		/// 현재 offset 위치의 데이터를 8bit 만큼 읽음
		///
		/// @return 읽은 데이터를 8bit로 반환
		OV_DECLARE_READ_FUNCTION(uint8_t, Read8, );
		/// 현재 offset 위치의 데이터를 16bit 만큼 읽음 (endian을 별도로 지정하지 않음)
		///
		/// @return 읽은 데이터를 16bit로 반환
		OV_DECLARE_READ_FUNCTION(uint16_t, Read16, );
		/// 현재 offset 위치의 데이터를 24bit 만큼 읽음 (endian을 별도로 지정하지 않음)
		///
		/// @return 읽은 데이터를 24bit로 반환
		OV_DECLARE_READ_FUNCTION(uint24_t, Read24, );
		/// 현재 offset 위치의 데이터를 32bit 만큼 읽음 (endian을 별도로 지정하지 않음)
		///
		/// @return 읽은 데이터를 32bit로 반환
		OV_DECLARE_READ_FUNCTION(uint32_t, Read32, );
		/// 현재 offset 위치의 데이터를 64bit 만큼 읽음 (endian을 별도로 지정하지 않음)
		///
		/// @return 읽은 데이터를 64bit로 반환
		OV_DECLARE_READ_FUNCTION(uint64_t, Read64, );

		// LE => HE (Data안에 Little Endian으로 저장되어 있는 것을 Host endian으로 읽음)

		/// 현재 offset 위치의 데이터를 little endian 16bit 데이터로 간주하고 host endian으로 변환하여 읽음
		///
		/// @return 읽은 데이터를 16bit로 반환
		OV_DECLARE_READ_FUNCTION(uint16_t, ReadLE16, LE16ToHost);
		/// 현재 offset 위치의 데이터를 little endian 24bit 데이터로 간주하고 host endian으로 변환하여 읽음
		///
		/// @return 읽은 데이터를 24bit로 반환
		OV_DECLARE_READ_FUNCTION(uint24_t, ReadLE24, LE24ToHost);
		/// 현재 offset 위치의 데이터를 little endian 32bit 데이터로 간주하고 host endian으로 변환하여 읽음
		///
		/// @return 읽은 데이터를 32bit로 반환
		OV_DECLARE_READ_FUNCTION(uint32_t, ReadLE32, LE32ToHost);
		/// 현재 offset 위치의 데이터를 little endian 64bit 데이터로 간주하고 host endian으로 변환하여 읽음
		///
		/// @return 읽은 데이터를 64bit로 반환
		OV_DECLARE_READ_FUNCTION(uint64_t, ReadLE64, LE64ToHost);

		// BE => HE (Data안에 Big Endian으로 저장되어 있는 것을 Host endian으로 읽음)

		/// 현재 offset 위치의 데이터를 big endian 16bit 데이터로 간주하고 host endian으로 변환하여 읽음
		///
		/// @return 읽은 데이터를 16bit로 반환
		OV_DECLARE_READ_FUNCTION(uint16_t, ReadBE16, BE16ToHost);
		/// 현재 offset 위치의 데이터를 big endian 24bit 데이터로 간주하고 host endian으로 변환하여 읽음
		///
		/// @return 읽은 데이터를 24bit로 반환
		OV_DECLARE_READ_FUNCTION(uint24_t, ReadBE24, BE24ToHost);
		/// 현재 offset 위치의 데이터를 big endian 32bit 데이터로 간주하고 host endian으로 변환하여 읽음
		///
		/// @return 읽은 데이터를 32bit로 반환
		OV_DECLARE_READ_FUNCTION(uint32_t, ReadBE32, BE32ToHost);
		/// 현재 offset 위치의 데이터를 big endian 64bit 데이터로 간주하고 host endian으로 변환하여 읽음
		///
		/// @return 읽은 데이터를 64bit로 반환
		OV_DECLARE_READ_FUNCTION(uint64_t, ReadBE64, BE64ToHost);

		// NE => HE (Data안에 Network Endian으로 저장되어 있는 것을 Host endian으로 읽음)

		/// 현재 offset 위치의 데이터를 network endian 16bit 데이터로 간주하고 host endian으로 변환하여 읽음
		///
		/// @return 읽은 데이터를 16bit로 반환
		OV_DECLARE_READ_FUNCTION(uint16_t, ReadNE16, NetworkToHost16);
		/// 현재 offset 위치의 데이터를 network endian 32bit 데이터로 간주하고 host endian으로 변환하여 읽음
		///
		/// @return 읽은 데이터를 32bit로 반환
		OV_DECLARE_READ_FUNCTION(uint24_t, ReadNE24, NetworkToHost24);
		/// 현재 offset 위치의 데이터를 network endian 32bit 데이터로 간주하고 host endian으로 변환하여 읽음
		///
		/// @return 읽은 데이터를 32bit로 반환
		OV_DECLARE_READ_FUNCTION(uint32_t, ReadNE32, NetworkToHost32);
		/// 현재 offset 위치의 데이터를 network endian 64bit 데이터로 간주하고 host endian으로 변환하여 읽음
		///
		/// @return 읽은 데이터를 64bit로 반환
		OV_DECLARE_READ_FUNCTION(uint64_t, ReadNE64, NetworkToHost64);


		/// 현재 offset 위치의 데이터를 읽음
		///
		/// @return 읽은 데이터를 Data 인스턴스로 반환
		std::shared_ptr<Data> ReadData(size_t length);
		/// 현재 offset 위치의 스트링을 읽음
		///
		/// @return 읽은 스트링을 반환
		ov::String ReadString(size_t length);

		/// 데이터를 현재 위치(offset)에 bytes만큼 기록함. 기록 후 현재 위치가 변경됨.
		///
		/// @param buffer 기록할 데이터
		/// @param count 기록할 데이터의 바이트 수
		///
		/// @return 성공적으로 기록되었는지 여부
		///
		/// @remarks
		/// 만약, _data가 is_reference 타입이라면 false가 반환될 수 있음.
		/// 데이터를 기록할 공간이 부족할 경우 _data에 Append() 됨.
		bool Write(const void *data, size_t bytes) noexcept;

		/// 데이터를 현재 위치(offset)에 data를 기록함. 기록 후 현재 위치가 변경됨.
		///
		/// @param data 기록할 데이터
		///
		/// @return 성공적으로 기록되었는지 여부
		///
		/// @remarks
		/// 만약, _data가 is_reference 타입이라면 false가 반환될 수 있음.
		/// 데이터를 기록할 공간이 부족할 경우 _data에 Append() 됨.
		bool Write(const std::shared_ptr<const Data> &data) noexcept
		{
			return Write(data->GetData(), data->GetLength());
		}

		bool Write(const Data &data) noexcept
		{
			return Write(data.GetData(), data.GetLength());
		}

		bool Write(const Data *data) noexcept
		{
			return Write(data->GetData(), data->GetLength());
		}

		/// T 타입의 데이터를 현재 위치(offset)에 count개 만큼 기록함. 기록 후 현재 위치가 변경됨.
		///
		/// @tparam T 기록할 데이터 타입
		/// @param buffer 기록할 데이터
		/// @param count 기록할 데이터 개수
		///
		/// @return 성공적으로 기록되었는지 여부
		///
		/// @remarks
		/// 만약, _data가 is_reference 타입이라면 false가 반환될 수 있음.
		/// 데이터를 기록할 공간이 부족할 경우 _data에 Append() 됨.
		template <typename T>
		bool Write(const T *buffer, size_t count)
		{
			return Write((const void *)buffer, sizeof(T) * count);
		}

		/// T 타입의 데이터 1개를 현재 위치(offset)에 기록함. 기록 후 현재 위치가 변경됨.
		///
		/// @tparam T 기록할 데이터 타입
		/// @param buffer 기록할 데이터
		///
		/// @return 성공적으로 기록되었는지 여부
		///
		/// @remarks
		/// 만약, _data가 is_reference 타입이라면 false가 반환될 수 있음.
		/// 데이터를 기록할 공간이 부족할 경우 _data에 Append() 됨.
		template <typename T>
		bool Write(const T *buffer)
		{
			return Write(buffer, 1);
		}

		bool WriteText(const ov::String &text, bool null_terminate = false) noexcept
		{
			return Write(text.ToData(null_terminate));
		}

		bool WriteBE(uint8_t value)
		{
			return Write(&value);
		}

		bool WriteBE(uint16_t value)
		{
			return WriteBE16(value);
		}

		bool WriteBE(uint32_t value)
		{
			return WriteBE32(value);
		}

		bool WriteBE(uint64_t value)
		{
			return WriteBE64(value);
		}

		/// data를 데이터 끝에 bytes만큼 기록함. 기록 후 현재 위치가 변경되지 않음.
		///
		/// @param buffer 기록할 data
		/// @param count 기록할 데이터의 바이트 수
		///
		/// @return 성공적으로 기록되었는지 여부
		///
		/// @remarks
		/// 만약, _data가 is_reference 타입이라면 false가 반환될 수 있음.
		bool Append(const void *data, size_t bytes) noexcept;

		/// data를 데이터 끝에 bytes만큼 기록함. 기록 후 현재 위치가 변경되지 않음.
		///
		/// @param data 기록할 data
		///
		/// @return 성공적으로 기록되었는지 여부
		///
		/// @remarks
		/// 만약, _data가 is_reference 타입이라면 false가 반환될 수 있음.
		bool Append(const std::shared_ptr<const Data> &data) noexcept
		{
			return Append(data->GetData(), data->GetLength());
		}

		/// T 타입의 데이터를 데이터 끝에 count개 만큼 추가함. 기록 후 현재 위치가 변경되지 않음.
		///
		/// @tparam T 기록할 데이터 타입
		/// @param buffer 기록할 데이터
		/// @param count 기록할 데이터 개수
		///
		/// @return 성공적으로 기록되었는지 여부
		///
		/// @remarks
		/// 만약, _data가 is_reference 타입이라면 false가 반환될 수 있음.
		/// 데이터를 기록할 공간이 부족할 경우 _data에 Append() 됨.
		template <typename T>
		bool Append(const T *buffer, size_t count)
		{
			return Append((const void *)buffer, sizeof(T) * count);
		}

		/// T 타입의 데이터 1개를 데이터 끝에 추가함. 기록 후 현재 위치가 변경되지 않음.
		///
		/// @tparam T 기록할 데이터 타입
		/// @param buffer 기록할 데이터
		///
		/// @return 성공적으로 기록되었는지 여부
		///
		/// @remarks
		/// 만약, _data가 is_reference 타입이라면 false가 반환될 수 있음.
		template <typename T>
		bool Append(const T *buffer)
		{
			return Append(buffer, 1);
		}

		// byte order를 고려하여 쓸 수 있는 유틸리티 함수
		// 사용 방법)
		// stream.Write8((uint8_t)0x12);
		// stream.WriteLE16((uint8_t)0x1234);

		// Host ordering

		/// 현재 offset 위치에 8bit 데이터를 기록함 (endian을 별도로 지정하지 않음)
		///
		/// @return 성공적으로 기록 되었는지 여부
		OV_DECLARE_WRITE_FUNCTION(uint8_t, Write8, );
		/// 현재 offset 위치에 16bit 데이터를 기록함 (endian을 별도로 지정하지 않음)
		///
		/// @return 성공적으로 기록 되었는지 여부
		OV_DECLARE_WRITE_FUNCTION(uint16_t, Write16, );
		/// 현재 offset 위치에 24bit 데이터를 기록함 (endian을 별도로 지정하지 않음)
		///
		/// @return 성공적으로 기록 되었는지 여부
		OV_DECLARE_WRITE_FUNCTION(uint24_t, Write24, );
		/// 현재 offset 위치에 32bit 데이터를 기록함 (endian을 별도로 지정하지 않음)
		///
		/// @return 성공적으로 기록 되었는지 여부
		OV_DECLARE_WRITE_FUNCTION(uint32_t, Write32, );
		/// 현재 offset 위치에 64bit 데이터를 기록함 (endian을 별도로 지정하지 않음)
		///
		/// @return 성공적으로 기록 되었는지 여부
		OV_DECLARE_WRITE_FUNCTION(uint64_t, Write64, );

		// LE => HE (Data안에 Little Endian으로 저장되어 있는 것을 Host endian으로 읽음)

		/// 현재 offset 위치에 host endian 데이터를 little endian 16bit 데이터로 변환하여 기록함
		///
		/// @return 성공적으로 기록 되었는지 여부
		OV_DECLARE_WRITE_FUNCTION(uint16_t, WriteLE16, HostToLE16);
		/// 현재 offset 위치에 host endian 데이터를 little endian 24bit 데이터로 변환하여 기록함
		///
		/// @return 성공적으로 기록 되었는지 여부
		OV_DECLARE_WRITE_FUNCTION(uint24_t, WriteLE32, HostToLE24);
		/// 현재 offset 위치에 host endian 데이터를 little endian 32bit 데이터로 변환하여 기록함
		///
		/// @return 성공적으로 기록 되었는지 여부
		OV_DECLARE_WRITE_FUNCTION(uint32_t, WriteLE32, HostToLE32);
		/// 현재 offset 위치에 host endian 데이터를 little endian 64bit 데이터로 변환하여 기록함
		///
		/// @return 성공적으로 기록 되었는지 여부
		OV_DECLARE_WRITE_FUNCTION(uint64_t, WriteLE64, HostToLE64);

		// BE => HE (Data안에 Big Endian으로 저장되어 있는 것을 Host endian으로 읽음)

		/// 현재 offset 위치에 host endian 데이터를 big endian 16bit 데이터로 변환하여 기록함
		///
		/// @return 성공적으로 기록 되었는지 여부
		OV_DECLARE_WRITE_FUNCTION(uint16_t, WriteBE16, HostToBE16);
		/// 현재 offset 위치에 host endian 데이터를 big endian 24bit 데이터로 변환하여 기록함
		///
		/// @return 성공적으로 기록 되었는지 여부
		OV_DECLARE_WRITE_FUNCTION(uint24_t, WriteBE24, HostToBE24);
		/// 현재 offset 위치에 host endian 데이터를 big endian 32bit 데이터로 변환하여 기록함
		///
		/// @return 성공적으로 기록 되었는지 여부
		OV_DECLARE_WRITE_FUNCTION(uint32_t, WriteBE32, HostToBE32);
		/// 현재 offset 위치에 host endian 데이터를 big endian 64bit 데이터로 변환하여 기록함
		///
		/// @return 성공적으로 기록 되었는지 여부
		OV_DECLARE_WRITE_FUNCTION(uint64_t, WriteBE64, HostToBE64);

		// NE => HE (Data안에 Network Endian으로 저장되어 있는 것을 Host endian으로 읽음)

		/// 현재 offset 위치에 host endian 데이터를 network endian 16bit 데이터로 변환하여 기록함
		///
		/// @return 성공적으로 기록 되었는지 여부
		OV_DECLARE_WRITE_FUNCTION(uint16_t, WriteNE16, HostToNetwork16);
		/// 현재 offset 위치에 host endian 데이터를 network endian 32bit 데이터로 변환하여 기록함
		///
		/// @return 성공적으로 기록 되었는지 여부
		OV_DECLARE_WRITE_FUNCTION(uint32_t, WriteNE32, HostToNetwork32);
		/// 현재 offset 위치에 host endian 데이터를 network endian 24bit 데이터로 변환하여 기록함
		///
		/// @return 성공적으로 기록 되었는지 여부
		OV_DECLARE_WRITE_FUNCTION(uint24_t, WriteNE24, HostToNetwork24);
		/// 현재 offset 위치에 host endian 데이터를 network endian 64bit 데이터로 변환하여 기록함
		///
		/// @return 성공적으로 기록 되었는지 여부
		OV_DECLARE_WRITE_FUNCTION(uint64_t, WriteNE64, HostToNetwork64);

		/// T 형태로 변환하였을 때, 남은(읽을 수 있는) 데이터 수. length와 offset만 고려하며, capacity와는 무관함
		///
		/// @tparam T 데이터 타입
		///
		/// @return 개수
		template <typename T = uint8_t>
		inline size_t Remained() const noexcept
		{
			ssize_t remained_bytes = (_read_only_data->GetLength() - _offset);

			OV_ASSERT(remained_bytes >= 0, "_data is changed while using byte stream");

			remained_bytes = std::max(0L, remained_bytes);

			return remained_bytes / sizeof(T);
		}

		/// 남은 바이트 수 반환
		///
		/// @return 남은 바이트 수
		///
		/// @remarks
		/// Remained<uint8_t>() 와 동일함.
		size_t Remained() const noexcept;

		/// 지정한 bytes 만큼 남았는지 확인
		///
		/// @param bytes 확인할 byte 수
		///
		/// @return 데이터 내 최소 bytes 만큼 있는지 여부
		bool IsRemained(size_t bytes) const noexcept;

		bool IsEmpty() const noexcept;

		size_t GetLength() const noexcept
		{
			return _read_only_data->GetLength();
		}

		/// 저장되어 있는 쓰기 가능한 데이터를 얻어옴
		///
		/// @return 저장된 데이터
		Data *GetData() noexcept;

		/// 저장되어 있는 쓰기 가능한 데이터를 얻어옴
		std::shared_ptr<Data> GetDataPointer() noexcept;

		/// Returns the pointer if shared_ptr<Data> was provided when ByteStream was created.
		// If Data * is provided, nullptr is returned
		std::shared_ptr<const Data> GetDataPointer() const;

		/// GetOffset() 부터 Remained() 크기가 고려된 데이터를 얻어옴
		/// 여기서 얻어온 데이터는, 원본 데이터를 참조만 하기 때문에 ByteStream 객체가 해제되기 전 까지만 사용해야 함
		///
		/// @return 저장된 데이터
		std::shared_ptr<const Data> GetRemainData() const noexcept;
		std::shared_ptr<const Data> GetRemainData(size_t length) const noexcept;

		/// 현재 위치를 얻어옴
		///
		/// @return 현재 위치
		off_t GetOffset() const noexcept;

		/// 현재 위치 pos 만큼 이동
		bool MoveOffset(off_t pos) noexcept;

		/// 현재 위치를 설정함
		///
		/// @param offset 설정할 위치
		///
		/// @return 성공적으로 설정되었는지 여부
		///
		/// @remarks
		/// 만약, offset이 _data의 Count()보다 클 경우, _data에 빈 데이터를 append함
		bool SetOffset(off_t offset) noexcept;

		/// 현재 위치의 데이터를 dump함
		///
		/// @param max_bytes dump할 최대 크기
		/// @param title dump할 때 보여질 제목
		///
		/// @return dump결과가 저장된 문자열
		String Dump(size_t max_bytes = 1024L, const char *title = nullptr) const noexcept;

		/// offset history에 현재 offset을 넣음
		/// 여러 곳에서 stream을 사용할 때, offset을 백업 & 복원하면서 사용해야 하는 이슈가 있는데, 이를 편하게 해주는 유틸리티 함수
		/// (예: 임의의 위치로 이동한 뒤 다시 원래 자리로 오는 것)
		///
		/// @return offset이 성공적으로 push 되었는지 여부
		bool PushOffset() noexcept;

		/// offset history에서 offset을 가져옴
		///
		/// @return 성공적으로 offset이 복원되었는지 여부. 만약 offset history에 항목이 없거나 SetOffset()에 실패하면 false를 반환함.
		///
		/// @see SetOffset()
		bool PopOffset() noexcept;

		/// 현재 버퍼 위치를 T 타입으로 얻어옴
		///
		/// @tparam T 데이터 타입
		///
		/// @return T 타입으로 변경된 현재 위치의 버퍼
		template <typename T>
		inline const T *CurrentBuffer() const
		{
			return reinterpret_cast<const T *>((_read_only_data->GetDataAs<const uint8_t>()) + _offset);
		}

	protected:
		ByteStream(Data *data, const Data *read_only_data, std::shared_ptr<const Data> data_pointer, off_t offset);

		/// T 타입의 데이터 1개를 현재 위치(offset)에 기록함. 기록 후 현재 위치가 변경됨.
		///
		/// @tparam T 기록할 데이터 타입
		/// @param buffer 기록할 데이터
		///
		/// @return 성공적으로 기록되었는지 여부
		///
		/// @remarks
		/// 만약, _data가 is_reference 타입이라면 false가 반환될 수 있음.
		/// 데이터를 기록할 공간이 부족할 경우 _data에 Append() 됨.
		template <typename T>
		bool WriteInternal(const T &buffer)
		{
			return Write<T>(&buffer, 1);
		}

		Data *_data = nullptr;
		const Data *_read_only_data = nullptr;
		std::shared_ptr<Data> _data_writable_pointer = nullptr;
		// If the data were provided in std::shared_ptr, retains it
		std::shared_ptr<const Data> _data_pointer;

		// 데이터를 읽거나 기록할 때 사용될 offset
		off_t _offset = 0L;

		// offset push & pop 할 때 사용하는 history queue
		std::vector<off_t> _offset_stack;
	};
}  // namespace ov

ov::ByteStream &operator<<(ov::ByteStream &byte_stream, const char *string);
ov::ByteStream &operator<<(ov::ByteStream &byte_stream, const std::string &string);
ov::ByteStream &operator<<(ov::ByteStream &byte_stream, const std::string_view &string);

template <size_t length>
ov::ByteStream &operator<<(ov::ByteStream &byte_stream, const char (&string)[length])
{
	byte_stream.Write(string, length);
	return byte_stream;
}
