//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include "table.h"

namespace http
{
	namespace hpack
	{
		class DynamicTable : public Table
		{
		private:
			bool Insert(const HeaderField &header_field) override;
			uint32_t CalcIndexNumber(uint32_t sequence, uint32_t table_size, uint32_t removed_item_count) override;
		};
	} // namespace hpack
} // namespace http