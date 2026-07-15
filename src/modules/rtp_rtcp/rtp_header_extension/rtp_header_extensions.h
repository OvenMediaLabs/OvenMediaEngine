#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include "rtp_header_extension.h"

// |id|length|data|id2|length2|data2|..........|pad|pad
// Length : multiples of 4 bytes

class RtpHeaderExtensions
{
public:
	bool AddExtention(const std::shared_ptr<RtpHeaderExtension> &extension)
	{
		_extension_map.emplace(extension->GetId(), extension);
		auto data = extension->Marshal(GetHeaderType());
		if(data == nullptr)
		{
			return false;
		}
		
		_total_data_length += data->GetLength();
		return true;
	}

	size_t GetTotalDataLength() const
	{
		return _total_data_length;
	}

	std::map<uint8_t, std::shared_ptr<RtpHeaderExtension>> GetMap() const
	{
		return _extension_map;
	}

	std::shared_ptr<RtpHeaderExtension> GetExtension(uint8_t id) const
	{
		if(_extension_map.find(id) == _extension_map.end())
		{
			return nullptr;
		}

		return _extension_map.at(id);
	}
	
	RtpHeaderExtension::HeaderType GetHeaderType() const
	{
		return RtpHeaderExtension::HeaderType::TWO_BYTE_HEADER;
	}

private:
	std::map<uint8_t, std::shared_ptr<RtpHeaderExtension>> _extension_map;
	size_t _total_data_length = 0;
};