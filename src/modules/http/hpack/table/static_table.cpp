//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================

#include "static_table.h"

namespace http
{
	namespace hpack
	{
		StaticTable::StaticTable()
		{
			// Static Table Usage is 2636 octets so we can use it as a table size(4096)

			// https://www.rfc-editor.org/rfc/rfc7541.html#appendix-A
			// The index number starts from 1. There are up to 61 static tables.
			Index(HeaderField(":authority", ""));
			Index(HeaderField(":method", "GET"));
			Index(HeaderField(":method", "POST"));
			Index(HeaderField(":path", "/"));
			Index(HeaderField(":path", "/index.html"));
			Index(HeaderField(":scheme", "http"));
			Index(HeaderField(":scheme", "https"));
			Index(HeaderField(":status", "200"));
			Index(HeaderField(":status", "204"));
			Index(HeaderField(":status", "206"));
			Index(HeaderField(":status", "304"));
			Index(HeaderField(":status", "400"));
			Index(HeaderField(":status", "404"));
			Index(HeaderField(":status", "500"));
			Index(HeaderField("accept-charset", ""));
			Index(HeaderField("accept-encoding", "gzip, deflate"));
			Index(HeaderField("accept-language", ""));
			Index(HeaderField("accept-ranges", ""));
			Index(HeaderField("accept", ""));
			Index(HeaderField("access-control-allow-origin", ""));
			Index(HeaderField("age", ""));
			Index(HeaderField("allow", ""));
			Index(HeaderField("authorization", ""));
			Index(HeaderField("cache-control", ""));
			Index(HeaderField("content-disposition", ""));
			Index(HeaderField("content-encoding", ""));
			Index(HeaderField("content-language", ""));
			Index(HeaderField("content-length", ""));
			Index(HeaderField("content-location", ""));
			Index(HeaderField("content-range", ""));
			Index(HeaderField("content-type", ""));
			Index(HeaderField("cookie", ""));
			Index(HeaderField("date", ""));
			Index(HeaderField("etag", ""));
			Index(HeaderField("expect", ""));
			Index(HeaderField("expires", ""));
			Index(HeaderField("from", ""));
			Index(HeaderField("host", ""));
			Index(HeaderField("if-match", ""));
			Index(HeaderField("if-modified-since", ""));
			Index(HeaderField("if-none-match", ""));
			Index(HeaderField("if-range", ""));
			Index(HeaderField("if-unmodified-since", ""));
			Index(HeaderField("last-modified", ""));
			Index(HeaderField("link", ""));
			Index(HeaderField("location", ""));
			Index(HeaderField("max-forwards", ""));
			Index(HeaderField("proxy-authenticate", ""));
			Index(HeaderField("proxy-authorization", ""));
			Index(HeaderField("range", ""));
			Index(HeaderField("referer", ""));
			Index(HeaderField("refresh", ""));
			Index(HeaderField("retry-after", ""));
			Index(HeaderField("server", ""));
			Index(HeaderField("set-cookie", ""));
			Index(HeaderField("strict-transport-security", ""));
			Index(HeaderField("transfer-encoding", ""));
			Index(HeaderField("user-agent", ""));
			Index(HeaderField("vary", ""));
			Index(HeaderField("via", ""));
			Index(HeaderField("www-authenticate", ""));
		}

		bool StaticTable::Insert(const HeaderField &header_field)
		{
			// StaticTable does not change index number of header_field
			_header_fields_table.push_back(header_field);
			return true;
		}

		uint32_t StaticTable::CalcIndexNumber(uint32_t sequence, uint32_t table_size, uint32_t removed_item_count)
		{
			// StaticTable does not remove any item so sequence number is same as index number
			return sequence;
		}
	}
}