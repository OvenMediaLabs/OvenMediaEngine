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
#include <random>

#include "./string.h"

namespace ov
{
	class Random
	{
	public:
		Random() = default;
		virtual ~Random() = default;

		template<typename T>
		static T GenerateRandom()
		{
			return GenerateRandom<T>(std::numeric_limits<T>::min(), std::numeric_limits<T>::max());
		}

		template<typename T>
		static T GenerateRandom(T min)
		{
			return GenerateRandom<T>(min, std::numeric_limits<T>::max());
		}

		template<typename T>
		static T GenerateRandom(T min, T max)
		{
			std::random_device rd;
			std::mt19937 mt(rd());
			std::uniform_int_distribution<T> dist(min, max);
			return dist(mt);
		}

		static uint32_t GenerateUInt32(uint32_t min = 1, uint32_t max = UINT32_MAX)
		{
			return GenerateRandom<uint32_t>(min, max);
		}

		static int32_t GenerateInt32(int32_t min = INT32_MIN, int32_t max = INT32_MAX)
		{
			return GenerateRandom<int32_t>(min, max);
		}

		static ov::String GenerateString(uint32_t length);
		static ov::String GenerateNumberString(uint32_t length);

		static void Fill(void *buffer, size_t length);
	};
}