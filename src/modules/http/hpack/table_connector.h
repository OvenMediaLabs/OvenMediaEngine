//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "table/static_table.h"
#include "table/dynamic_table.h"

namespace http
{
	namespace hpack
	{
		class TableConnector
		{
		public:
			bool GetHeaderField(size_t index, HeaderField &header_field);
			bool Index(const HeaderField &header_field);

			// name_indexed, value_indexed, index
			std::tuple<bool, bool, uint32_t> LookupIndex(const HeaderField &header_field);
			bool UpdateDynamicTableSize(size_t size);
			size_t GetDynamicTableSize();
			
		private:
			// StaticTable is singleton instance
			StaticTable* _static_table = StaticTable::GetInstance();
			std::shared_ptr<DynamicTable> _dynamic_table = std::make_shared<DynamicTable>();

			// The dynamic table must be atomic
			std::mutex _dynamic_table_lock;
		};
	}
}