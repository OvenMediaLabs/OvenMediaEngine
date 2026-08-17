//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "srt_stream.h"

namespace cfg
{
	namespace cmn
	{
		struct SrtStreamMap : public Item
		{
		protected:
			std::vector<SrtStream> _stream_list;

		public:
			CFG_DECLARE_CONST_REF_GETTER_OF(GetStreamList, _stream_list)

		protected:
			void MakeList() override
			{
				Register<OmitJsonName>("Stream", &_stream_list);
			}
		};
	}  // namespace cmn
}  // namespace cfg
