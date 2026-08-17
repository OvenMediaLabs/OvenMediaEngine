//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "crc_32.h"

namespace ov
{
	static unsigned long crc_table[256];
	static int crc_table_computed = false;

	static void MakeCrcTable()
	{
		unsigned long c;

		int n, k;
		for(n = 0; n < 256; n++)
		{
			c = (unsigned long)n;
			for(k = 0; k < 8; k++)
			{
				if(c & 1)
				{
					c = 0xEDB88320L ^ (c >> 1L);
				}
				else
				{
					c = c >> 1;
				}
			}
			crc_table[n] = c;
		}

		crc_table_computed = true;
	}

	static unsigned long UpdateCrc(unsigned long initial, const unsigned char *buf, ssize_t len)
	{
		unsigned long c = initial ^0xFFFFFFFFL;
		ssize_t n;

		if(crc_table_computed == false)
		{
			MakeCrcTable();
		}
		for(n = 0; n < len; n++)
		{
			c = crc_table[(c ^ buf[n]) & 0xFF] ^ (c >> 8);
		}
		return c ^ 0xFFFFFFFFL;
	}

	uint32_t Crc32::Update(uint32_t initial, const void *buffer, ssize_t length)
	{
		return static_cast<uint32_t>(UpdateCrc(initial, static_cast<const uint8_t *>(buffer), length));
	}

	uint32_t Crc32::Update(uint32_t initial, const ov::Data *data)
	{
		return Update(initial, data->GetData(), data->GetLength());
	}

	uint32_t Crc32::Calculate(const void *buffer, ssize_t length)
	{
		return Update(0, static_cast<const uint8_t *>(buffer), length);
	}

	uint32_t Crc32::Calculate(const ov::Data *data)
	{
		return Calculate(data->GetData(), data->GetLength());
	}
}
