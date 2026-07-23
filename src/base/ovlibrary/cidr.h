#pragma once

#include "base/common_types.h"

namespace ov
{
	class CIDR
	{
	public:
		static std::shared_ptr<CIDR> Parse(const ov::String &str_cidr)
		{
			auto cidr = std::make_shared<CIDR>();
			if(cidr->ParseInternal(str_cidr) == false)
			{
				return nullptr;
			}

			return cidr;
		}

		bool CheckIP(const ov::String &str_ip_addr)
		{
			uint32_t ui32_ip_addr;
			if(inet_pton(AF_INET, str_ip_addr.CStr(), &ui32_ip_addr) != 1)
			{
				return false;
			}

			return ui32_ip_addr >= _ui32_network_address && ui32_ip_addr <= _ui32_broadcast_address;
		}

		ov::String Begin()
		{
			return _str_network_address;
		}

		ov::String End()
		{
			return _str_broadcast_address;
		}

	private:
		bool ParseInternal(const ov::String &str_cidr)
		{
			_cidr = str_cidr;

			auto temp = str_cidr.Split("/");
			if(temp.size() != 2)
			{
				return false;
			}

			_str_ip_addr = temp[0];
			_str_bitmask = temp[1];

			if(inet_pton(AF_INET, _str_ip_addr.CStr(), &_ui32_ip_addr) != 1)
			{
				return false;
			}

			// Validation
			if(std::stoi(_str_bitmask.CStr()) <= 0 || std::stoi(_str_bitmask.CStr()) > 32)
			{
				return false;
			}

			_ui32_bitmask = CreateBitmask(_str_bitmask);

			_ui32_network_address = (_ui32_ip_addr & _ui32_bitmask);
			_ui32_broadcast_address = (_ui32_ip_addr | ~_ui32_bitmask);

			// To string
			char buf[20];
			if(inet_ntop(AF_INET, &_ui32_network_address, buf, sizeof(buf)) == nullptr)
			{
				return false;
			}
			_str_network_address = buf;

			if(inet_ntop(AF_INET, &_ui32_broadcast_address, buf, sizeof(buf)) == nullptr)
			{
				return false;
			}
			_str_broadcast_address = buf;

			return true;
		}

		uint32_t CreateBitmask(const ov::String &str_bitmask)
		{
			uint32_t times = (uint32_t)std::stoi(str_bitmask.CStr())-1;
			uint32_t i;
			uint32_t ui32_bitmask = 1;

			for(i=0; i<times; ++i)
			{
				ui32_bitmask <<= 1;
				ui32_bitmask |= 1;
			}

			for(i=0; i<32-times-1; ++i)
			{
				ui32_bitmask <<= 1;
			}

			return ntohl(ui32_bitmask);
		}

		ov::String	_cidr;
		uint32_t	_ui32_ip_addr = 0;
		uint32_t	_ui32_bitmask = 0;
		ov::String	_str_ip_addr;
		ov::String	_str_bitmask;
		
		uint32_t	_ui32_network_address = 0;	// Begin
		uint32_t	_ui32_broadcast_address = 0; // End
		ov::String	_str_network_address;	// Begin
		ov::String	_str_broadcast_address; // End
	};
}