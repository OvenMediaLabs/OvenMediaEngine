#pragma once
#include "base/ovlibrary/ovlibrary.h"

#define MIN_RTCP_SDES_CHUNK_SIZE	4 + 1 + 1	// ssrc/csrc(4) + type(1) + length field(1) + data length(x)

class SdesChunk
{
public:
	enum class Type : uint8_t
	{
		CNAME = 1,
		NAME = 2,
		EMAIL = 3,
		PHONE = 4,
		LOC = 5,
		TOOL = 6,
		NOTE = 7,
		PRIV = 8,
		RtpStreamId = 12,
		RepairedRtpStreamId = 13
	};

	SdesChunk(){}
	SdesChunk(uint32_t src, Type type, ov::String text)
	{
		_ssrc = src;
		_type = type;
		_text = text;
	}

	static std::shared_ptr<SdesChunk> Parse(const uint8_t *data, size_t data_size);
	std::shared_ptr<ov::Data> GetData();

	void Print();

	uint32_t GetSsrc() const { return _ssrc; }
	Type GetType() const { return _type; }
	ov::String GetText() const { return _text; }

private:
	uint32_t _ssrc = 0; 
	Type _type;
	ov::String _text;
};