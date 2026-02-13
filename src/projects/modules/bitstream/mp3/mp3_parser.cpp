//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================

#include "mp3_parser.h"

#include <base/ovlibrary/bit_reader.h>
#include <base/ovlibrary/log.h>
#include <base/ovlibrary/ovlibrary.h>

#include <memory>

#define OV_LOG_TAG "MP3Parser"

bool MP3Parser::IsValid(const uint8_t *data, size_t data_length)
{
	if (data == nullptr || data_length < 4)
	{
		return false;
	}

	return true;
}

bool MP3Parser::Parse(const uint8_t *data, size_t data_length, MP3Parser &parser)
{
	BitReader reader(data, data_length);

    auto syncword = reader.ReadBits<uint16_t>(11); // syncword, must be 0x7FF
    if (syncword != 0x7FF)
    {
        logte("MP3Parser::Parse() - syncword is not 0x7FF");
        return false;
    }

    // MPEG Audio version ID
    // 00 - MPEG Version 2.5 (later extension of MPEG 2)
    // 01 - reserved
    // 10 - MPEG Version 2 (ISO/IEC 13818-3)
    // 11 - MPEG Version 1 (ISO/IEC 11172-3)  
    parser._version_id = reader.ReadBits<uint8_t>(2);

    // Layer description
    // 00 - reserved
    // 01 - Layer III
    // 10 - Layer II
    // 11 - Layer I    
    parser._layer_id = reader.ReadBits<uint8_t>(2);

    // Protection bit
    // 0 - Protected by CRC (16bit CRC follows header)
    // 1 - Not protected
    parser._protection_bit = reader.ReadBits<uint8_t>(1);

    // bitrate_index
    parser._bitrate_index = reader.ReadBits<uint8_t>(4);

    // sampling_frequency
    parser._sampling_frequency = reader.ReadBits<uint8_t>(2);

    // padding_bit
    parser._padding_bit = reader.ReadBits<uint8_t>(1);

    // private_bit
    parser._private_bit = reader.ReadBits<uint8_t>(1);

    // channel_mode
    parser._channel_mode = reader.ReadBits<uint8_t>(2);

    // mode_extension
    parser._mode_extension = reader.ReadBits<uint8_t>(2);

    // copy_right
    parser._copy_right = reader.ReadBits<uint8_t>(1);

    // original
    parser._original = reader.ReadBits<uint8_t>(1);

    // emphasis
    parser._emphasis = reader.ReadBits<uint8_t>(2);
    

	return true;
}

uint32_t MP3Parser::GetBitrate()
{
    if (_bitrate > 0)
    {
        return _bitrate;
    }

    uint8_t version = GetVersion();
    uint8_t layer = GetLayer();

    // V2 means Version 2 and Version 2.5
    if (version == 2.5)
    {
        version = 2;
    }

    // If the version is 2, the layer 2 & 3 has same bitrate table
    if (version == 2)
    {
        if (layer == 3)
        {
            layer = 2;
        }
    }

    if (version == 2 && layer == 2) // V2, L2&L3
    {
        switch (_bitrate_index)
        {
        case 0b0000: _bitrate = 0; break;
        case 0b0001: _bitrate = 8 * 1000; break;
        case 0b0010: _bitrate = 16 * 1000; break;
        case 0b0011: _bitrate = 24 * 1000; break;
        case 0b0100: _bitrate = 32 * 1000; break;
        case 0b0101: _bitrate = 40 * 1000; break;
        case 0b0110: _bitrate = 48 * 1000; break;
        case 0b0111: _bitrate = 56 * 1000; break;
        case 0b1000: _bitrate = 64 * 1000; break;
        case 0b1001: _bitrate = 80 * 1000; break;
        case 0b1010: _bitrate = 96 * 1000; break;
        case 0b1011: _bitrate = 112 * 1000; break;
        case 0b1100: _bitrate = 128 * 1000; break;
        case 0b1101: _bitrate = 144 * 1000; break;
        case 0b1110: _bitrate = 160 * 1000; break;
        case 0b1111: default: _bitrate = 0; break; // bad
        }
    }
    else if (version == 2 && layer == 1) // V2, L1
    {
        switch(_bitrate_index)
        {
        case 0b0000: _bitrate = 0; break;
        case 0b0001: _bitrate = 32 * 1000; break;
        case 0b0010: _bitrate = 48 * 1000; break;
        case 0b0011: _bitrate = 56 * 1000; break;
        case 0b0100: _bitrate = 64 * 1000; break;
        case 0b0101: _bitrate = 80 * 1000; break;
        case 0b0110: _bitrate = 96 * 1000; break;
        case 0b0111: _bitrate = 112 * 1000; break;
        case 0b1000: _bitrate = 128 * 1000; break;
        case 0b1001: _bitrate = 144 * 1000; break;
        case 0b1010: _bitrate = 160 * 1000; break;
        case 0b1011: _bitrate = 176 * 1000; break;
        case 0b1100: _bitrate = 192 * 1000; break;
        case 0b1101: _bitrate = 224 * 1000; break;
        case 0b1110: _bitrate = 256 * 1000; break;
        case 0b1111: default: _bitrate = 0; break; // bad
        }
    }
    else if (version == 1 && layer == 3) // V1, L3
    {
        switch (_bitrate_index)
        {
        case 0b0000: _bitrate = 0; break;
        case 0b0001: _bitrate = 32 * 1000; break;
        case 0b0010: _bitrate = 40 * 1000; break;
        case 0b0011: _bitrate = 48 * 1000; break;
        case 0b0100: _bitrate = 56 * 1000; break;
        case 0b0101: _bitrate = 64 * 1000; break;
        case 0b0110: _bitrate = 80 * 1000; break;
        case 0b0111: _bitrate = 96 * 1000; break;
        case 0b1000: _bitrate = 112 * 1000; break;
        case 0b1001: _bitrate = 128 * 1000; break;
        case 0b1010: _bitrate = 160 * 1000; break;
        case 0b1011: _bitrate = 192 * 1000; break;
        case 0b1100: _bitrate = 224 * 1000; break;
        case 0b1101: _bitrate = 256 * 1000; break;
        case 0b1110: _bitrate = 320 * 1000; break;
        case 0b1111: default: _bitrate = 0; break; // bad
        }
    }
    else if (version == 1 && layer == 2) // V1, L2
    {
        switch (_bitrate_index)
        {
        case 0b0000: _bitrate = 0; break;
        case 0b0001: _bitrate = 32 * 1000; break;
        case 0b0010: _bitrate = 48 * 1000; break;
        case 0b0011: _bitrate = 56 * 1000; break;
        case 0b0100: _bitrate = 64 * 1000; break;
        case 0b0101: _bitrate = 80 * 1000; break;
        case 0b0110: _bitrate = 96 * 1000; break;
        case 0b0111: _bitrate = 112 * 1000; break;
        case 0b1000: _bitrate = 128 * 1000; break;
        case 0b1001: _bitrate = 160 * 1000; break;
        case 0b1010: _bitrate = 192 * 1000; break;
        case 0b1011: _bitrate = 224 * 1000; break;
        case 0b1100: _bitrate = 256 * 1000; break;
        case 0b1101: _bitrate = 320 * 1000; break;
        case 0b1110: _bitrate = 384 * 1000; break;
        case 0b1111: default: _bitrate = 0; break; // bad
        }
    }
    else if (version == 1 && layer == 1) // V1, L1
    {
        switch (_bitrate_index)
        {
        case 0b0000: _bitrate = 0; break;
        case 0b0001: _bitrate = 32 * 1000; break;
        case 0b0010: _bitrate = 64 * 1000; break;
        case 0b0011: _bitrate = 96 * 1000; break;
        case 0b0100: _bitrate = 128 * 1000; break;
        case 0b0101: _bitrate = 160 * 1000; break;
        case 0b0110: _bitrate = 192 * 1000; break;
        case 0b0111: _bitrate = 224 * 1000; break;
        case 0b1000: _bitrate = 256 * 1000; break;
        case 0b1001: _bitrate = 288 * 1000; break;
        case 0b1010: _bitrate = 320 * 1000; break;
        case 0b1011: _bitrate = 352 * 1000; break;
        case 0b1100: _bitrate = 384 * 1000; break;
        case 0b1101: _bitrate = 416 * 1000; break;
        case 0b1110: _bitrate = 448 * 1000; break;
        case 0b1111: default: _bitrate = 0; break; // bad
        }
    }

    return _bitrate;
}

