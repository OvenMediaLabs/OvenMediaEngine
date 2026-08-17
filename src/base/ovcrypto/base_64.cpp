//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "base_64.h"
#include "ovcrypto_private.h"

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>

namespace ov
{
	ov::String Base64::Encode(const Data &data, bool url)
	{
		if (data.GetLength() == 0L)
		{
			return "";
		}

		ov::String base64;
		base64.SetLength(Base64encode_len(data.GetLength()));
		Base64encode(base64.GetBuffer(), data.GetDataAs<char>(), data.GetLength());

		if(url == true && !base64.IsEmpty())
		{
			// TODO(Getroot): Need to optimize for performance
			base64 = base64.Replace("+", "-");
			base64 = base64.Replace("/", "_");

			// Remove '=' characters
			if(base64.GetBuffer()[base64.GetLength()-1] == '=')
			{
				if(base64.GetBuffer()[base64.GetLength()-2] == '=')
				{
					base64.SetLength(base64.GetLength() - 2);
				}
				else
				{
					base64.SetLength(base64.GetLength() - 1);
				}
			}
		}

		return base64;
	}

	ov::String Base64::Encode(const std::shared_ptr<const Data> &data, bool url)
	{
		return Encode(*data, url);
	}

	std::shared_ptr<Data> Base64::Decode(const ov::String &text, bool url)
	{
		if (text.IsEmpty())
		{
			return nullptr;
		}

		ov::String source = text;

		// TODO(Getroot): Need to optimize for performance
		if(url == true)
		{
			// replace any characters
			source = source.Replace("-", "+");
			source = source.Replace("_", "/");

			uint8_t pad_count = 4-(source.GetLength() % 4);
			if(pad_count == 4)
			{
				pad_count = 0;
			}

			for(uint8_t i = 0; i<pad_count; i++)
			{	
				source.Append("=");
			}
		}

		auto data = std::make_shared<ov::Data>();
		data->SetLength(Base64decode_len(source.GetBuffer()));
		size_t actualSize = Base64decode(data->GetWritableDataAs<char>(), source.GetBuffer());
		data->SetLength(actualSize);

		return data;
	}
}  // namespace ov
