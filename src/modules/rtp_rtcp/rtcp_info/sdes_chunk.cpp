#include "sdes_chunk.h"
#include "rtcp_private.h"

#include <base/ovlibrary/byte_io.h>

std::shared_ptr<SdesChunk> SdesChunk::Parse(const uint8_t *data, size_t data_size)
{
	// Not yet implemented
	return nullptr;
}

std::shared_ptr<ov::Data> SdesChunk::GetData()
{
	if(_text.GetLength() > 255)
	{
		return nullptr;
	}

	auto chunk_size = MIN_RTCP_SDES_CHUNK_SIZE + _text.GetLength();
	auto data = std::make_shared<ov::Data>(chunk_size);
	data->SetLength(chunk_size);

	ov::ByteStream write_stream(data);

	write_stream.WriteBE32(_ssrc);
	write_stream.WriteBE(static_cast<uint8_t>(_type));
	write_stream.WriteBE(static_cast<uint8_t>(_text.GetLength()));
	write_stream.Write(_text.ToData(false));

	return data;
}

void SdesChunk::Print()
{
	logti("SDES Chunk >> ssrc/csrc(%u) type(%d) data(%s)", _ssrc, static_cast<uint8_t>(_type), _text.CStr());
}