//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <stdint.h>

#include "../ovlibrary/ovlibrary.h"

namespace ov
{
	// RFC1952 - 8. Appendix: Sample CRC Code 부분을 참고하여 구현
	class Crc32
	{
	public:
		Crc32();
		~Crc32();

		static uint32_t Update(uint32_t initial, const void *buffer, ssize_t length);
		static uint32_t Update(uint32_t initial, const ov::Data *data);

		static uint32_t Calculate(const void *buffer, ssize_t length);
		static uint32_t Calculate(const ov::Data *data);
	};
}
