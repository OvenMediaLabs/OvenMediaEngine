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
#include "../data_structure.h"

namespace http
{
	namespace hpack
	{
		class Table
		{
		public:
			bool GetHeaderField(size_t index, HeaderField &header_field)
			{
				// Get header_field from _header_fields_table
				try
				{
					header_field = _header_fields_table.at(index - 1);
					return true;
				}
				catch (const std::out_of_range &)
				{
					return false;
				}

				return false;
			}

			bool Index(const HeaderField &header_field)
			{
				if (FreeUpSpace(header_field.GetSize()) == false)
				{
					return false;
				}

				// https://datatracker.ietf.org/doc/html/rfc7541#section-4.4
				// If the size of the new entry is less than or equal to the maximum
				// size, that entry is added to the table.  It is not an error to
				// attempt to add an entry that is larger than the maximum size; an
				// attempt to add an entry larger than the maximum size causes the table
				// to be emptied of all existing entries and results in an empty table.
				if (header_field.GetSize() + _table_usage > _table_size)
				{
					return true;
				}

				if (Insert(header_field) == false)
				{
					return false;
				}

				// Since entries in the table can be deleted from the oldest,
				// keep the most up-to-date list.
				_header_field_name_sequence_map[header_field.GetName()] = _append_sequence;
				_header_field_sequence_map[header_field.GetKey()] = _append_sequence;

				logt("DEBUG", "Indexed header field: %s", header_field.ToString().CStr());

				_append_sequence++;

				_table_usage += header_field.GetSize();

				return false;
			};

			// Return: <Name indexed, Value indexed, Index Number>
			std::tuple<bool, bool, uint32_t> LookupIndex(const HeaderField &header_field)
			{
				// If name/value pair is matched in the table, return the index number.
				auto it = _header_field_sequence_map.find(header_field.GetKey().CStr());
				if (it != _header_field_sequence_map.end())
				{
					// Found {name, value} in static table
					auto sequence_number = it->second;
					auto index = CalcIndexNumber(sequence_number, _header_fields_table.size(), _removed_count);

					return {true, true, index};
				}

				// Else if only name is matched in the table, return the index number.
				it = _header_field_name_sequence_map.find(header_field.GetName().CStr());
				if (it != _header_field_name_sequence_map.end())
				{
					// Found {name, value} in table
					auto sequence_number = it->second;
					auto index = CalcIndexNumber(sequence_number, _header_fields_table.size(), _removed_count);

					// Found {only name} in table
					return {true, false, index};
				}

				return {false, false, 0};
			}

			bool UpdateTableSize(size_t size)
			{
				while (_table_usage >= size)
				{
					// Pop the oldest entry from the table.
					if (PopHeaderField() == 0)
					{
						break;
					}
				}

				_table_size = size;

				return true;
			}

			size_t GetTableSize()
			{
				return _table_size;
			}

			size_t GetTableUsage()
			{
				return _table_usage;
			}

			size_t GetNumberOfTableEntries()
			{
				return _header_fields_table.size();
			}

			size_t PopHeaderField()
			{
				if (_header_fields_table.empty())
				{
					return 0;
				}

				auto header_field = _header_fields_table.back();
				_header_fields_table.pop_back();
				
				_removed_count ++;
				_table_usage -= header_field.GetSize();

				return header_field.GetSize();
			}

		protected:
			std::deque<HeaderField> _header_fields_table;

			// Map for performance 
			// name : inserted order
			std::unordered_map<ov::String, uint32_t, ov::CaseInsensitiveHash, ov::CaseInsensitiveEqual> _header_field_name_sequence_map;
			// key(name + value) : inserted order
			std::unordered_map<ov::String, uint32_t, ov::CaseInsensitiveHash, ov::CaseInsensitiveEqual> _header_field_sequence_map;

		private:
			virtual bool Insert(const HeaderField &header_field) = 0;

			virtual uint32_t CalcIndexNumber(uint32_t sequence, uint32_t table_size, uint32_t removed_item_count) = 0;

			bool FreeUpSpace(size_t new_entry_size)
			{
				while (_table_usage + new_entry_size > _table_size)
				{
					// Pop the oldest entry from the table.
					// PopHeaderField decreases _table_usage.
					if (PopHeaderField() == 0)
					{
						break;
					}
				}

				return true;
			}

			// Initial Table Size : 4096
			size_t _table_size = 4096;
			size_t _table_usage = 0;

			// Order Sequence
			uint32_t _append_sequence = 1;

			// Removed item count
			uint32_t _removed_count = 0;
		};
	}  // namespace hpack
}  // namespace http