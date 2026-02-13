//==============================================================================
//
//  OvenMediaEngine
//
//  Created by getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#include "random.h"

#include <algorithm>

namespace ov
{
	String Random::GenerateString(uint32_t size)
	{
		std::string str("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");

		std::random_device rd;
		std::mt19937 generator(rd());
		std::shuffle(str.begin(), str.end(), generator);
		String random_str = str.substr(0, size).c_str();

		return random_str;
	}

	String Random::GenerateNumberString(uint32_t length)
	{
		std::string str("0123456789");

		std::random_device rd;
		std::mt19937 generator(rd());
		std::shuffle(str.begin(), str.end(), generator);
		String random_str = str.substr(0, length).c_str();

		return random_str;
	}

	void Random::Fill(void *buffer, size_t length)
	{
		static thread_local std::mt19937 generator([] {
			std::random_device rd;
			return std::mt19937(rd());
		}());
		static thread_local std::uniform_int_distribution<uint8_t> dist(0, 255);

		uint8_t *ptr = static_cast<uint8_t *>(buffer);

		for (size_t i = 0; i < length; ++i)
		{
			*(ptr++) = dist(generator);
		}
	}

}  // namespace ov
