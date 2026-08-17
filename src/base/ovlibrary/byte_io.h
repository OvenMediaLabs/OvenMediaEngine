#pragma once

#include <stdint.h>
#include <limits>

template<typename T,
	unsigned int B = sizeof(T),
	bool is_signed = std::numeric_limits<T>::is_signed>
class ByteReader;

template<typename T>
class ByteReader<T, 1, false>
{
public:
	static T ReadBigEndian(const uint8_t *data)
	{
		return data[0];
	}

	static T ReadLittleEndian(const uint8_t *data)
	{
		return data[0];
	}
};

template<typename T,
	unsigned int B = sizeof(T),
	bool is_signed = std::numeric_limits<T>::is_signed>
class ByteWriter;

template <typename T, unsigned int B>
class ByteReader<T, B, false> {
public:
	static T ReadBigEndian(const uint8_t* data) 
	{
		if(B <= sizeof(T))
		{
			return 0;
		}
		return InternalReadBigEndian(data);
	}

	static T ReadLittleEndian(const uint8_t* data) 
	{
		if(B <= sizeof(T))
		{
			return 0;
		}
		return InternalReadLittleEndian(data);
	}

private:
	static T InternalReadBigEndian(const uint8_t* data) 
	{
		T val(0);
		for (unsigned int i = 0; i < B; ++i)
		{
			val |= static_cast<T>(data[i]) << ((B - 1 - i) * 8);
		}
		return val;
	}

	static T InternalReadLittleEndian(const uint8_t* data) 
	{
		T val(0);
		for (unsigned int i = 0; i < B; ++i)
		{
			val |= static_cast<T>(data[i]) << (i * 8);
		}
		return val;
	}
};

template<typename T>
class ByteWriter<T, 1, false>
{
public:
	static void WriteBigEndian(uint8_t *data, T val)
	{
		data[0] = val;
	}

	static void WriteLittleEndian(uint8_t *data, T val)
	{
		data[0] = val;
	}
};

template<typename T>
class ByteReader<T, 2, false>
{
public:
	static T ReadBigEndian(const uint8_t *data)
	{
		return (data[0] << 8) | data[1];
	}

	static T ReadLittleEndian(const uint8_t *data)
	{
		return data[0] | (data[1] << 8);
	}
};

template<typename T>
class ByteWriter<T, 2, false>
{
public:
	static void WriteBigEndian(uint8_t *data, T val)
	{
		data[0] = val >> 8;
		data[1] = val;
	}

	static void WriteLittleEndian(uint8_t *data, T val)
	{
		data[0] = val;
		data[1] = val >> 8;
	}
};

template<typename T>
class ByteReader<T, 3, false>
{
public:
	static T ReadBigEndian(const uint8_t *data)
	{
		return (Get(data, 0) << 16) | (Get(data, 1) << 8) | (Get(data, 2));
	}

	static T ReadLittleEndian(const uint8_t *data)
	{
		return Get(data, 0) | (Get(data, 1) << 8) | (Get(data, 2) << 16);
	}

private:
	inline static T Get(const uint8_t *data, unsigned int index)
	{
		return static_cast<T>(data[index]);
	}
};

template<typename T>
class ByteReader<T, 4, false>
{
public:
	static T ReadBigEndian(const uint8_t *data)
	{
		return (Get(data, 0) << 24) | (Get(data, 1) << 16) | (Get(data, 2) << 8) | Get(data, 3);
	}

	static T ReadLittleEndian(const uint8_t *data)
	{
		return Get(data, 0) | (Get(data, 1) << 8) | (Get(data, 2) << 16) | (Get(data, 3) << 24);
	}

private:
	inline static T Get(const uint8_t *data, unsigned int index)
	{
		return static_cast<T>(data[index]);
	}
};

template<typename T>
class ByteWriter<T, 3, false>
{
public:
	static void WriteBigEndian(uint8_t *data, T val)
	{
		data[0] = val >> 16;
		data[1] = val >> 8;
		data[2] = val;
	}

	static void WriteLittleEndian(uint8_t *data, T val)
	{
		data[0] = val;
		data[1] = val >> 8;
		data[2] = val >> 16;
	}
};


template<typename T>
class ByteWriter<T, 4, false>
{
public:
	static void WriteBigEndian(uint8_t *data, T val)
	{
		data[0] = val >> 24;
		data[1] = val >> 16;
		data[2] = val >> 8;
		data[3] = val;
	}

	static void WriteLittleEndian(uint8_t *data, T val)
	{
		data[0] = val;
		data[1] = val >> 8;
		data[2] = val >> 16;
		data[3] = val >> 24;
	}
};

template<typename T>
class ByteReader<T, 8, false>
{
public:
	static T ReadBigEndian(const uint8_t *data)
	{
		return
			(Get(data, 0) << 56) | (Get(data, 1) << 48) |
			(Get(data, 2) << 40) | (Get(data, 3) << 32) |
			(Get(data, 4) << 24) | (Get(data, 5) << 16) |
			(Get(data, 6) << 8) | Get(data, 7);
	}

	static T ReadLittleEndian(const uint8_t *data)
	{
		return
			Get(data, 0) | (Get(data, 1) << 8) |
			(Get(data, 2) << 16) | (Get(data, 3) << 24) |
			(Get(data, 4) << 32) | (Get(data, 5) << 40) |
			(Get(data, 6) << 48) | (Get(data, 7) << 56);
	}

private:
	inline static T Get(const uint8_t *data, unsigned int index)
	{
		return static_cast<T>(data[index]);
	}
};

template<typename T>
class ByteWriter<T, 8, false>
{
public:
	static void WriteBigEndian(uint8_t *data, T val)
	{
		data[0] = val >> 56;
		data[1] = val >> 48;
		data[2] = val >> 40;
		data[3] = val >> 32;
		data[4] = val >> 24;
		data[5] = val >> 16;
		data[6] = val >> 8;
		data[7] = val;
	}

	static void WriteLittleEndian(uint8_t *data, T val)
	{
		data[0] = val;
		data[1] = val >> 8;
		data[2] = val >> 16;
		data[3] = val >> 24;
		data[4] = val >> 32;
		data[5] = val >> 40;
		data[6] = val >> 48;
		data[7] = val >> 56;
	}
};
