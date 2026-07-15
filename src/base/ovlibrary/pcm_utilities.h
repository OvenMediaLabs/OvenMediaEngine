//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

namespace ov
{
// Interleave data of source and store it in destination
	// For example, the source will be interleaved while channels = 2, samples = 5
	// Source:
	//  00 01 02 03 04 05 06 07 08 09
	//  L0 L1 L2 L3 L4 R0 R1 R2 R3 R4
	//  |<-Channel 1->|<-Channel 2->|
	// Destination:
	//  00 05 01 06 02 07 03 08 04 09
	//  L0 R0 L1 R1 L2 R2 L3 R3 L4 R4
	template<typename T>
	bool Interleave(void *destination, const void *source, int channels, int samples)
	{
		const T *src = static_cast<const T *>(source);

		for(int channel = 0; channel < channels; ++channel)
		{
			T *dst = static_cast<T *>(destination) + channel;

			for(int sample = 0; sample < samples; ++sample)
			{
				// destination
				*dst = *src++;
				dst += channels;
			}
		}

		return true;
	}

	// Interleave left & right channel data into destination
	// For example, the source will be interleaved while samples = 5
	// Left:
	//  00 01 02 03 04
	//  L0 L1 L2 L3 L4
	// Right:
	//  05 06 07 08 09
	//  R0 R1 R2 R3 R4
	// Destination:
	//  00 05 01 06 02 07 03 08 04 09
	//  L0 R0 L1 R1 L2 R2 L3 R3 L4 R4
	template<typename T>
	bool Interleave(void *destination, const void *left, const void *right, int samples)
	{
		T *dst = static_cast<T *>(destination);
		const T *l = static_cast<const T *>(left);
		const T *r = static_cast<const T *>(right);

		for(int sample = 0; sample < samples; ++sample)
		{
			// copy left channel data into dst
			*dst++ = *l++;
			// copy right channel data into dst
			*dst++ = *r++;
		}

		return true;
	}
}