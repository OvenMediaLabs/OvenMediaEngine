//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "memory_utilities.h"

#include <algorithm>

#include "./assert.h"

namespace ov
{
	size_t BitMemcpy(
		const void *src_data, const size_t src_bits_length, uint8_t src_bit_offset,
		void *dst_data, const size_t dst_bits_length, uint8_t dst_bit_offset,
		size_t bits_to_copy)
	{
		bits_to_copy = std::min({bits_to_copy, src_bits_length, dst_bits_length});

		size_t bits_copied = 0;

		if (bits_to_copy == 0)
		{
			// No bits to copy
			return bits_copied;
		}

		if ((src_bit_offset >= 8) || (dst_bit_offset >= 8))
		{
			OV_ASSERT(false, "Invalid bit offset: src_bit_offset=%u, dst_bit_offset=%u", src_bit_offset, dst_bit_offset);
			return bits_copied;
		}

		auto src_buffer = static_cast<const uint8_t *>(src_data);
		auto dst_buffer = static_cast<uint8_t *>(dst_data);

		if (src_bit_offset == dst_bit_offset)
		{
			// If `src_bit_offset` and `dst_bit_offset` are the same,
			// copy only some data bit by bit and then copy the remaining data
			// in byte units to improve performance

			if (src_bit_offset > 0)
			{
				// Copy the leading bits if `src_bit_offset` is not `0`
				auto bits_to_copy_for_align = std::min(static_cast<size_t>(8 - src_bit_offset), bits_to_copy);

				auto dst_byte = *dst_buffer;
				const auto mask = (1 << bits_to_copy_for_align) - 1;
				dst_byte &= ~mask;
				dst_byte |= (*src_buffer) & mask;
				*dst_buffer = dst_byte;

				src_buffer++;
				dst_buffer++;

				bits_copied += bits_to_copy_for_align;
				bits_to_copy -= bits_to_copy_for_align;

				src_bit_offset = 0;
				dst_bit_offset = 0;
			}

			// Now that the `bit_offset` is `0`, we can process it in byte units
			auto bytes_to_copy = bits_to_copy / 8;

			if (bytes_to_copy > 0)
			{
				::memcpy(dst_buffer, src_buffer, bytes_to_copy);

				src_buffer += bytes_to_copy;
				dst_buffer += bytes_to_copy;

				bits_copied += bytes_to_copy * 8;
				bits_to_copy -= bytes_to_copy * 8;
			}

			// Copy the remaining bits
			if (bits_to_copy > 0)
			{
				auto dst_byte = *dst_buffer;

				const auto mask = (1 << (8 - bits_to_copy)) - 1;
				dst_byte &= mask;
				dst_byte |= (*src_buffer) & (~mask);
				*dst_buffer = dst_byte;

				bits_copied += bits_to_copy;
				bits_to_copy = 0;
			}
		}

		if (bits_to_copy == 0)
		{
			// All bits have been copied
			return bits_copied;
		}

		// If `src_bit_offset` and `dst_bit_offset` are different, copy the remaining bits bit by bit
		uint8_t src_bits = (*src_buffer) << src_bit_offset;
		uint8_t dst_bits = *dst_buffer;
		uint8_t dst_mask = (0b10000000 >> dst_bit_offset);

		for (size_t index = 0; index < bits_to_copy; index++)
		{
			// Get the source bit
			const auto src_bit = (src_bits & 0b10000000) ? 1 : 0;
			if (src_bit_offset < 7)
			{
				src_bits <<= 1;
				src_bit_offset++;
			}
			else
			{
				if (index < bits_to_copy - 1)
				{
					src_bits = *(++src_buffer);
				}
				src_bit_offset = 0;
			}

			// Clear the destination bit
			dst_bits &= ~dst_mask;
			// Set the destination bit if the source bit is set
			if (src_bit)
			{
				dst_bits |= dst_mask;
			}

			if (dst_bit_offset < 7)
			{
				dst_bit_offset++;
				dst_mask >>= 1;
			}
			else
			{
				*(dst_buffer++) = dst_bits;
				if (index < bits_to_copy - 1)
				{
					dst_bits = *dst_buffer;
				}
				dst_bit_offset = 0;
				dst_mask = 0b10000000;
			}
		}

		// If there are remaining bits in the destination byte, store them
		if (dst_bit_offset > 0)
		{
			*dst_buffer = dst_bits;
		}

		return bits_copied + bits_to_copy;
	}
}  // namespace ov
