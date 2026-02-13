#include "nal_unit_bitstream_parser.h"

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
	value = 0;
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
            zero_bit_count++;
        }
        else
        {
            break;
        }
    }

    if (zero_bit_count > 0)
    {
        uint32_t rest;
        if (ReadBits(zero_bit_count, rest) == false)
        {
            return false;
        }

        value = (1 << zero_bit_count) - 1 + rest;
    }
    else
    {
        value = 0;
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
    uint64_t dummy;
	return ReadBits(count, dummy);
}

void NalUnitBitstreamParser::NextPosition()
{
    _position ++;

    // skip emulation_prevention_three_byte
    if (*_position == 0x03 &&
        BytesConsumed() >= 3 && 
        BytesRemained() >= 1 &&
                *(_position - 2) == 0x00 && 
                *(_position - 1) == 0x00 && 
                (*(_position + 1) | 0b11) == 0b11)
    {
        _position ++;
    }
}