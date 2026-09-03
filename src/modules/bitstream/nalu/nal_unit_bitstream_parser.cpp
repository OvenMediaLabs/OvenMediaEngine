#include "nal_unit_bitstream_parser.h"

#include <algorithm>

NalUnitBitstreamParser::NalUnitBitstreamParser(const uint8_t *bitstream, size_t length)
	: BitReader(bitstream, length)
{
}

bool NalUnitBitstreamParser::ReadU8(uint8_t &value)
{
    return ReadBits(8, value);
}

bool NalUnitBitstreamParser::ReadU16(uint16_t &value)
{
    return ReadBits(16, value);
}

bool NalUnitBitstreamParser::ReadU32(uint32_t &value)
{
	return ReadBits(32, value);
}

bool NalUnitBitstreamParser::ReadUEV(uint32_t &value)
{
	value			   = 0;
	int zero_bit_count = 0;
	uint8_t bit;

	while (true)
	{
		if (ReadBit(bit) == false)
		{
			return false;
		}

		if (bit == 0)
		{
			// 31 leading zeros already put codeNum at 2^32 - 2, the maximum any `ue(v)` element takes.
			// Refusing as the run is counted avoids walking a longer one first.
			if (++zero_bit_count > 31)
			{
				return false;
			}
		}
		else
		{
			break;
		}
	}

	if (zero_bit_count > 0)
	{
		uint32_t rest;
		if (ReadBits(static_cast<uint8_t>(zero_bit_count), rest) == false)
		{
			return false;
		}

		value = (1U << zero_bit_count) - 1 + rest;
	}

	return true;
}

bool NalUnitBitstreamParser::ReadSEV(int32_t &value)
{
    uint32_t uev_value;
    if (ReadUEV(uev_value) == false)
    {
        return false;
    }

	int32_t sign = (uev_value % 2 == 0) ? -1 : 1;
    value = sign * static_cast<int32_t>((uev_value + 1) / 2);
    return true;
}

bool NalUnitBitstreamParser::Skip(uint32_t count)
{
	// `ReadBits()` takes the width as a `uint8_t` and rejects a width wider than its output.
	// Anything larger is consumed in 64 bit steps.
	while (count > 0)
	{
		const auto step = static_cast<uint8_t>(std::min<uint32_t>(count, 64));

		uint64_t dummy;
		if (ReadBits(step, dummy) == false)
		{
			return false;
		}

		count -= step;
	}

	return true;
}

void NalUnitBitstreamParser::NextPosition()
{
    _position ++;

    // skip emulation_prevention_three_byte. The bounds come first: *_position and
    // *(_position + 1) are both read below
    if (BytesRemained() >= 2 &&
        BytesConsumed() >= 3 &&
        *_position == 0x03 &&
                *(_position - 2) == 0x00 && 
                *(_position - 1) == 0x00 && 
                (*(_position + 1) | 0b11) == 0b11)
    {
        _position ++;
    }
}