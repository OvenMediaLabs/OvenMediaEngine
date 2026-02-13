//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../common/options.h"
#include "../../common/ranged_port.h"
#include "../../common/srt_stream_map.h"
#include "./publisher.h"

namespace cfg
{
	namespace bind
	{
		namespace pub
		{
			struct SRT : public Publisher<cmn::RangedPort>
			{
			protected:
				cmn::Options _options;
				cmn::SrtStreamMap _stream_map;

			public:
				using Item::IsParsed;

				explicit SRT(const char *port)
					: Publisher<cmn::RangedPort>(port)
				{
				}

				CFG_DECLARE_CONST_REF_GETTER_OF(GetOptions, _options);
				CFG_DECLARE_CONST_REF_GETTER_OF(GetStreamMap, _stream_map);

			protected:
				void MakeList() override
				{
					Publisher<cmn::RangedPort>::MakeList();

					Item::Register<Optional>("Options", &_options);
					Item::Register<Optional>("StreamMap", &_stream_map);
				};
			};
		}  // namespace pub
	}  // namespace bind
}  // namespace cfg
