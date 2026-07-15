//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================

#include "dynamic_table.h"

namespace http
{
	namespace hpack
	{
		bool DynamicTable::Insert(const HeaderField &header_field)
		{
			_header_fields_table.push_front(header_field);
			return true;
		}

		uint32_t DynamicTable::CalcIndexNumber(uint32_t sequence, uint32_t table_size, uint32_t removed_item_count)
		{
			////////////////
			// How to find index by header_field for DynamicTable
			// Since a new item is input to the front of the table (first-in, first-out order),
			// the index of the item increases by one each time a new item is input.
			// Therefore, the index can be calculated as follows using the input order and table size.
			// 
			// index = table.size() - (inserted order number - removed item count)

			return (table_size - (sequence - removed_item_count)) + 1;;
		}
	}
}