uint32_t MP3Parser::GetSampleRate()
{
    if (_sampling_rate > 0)
    {
        return _sampling_rate;
    }

    auto version = GetVersion();

    if (version == 1)
    {
        switch (_sampling_frequency)
        {
        case 0b00: _sampling_rate = 44100; break;
        case 0b01: _sampling_rate = 48000; break;
        case 0b10: _sampling_rate = 32000; break;
        case 0b11: default: _sampling_rate = 0; break; // bad
        }
    }
    else if (version == 2)
    {
        switch (_sampling_frequency)
        {
        case 0b00: _sampling_rate = 22050; break;
        case 0b01: _sampling_rate = 24000; break;
        case 0b10: _sampling_rate = 16000; break;
        case 0b11: default: _sampling_rate = 0; break; // bad
        }
    }
    else if (version == 2.5)
    {
        switch (_sampling_frequency)
        {
        case 0b00: _sampling_rate = 11025; break;
        case 0b01: _sampling_rate = 12000; break;
        case 0b10: _sampling_rate = 8000; break;
        case 0b11: default: _sampling_rate = 0; break; // bad
        }
    }

    return _sampling_rate;
}

uint8_t MP3Parser::GetChannelCount()
{
    if (_channel_count > 0)
    {
        return _channel_count;
    }

    switch (_channel_mode)
    {
        case 0b00: _channel_count = 2; break; // stereo
        case 0b01: _channel_count = 2; break; // joint stereo
        case 0b10: _channel_count = 2; break; // dual channel
        case 0b11: _channel_count = 1; break; // mono
    }

    return _channel_count;
}

double MP3Parser::GetVersion()
{
    switch (_version_id)
    {
        case 0b00: return 2.5;
        case 0b10: return 2;
        case 0b11: return 1;
        default: return 0;
    }

    return 0;
}

uint8_t MP3Parser::GetLayer()
{
    switch (_layer_id)
    {
        case 0b01: return 3;
        case 0b10: return 2;
        case 0b11: return 1;
        default: return 0;
    
    }
}

ov::String MP3Parser::GetInfoString()
{
    ov::String output_string;

    output_string.FormatString("\n[MP3Parser]\n");
    output_string.AppendFormat("version: %.2f\n", GetVersion());
    output_string.AppendFormat("layer: %d\n", GetLayer());
    // bitrate
    output_string.AppendFormat("bitrate_index: %d\n", _bitrate_index);
    output_string.AppendFormat("bitrate: %d\n", GetBitrate());
    // sampling rate
    output_string.AppendFormat("sampling_frequency: %d\n", _sampling_frequency);
    output_string.AppendFormat("sampling_rate: %d\n", GetSampleRate());
    // channel count
    output_string.AppendFormat("channel_mode: %d\n", _channel_mode);
    output_string.AppendFormat("channel_count: %d\n", GetChannelCount());

    return output_string;
}