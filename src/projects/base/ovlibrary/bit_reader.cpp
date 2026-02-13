#include "bit_reader.h"

BitReader::BitReader(const uint8_t *buffer, size_t capacity)
	: _buffer(buffer),
	  _position(buffer),
	  _capacity(capacity)
{
}

BitReader::BitReader(const std::shared_ptr<const ov::Data> &data)
	: _buffer(data->GetDataAs<uint8_t>()),
	  _position(data->GetDataAs<uint8_t>()),
	  _capacity(data->GetLength())
{
}
