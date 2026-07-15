//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <base/ovsocket/socket_address.h>

namespace ov
{
	class AddressUtilities : public ov::Singleton<AddressUtilities>
	{
	public:
		// Get mapped IP address from set stun server
		bool ResolveMappedAddress(const ov::String &stun_server_addr);
		bool ResolveMappedAddress(const std::vector<ov::SocketAddress> &stun_server_addr_list);

		// Get resolved mapped address
		std::vector<ov::SocketAddress> GetMappedAddressList() const;

		// Get all IP addresses
		std::vector<String> GetIPv4List(bool include_mapped_address = true);
		std::vector<String> GetIPv6List(bool include_link_local_address, bool include_mapped_address = true);

		// A wrapper if inet_pton()
		MAY_THROWS(ov::SocketAddressError)
		static void InetPton(int address_family, const char *__restrict address, void *__restrict __buf);

	protected:
		std::vector<String> GetIPListInternal(ov::SocketFamily family, bool include_link_local_address, bool include_mapped_address);

	private:
		std::vector<ov::SocketAddress> _mapped_address_list;
	};
}  // namespace ov