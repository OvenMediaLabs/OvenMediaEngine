#include "vp8_parser.h"

#include <base/ovlibrary/bit_reader.h>
#include <base/ovlibrary/log.h>
#include <base/ovlibrary/ovlibrary.h>

#include <memory>

#define OV_LOG_TAG "VP8Parser"

bool VP8Parser::IsValid(const uint8_t *data, size_t data_length)
{
	if (data == nullptr || data_length < 10)
	{
		return false;
	}

	return true;
}

bool VP8Parser::ParseKeyFrame(const uint8_t *data, size_t data_length, bool &is_key_frame)
{
	if (data == nullptr || data_length < 3)
	{
		logtw("Invalid VP8 bitstream");
		return false;
	}

	const uint32_t frame_tag = static_cast<uint32_t>(data[0]) |
							   (static_cast<uint32_t>(data[1]) << 8) |
							   (static_cast<uint32_t>(data[2]) << 16);

	is_key_frame = ((frame_tag & 0x1) == 0);
	return true;
}

/*
Reference : https://datatracker.ietf.org/doc/rfc6386/?include_text=1

19.1.  Uncompressed Data Chunk

   | Frame Tag                                         | Type  |
   | ------------------------------------------------- | ----- |
   | frame_tag                                         | f(24) |
   | if (key_frame) {                                  |       |
   |     start_code                                    | f(24) |
   |     horizontal_size_code                          | f(16) |
   |     vertical_size_code                            | f(16) |
   | }                                                 |       |

   The 3-byte frame tag can be parsed as follows:

   ---- Begin code block --------------------------------------

   unsigned char *c = pbi->source;
   unsigned int tmp;

   tmp = (c[2] << 16) | (c[1] << 8) | c[0];

   key_frame = tmp & 0x1;
   version = (tmp >> 1) & 0x7;
   show_frame = (tmp >> 4) & 0x1;
   first_part_size = (tmp >> 5) & 0x7FFFF;

   ---- End code block ----------------------------------------

   Where:

   o  key_frame indicates whether the current frame is a key frame
      or not.

   o  version determines the bitstream version.

   o  show_frame indicates whether the current frame is meant to be
      displayed or not.

   o  first_part_size determines the size of the first partition
      (control partition), excluding the uncompressed data chunk.

   The start_code is a constant 3-byte pattern having value 0x9d012a.
   The latter part of the uncompressed chunk (after the start_code) can
   be parsed as follows:

   ---- Begin code block --------------------------------------

   unsigned char *c = pbi->source + 6;
   unsigned int tmp;

   tmp = (c[1] << 8) | c[0];

   width = tmp & 0x3FFF;
   horizontal_scale = tmp >> 14;

   tmp = (c[3] << 8) | c[2];

   height = tmp & 0x3FFF;
   vertical_scale = tmp >> 14;

   ---- End code block ----------------------------------------
*/
bool VP8Parser::Parse(const uint8_t *data, size_t data_length, VP8Parser &parser)
{
	bool is_key_frame = false;
	if (ParseKeyFrame(data, data_length, is_key_frame) == false)
	{
		return false;
	}

	if (data_length < 10)
	{
		logtw("Invalid VP8 bitstream");
		return false;
	}

	BitReader reader(data, data_length);

	uint32_t frame_tag = reader.ReadBits<uint8_t>(8) | (reader.ReadBits<uint8_t>(8) << 8) | (reader.ReadBits<uint8_t>(8) << 16);

	parser._key_frame = is_key_frame;
	parser._version = (frame_tag >> 1) & 0x7;
	parser._show_frame = (frame_tag >> 4) & 0x1;
	parser._first_part_size = (frame_tag >> 5) & 0x7FFFF;

	if (data_length <= parser._first_part_size + (parser._key_frame ? 10 : 3))
	{
		logtw("Invalid VP8 bitstream");
		return false;
	}

	if (parser._key_frame == true)
	{
		uint16_t tmp;

		uint32_t start_code = reader.ReadBits<uint8_t>(8) << 16 | (reader.ReadBits<uint8_t>(8) << 8) | (reader.ReadBits<uint8_t>(8) << 0);
		if (start_code != 0x9d012a)
		{
			logtw("Invalid VP8 bitstream");
			return false;
		}

		tmp = (reader.ReadBits<uint8_t>(8) << 0) | (reader.ReadBits<uint8_t>(8) << 8);

		parser._width = tmp & 0x3FFF;
		parser._horizontal_scale = tmp >> 14;

		tmp = (reader.ReadBits<uint8_t>(8) << 0) | (reader.ReadBits<uint8_t>(8) << 8);

		parser._height = tmp & 0x3FFF;
		parser._vertical_scale = tmp >> 14;

		if (!parser._width || !parser._height)
		{
			logtw("Invalid VP8 bitstream");
			return false;
		}

		// logte("start_code : %x, key_frame : %d, version : %d, show_frame : %d, first_part_size : %d", start_code, parser._key_frame, parser._version, parser._show_frame, parser._first_part_size);
		// logtw("width : %d, height : %d / width_scale : %d, height_scale : %d", parser._width, parser._height, parser._horizontal_scale, parser._vertical_scale);
	}

	return true;
}

bool VP8Parser::IsKeyFrame()
{
	return _key_frame;
}

uint16_t VP8Parser::GetWidth()
{
	return _width;
}

uint16_t VP8Parser::GetHeight()
{
	return _height;
}

uint8_t VP8Parser::GetHorizontalScale()
{
	return _horizontal_scale;
}

uint8_t VP8Parser::GetVerticalScale()
{
	return _vertical_scale;
}

ov::String VP8Parser::GetInfoString()
{
	ov::String out_str = ov::String::FormatString("\n[VP8Parser]\n");

	// out_str.AppendFormat("\tId(%d)\n", Id());
	// out_str.AppendFormat("\tLayer(%d)\n", Layer());
	// out_str.AppendFormat("\tProtectionAbsent(%s)\n", ProtectionAbsent()?"true":"false");
	// out_str.AppendFormat("\tProfile(%d/%s)\n", Profile(), ProfileString().CStr());
	// out_str.AppendFormat("\tSamplerate(%d/%d)\n", Samplerate(), SamplerateNum());
	// out_str.AppendFormat("\tChannelConfiguration(%d)\n", ChannelConfiguration());
	// out_str.AppendFormat("\tHome(%s)\n", Home()?"true":"false");
	// out_str.AppendFormat("\tAacFrameLength(%d)\n", AacFrameLength());

	return out_str;
}
