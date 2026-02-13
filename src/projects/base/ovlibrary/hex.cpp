//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================

#include "hex.h"

namespace ov
{
    String Hex::Encode(const std::shared_ptr<ov::Data> &data)
    {
        return Encode(data->GetData(), data->GetLength());
    }

    String Hex::Encode(const void *data, size_t length)
	{
		String hex_string(length * 2);

		const auto *buffer = static_cast<const uint8_t *>(data);

		for (size_t index = 0; index < length; index++)
		{
			hex_string.AppendFormat("%02X", *buffer);
			buffer++;
		}

		return hex_string;
	}

    std::shared_ptr<ov::Data> Hex::Decode(const ov::String &hex_string)
    {
        // if hyphen exists, remove it for uuid format
        if (hex_string.IndexOf("-") != -1)
        {
            auto hex = hex_string.Replace("-", "");
            return Decode(hex);
        }

        // and hex string is must be even
        if (hex_string.GetLength() % 2 != 0)
        {
            return nullptr;
        }

        // and hex string can not have non-hex character
        for (size_t i = 0; i < hex_string.GetLength(); i++)
        {
            auto ch = hex_string[i];
            // 0~9 , A~F or a~f
            if (!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f')))
            {
                return nullptr;
            }
        }

        auto data = std::make_shared<ov::Data>(hex_string.GetLength() / 2);
        data->SetLength(hex_string.GetLength() / 2);

        auto buffer = data->GetWritableDataAs<uint8_t>();

        for (size_t i = 0; i < hex_string.GetLength(); i += 2)
        {
            ov::String hex = hex_string.Substring(i, 2);
            uint8_t byte = (uint8_t)strtol(hex.GetBuffer(), nullptr, 16);

            buffer[i / 2] = byte;
        }

        return data;
    }
}