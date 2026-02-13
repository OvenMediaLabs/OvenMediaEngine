#pragma once

#include <base/ovlibrary/ovlibrary.h>

#include <stdint.h>

class VP8Parser
{
public:
	static bool IsValid(const uint8_t *data, size_t data_length);
	static bool Parse(const uint8_t *data, size_t data_length, VP8Parser &parser);

	bool IsKeyFrame();
	uint16_t GetWidth();
	uint16_t GetHeight();
	uint8_t GetHorizontalScale();
	uint8_t GetVerticalScale();

	ov::String GetInfoString();

public:
	bool _key_frame;
	uint8_t _version;
	uint8_t _show_frame;
	uint32_t _first_part_size;

	uint16_t _width;
	uint8_t _horizontal_scale;

	uint16_t _height;
	uint8_t _vertical_scale;
};