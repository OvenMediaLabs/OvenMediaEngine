//==============================================================================
//
//  OvenMediaEngine
//
//  Created by getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include <stdint.h>
#include <atomic>

#include "./string.h"
#include "./singleton.h"

namespace ov
{
	class Unique
	{
	public:
		static uint32_t GenerateUint32()
		{
			static std::atomic<uint32_t> _last_issued_uint32{100};
			return _last_issued_uint32 ++;
		}
	};
}