//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "decoder.h"
#include "encoder.h"
namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace oprf
			{
				struct HWAccels : public Item
				{
				protected:
					Decoder _decoder;
					Encoder _encoder;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(GetDecoder, _decoder);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetEncoder, _encoder);

				protected:
					void MakeList() override
					{
						Register<Optional>("Decoder", &_decoder);
						Register<Optional>("Encoder", &_encoder);
					}
				};
			}  // namespace oprf
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg