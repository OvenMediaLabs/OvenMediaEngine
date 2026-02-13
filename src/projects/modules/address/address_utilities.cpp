//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "address_utilities.h"

#include <ifaddrs.h>
#include <modules/ice/stun_client.h>

#define OV_LOG_TAG "AddressUtilities"

namespace ov
{
	bool AddressUtilities::ResolveMappedAddress(const ov::String &stun_server_addr)
	{
		std::vector<ov::SocketAddress> address_list;

		try
		{
			address_list = ov::SocketAddress::Create(stun_server_addr);
		}
		catch (const ov::Error &e)
		{
			logte("Could not resolve address: %s, %s", stun_server_addr.CStr(), e.What());
			return false;
		}

		return ResolveMappedAddress(address_list);
	}

	bool AddressUtilities::ResolveMappedAddress(const std::vector<ov::SocketAddress> &stun_server_addr_list)
	{
		ov::SocketAddress mapped_address;

		for (const auto &stun_server_addr : stun_server_addr_list)
		{
			if (StunClient::GetMappedAddress(stun_server_addr, mapped_address))
			{
				_mapped_address_list.push_back(mapped_address);
			}
		}

		return _mapped_address_list.empty() == false;
	}

	std::vector<ov::SocketAddress> AddressUtilities::GetMappedAddressList() const
	{
		return _mapped_address_list;
	}

	std::vector<ov::String> AddressUtilities::GetIPListInternal(ov::SocketFamily family, bool include_link_local_address, bool include_mapped_address)
	{
		std::vector<ov::String> list;

		if (include_mapped_address)
		{
			for (const auto &mapped_address : _mapped_address_list)
			{
				if (mapped_address.GetFamily() == family)
				{
					list.emplace_back(mapped_address.GetIpAddress());
				}
			}
		}

		struct ifaddrs *interfaces = nullptr;
		int result = ::getifaddrs(&interfaces);

		if (result == 0)
		{
			struct ifaddrs *temp_addr = interfaces;

			while (temp_addr != nullptr)
			{
				// temp_addr->ifa_addr may be nullptr when the interface doesn't have any address
				if (temp_addr->ifa_addr != nullptr)
				{
					const auto addr_family = temp_addr->ifa_addr->sa_family;

					if (addr_family == static_cast<int>(family))
					{
						switch (family)
						{
							case ov::SocketFamily::Inet: {
								auto addr = ToSockAddrIn4(temp_addr->ifa_addr)->sin_addr;

								// 0x0100007F = 127.0.0.1
								if (addr.s_addr != 0x0100007F)
								{
									char buffer[INET_ADDRSTRLEN];
									::inet_ntop(addr_family, &addr, buffer, INET_ADDRSTRLEN);

									list.emplace_back(buffer);
								}

								break;
							}

							case ov::SocketFamily::Inet6: {
								auto addr = ToSockAddrIn6(temp_addr->ifa_addr);
								auto addr6 = &(addr->sin6_addr);
								auto is_link_local_address = IN6_IS_ADDR_LINKLOCAL(addr6);

								if (::memcmp(addr6, &in6addr_loopback, sizeof(in6addr_loopback)) == 0)
								{
									// Exclude loopback addresses
									break;
								}

								// The address doesn't indicates loopback address
								char buffer[INET6_ADDRSTRLEN];
								::inet_ntop(addr_family, addr6, buffer, INET6_ADDRSTRLEN);

								if (is_link_local_address)
								{
									if (include_link_local_address)
									{
										// Append scope id for link-local address
										ov::String address(buffer);
										address.AppendFormat("%%%d", addr->sin6_scope_id);
										list.emplace_back(address);
									}
									else
									{
										// Don't include link local address for IPv6
									}
								}
								else
								{
									list.emplace_back(buffer);
								}
								break;
							}

							default:
								break;
						}
					}
				}

				temp_addr = temp_addr->ifa_next;
			}

			::freeifaddrs(interfaces);
		}

		return list;
	}

	std::vector<String> AddressUtilities::GetIPv4List(bool include_mapped_address)
	{
		return GetIPListInternal(ov::SocketFamily::Inet, false, include_mapped_address);
	}

	std::vector<String> AddressUtilities::GetIPv6List(bool include_link_local_address, bool include_mapped_address)
	{
		return GetIPListInternal(ov::SocketFamily::Inet6, include_link_local_address, include_mapped_address);
	}

	void AddressUtilities::InetPton(int address_family, const char *__restrict address, void *__restrict __buf)
	{
		const int result = ::inet_pton(address_family, address, __buf);

		switch (result)
		{
			case 1:
				// succeeded
				return;

			case 0:
				throw SocketAddressError("Invalid address: %s", address);

			case -1:
				throw SocketAddressError(ov::Error::CreateErrorFromErrno(), "Could not convert address: %s", address);

			default:
				throw SocketAddressError("Could not convert address: %s (%d)", address, result);
		}
	}
}  // namespace ov