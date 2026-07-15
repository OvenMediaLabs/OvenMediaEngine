//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================

#include "table_connector.h"

namespace http
{
	namespace hpack
	{
		bool TableConnector::GetHeaderField(size_t index, HeaderField &header_field)
		{
			// StaticTable : 1 ~ 61
			if (index < _static_table->GetNumberOfTableEntries())
			{
				return _static_table->GetHeaderField(index, header_field);
			}

			// lock 
			std::lock_guard<std::mutex> lock(_dynamic_table_lock);
			// DynamicTable 62 ~ (2^32 - 1)
			return _dynamic_table->GetHeaderField(index - _static_table->GetNumberOfTableEntries(), header_field);
		}

		bool TableConnector::Index(const HeaderField &header_field)
		{
			std::lock_guard<std::mutex> lock(_dynamic_table_lock);
			// Only can index to DynamicTable
			return _dynamic_table->Index(header_field);
		}

		std::tuple<bool, bool, uint32_t> TableConnector::LookupIndex(const HeaderField &header_field)
		{
			auto [static_name_indexed, static_value_indexed, static_index] = _static_table->LookupIndex(header_field);
			if (static_name_indexed == true && static_value_indexed == true)
			{
				return {true, true, static_index};
			}

			std::unique_lock<std::mutex> lock(_dynamic_table_lock);
			auto [dynamic_name_indexed, dynamic_value_indexed, dynamic_index] = _dynamic_table->LookupIndex(header_field);
			if (dynamic_name_indexed == true && dynamic_value_indexed == true)
			{
				return {true, true, dynamic_index + _static_table->GetNumberOfTableEntries()};
			}
			lock.unlock();

			if (static_name_indexed == true)
			{
				return {true, false, static_index};
			}
			else if (dynamic_name_indexed == true)
			{
				return {true, false, dynamic_index + _static_table->GetNumberOfTableEntries()};
			}

			return {false, false, 0};
		}

		bool TableConnector::UpdateDynamicTableSize(size_t size)
		{
			std::lock_guard<std::mutex> lock(_dynamic_table_lock);
			return _dynamic_table->UpdateTableSize(size);
		}

		size_t TableConnector::GetDynamicTableSize()
		{
			std::lock_guard<std::mutex> lock(_dynamic_table_lock);
			return _dynamic_table->GetTableSize();
		}
	}
}