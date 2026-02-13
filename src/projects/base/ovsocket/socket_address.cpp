//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "socket_address.h"

#include <config/config_manager.h>
#include <netdb.h>

#include <algorithm>

#include "../ovlibrary/assert.h"
#include "../ovlibrary/byte_ordering.h"
#include "../ovlibrary/converter.h"
#include "../ovlibrary/ovlibrary.h"
#include "./ipv6_support.h"
#include "./socket_private.h"

namespace ov
{
	ov::Regex g_port_range_regex = ov::Regex::CompiledRegex("^[0-9-,]+$");

	std::vector<SocketAddress::PortRange> SocketAddress::ParsePort(const ov::String &string)
	{
		std::vector<SocketAddress::PortRange> range_list;

		if (string.IsEmpty() == false)
		{
			auto port_groups = string.Split(",");

			for (const auto &port_group : port_groups)
			{
				auto group = port_group.Trim();

				// Check whether port_part is ranged port
				auto tokens = group.Split("-");

				if (tokens.size() > 2)
				{
					throw SocketAddressError("Invalid port range format: %s", string.CStr());
				}

				if (
					(tokens[0].IsNumeric() == false) ||
					((tokens.size() > 1) && (tokens[1].IsNumeric() == false)))
				{
					throw SocketAddressError("Invalid port range: %s (The port number must be numeric)", string.CStr());
				}

				auto start_port = ov::Converter::ToInt64(tokens[0]);
				auto end_port = (tokens.size() == 2) ? ov::Converter::ToInt64(tokens[1]) : start_port;

				if (start_port > end_port)
				{
					throw SocketAddressError("Invalid port range: %s (The end port number must be greater than the start port number)", string.CStr());
				}

				if (
					(start_port < 0LL) || (start_port >= UINT16_MAX) ||
					(end_port < 0LL) || (end_port >= UINT16_MAX))
				{
					throw SocketAddressError("Invalid port number: %s (The port number must be a value between 0 and %d)", string.CStr(), UINT16_MAX);
				}

				range_list.emplace_back(start_port, end_port);
			}
		}
		else
		{
			range_list.emplace_back(0, 0);
		}

		return range_list;
	}

	SocketAddress::Address SocketAddress::ParseAddress(const ov::String &string)
	{
		const auto separator = string.IndexOfRev(':');

		ov::String port_part;

		if (separator >= 0)
		{
			auto host_part = string.Substring(0, separator);

			if ((string.Get(0) == '[') && (string.Get(separator - 1) == ']'))
			{
				// string is a form of [xxx]:yyyy

				// IPv6 + Port
				host_part = host_part.Substring(1, separator - 2);

				port_part = string.Substring(separator + 1);
			}
			else if (host_part.IndexOf(':') >= 0)
			{
				// string if a form of xxx:yyy:zzz... (without port))

				// IPv6 Only (or invalid format)
				host_part = string;
			}
			else
			{
				// string is a form of xxx:yyyy

				// IPv4/Domain + Port
				port_part = string.Substring(separator + 1);
			}

			return Address{host_part, ParsePort(port_part)};
		}

		// There is no ':' in the string, So try to parse it as a port number
		if (g_port_range_regex.Matches(string).IsMatched())
		{
			// The ```string``` consists only of a port number or a range of port numbers
			return Address{"", ParsePort(string)};
		}

		// If failed, string consists only of a host name or an IP address
		return Address{string, {PortRange{0, 0}}};
	}

	void SocketAddress::CreateInternal(const ov::String &host, uint16_t port, std::vector<SocketAddress> *address_list)
	{
		StorageList storage_list;
		bool is_wildcard_host = false;

		ov::String new_host = host;

		if (host.HasPrefix('[') && host.HasSuffix(']'))
		{
			new_host = host.Substring(1, host.GetLength() - 2);
		}

		if (Resolve(new_host, &storage_list, &is_wildcard_host))
		{
			for (const auto &storage : storage_list)
			{
				SocketAddress address(new_host, storage);

				address.SetHostname(new_host);
				address.SetPort(port);
				address._is_wildcard_host = is_wildcard_host;

				address_list->push_back(std::move(address));
			}
		}
		else
		{
			// Could not obtain IP address from the host
			SocketAddress address;

			address.SetHostname(new_host);
			address.SetPort(port);

			address_list->push_back(std::move(address));
		}
	}

	void SocketAddress::CreateInternal(const std::vector<ov::String> &host_list, uint16_t port, std::vector<SocketAddress> *address_list)
	{
		for (const auto &host : host_list)
		{
			StorageList storage_list;
			bool is_wildcard_host;

			ov::String new_host = host;

			if (host.HasPrefix('[') && host.HasSuffix(']'))
			{
				new_host = host.Substring(1, host.GetLength() - 2);
			}

			Resolve(new_host, &storage_list, &is_wildcard_host);

			for (const auto &storage : storage_list)
			{
				SocketAddress address(new_host, storage);
				address.SetPort(port);
				address._is_wildcard_host = is_wildcard_host;

				address_list->push_back(std::move(address));
			}
		}
	}

	std::vector<SocketAddress> SocketAddress::Create(const ov::String &string)
	{
		std::vector<SocketAddress> address_list;

		ParseAddress(string).EachPort([&](const ov::String &host, const uint16_t port) -> bool {
			CreateInternal(host, port, &address_list);
			return true;
		});

		return address_list;
	}

	std::vector<SocketAddress> SocketAddress::Create(const std::vector<ov::String> &host_list, uint16_t port)
	{
		std::vector<SocketAddress> address_list;
		CreateInternal(host_list, port, &address_list);
		return address_list;
	}

	std::vector<SocketAddress> SocketAddress::Create(const ov::String &host, uint16_t port)
	{
		std::vector<SocketAddress> address_list;
		CreateInternal(host, port, &address_list);
		return address_list;
	}

	std::vector<SocketAddress> SocketAddress::Create(const ov::String &host, const std::vector<int> &port_list)
	{
		std::vector<SocketAddress> address_list;

		for (const auto &port : port_list)
		{
			CreateInternal(host, static_cast<uint16_t>(port), &address_list);
		}

		return address_list;
	}

	std::vector<SocketAddress> SocketAddress::Create(const std::vector<ov::String> &host_list, const std::vector<int> &port_list)
	{
		std::vector<SocketAddress> address_list;

		for (const auto &port : port_list)
		{
			CreateInternal(host_list, static_cast<uint16_t>(port), &address_list);
		}

		return address_list;
	}

	SocketAddress SocketAddress::CreateAndGetFirst(const ov::String &string)
	{
		try
		{
			auto address_list = Create(string);

			if (address_list.empty() == false)
			{
				return address_list.front();
			}
		}
		catch (const ov::Error &e)
		{
			logte("Could not create SocketAddress from string: %s: %s", string.CStr(), e.What());
		}

		return {};
	}

	SocketAddress SocketAddress::CreateAndGetFirst(const ov::String &host, uint16_t port)
	{
		try
		{
			auto address_list = Create(host, port);

			if (address_list.empty() == false)
			{
				return address_list.front();
			}
		}
		catch (const ov::Error &e)
		{
			logte("Could not create SocketAddress from host: %s, port: %d: %s", host.CStr(), port, e.What());
		}

		return {};
	}

	RaiiPtr<addrinfo> SocketAddress::GetAddrInfo(const ov::String &host)
	{
		addrinfo *info = nullptr;

		int result = ::getaddrinfo(host, nullptr, nullptr, &info);

		if (result != 0)
		{
			switch (result)
			{
				case EAI_NONAME:
					// NAME or SERVICE is unknown
					throw ov::SocketAddressError("Name or service is unknown: \"%s\"", host.CStr());

				case EAI_AGAIN:
					// Temporary failure in name resolution (Is the DNS not working?)
					throw ov::SocketAddressError("Temporary failure in name resolution: \"%s\"", host.CStr());

				default:
					throw ov::SocketAddressError("Could not resolve name: \"%s\" (%d)", host.CStr(), result);
			}
		}

		if (info != nullptr)
		{
			return {info, ::freeaddrinfo};
		}

		OV_ASSERT2(info != nullptr);
		throw ov::SocketAddressError(ov::Error::CreateErrorFromErrno(), "getaddrinfo() returns invalid value: %s", host.CStr());
	}

	bool SocketAddress::Resolve(ov::String host, SocketAddress::StorageList *storage_list, bool *is_wildcard_host)
	{
		const auto ipv4_supported = ov::ipv6::Checker::GetInstance()->IsIPv4Supported();
		const auto ipv6_supported = ov::ipv6::Checker::GetInstance()->IsIPv6Supported();

		if (host.IsEmpty())
		{
			if (ipv4_supported)
			{
				if (Resolve(OV_SOCKET_WILDCARD_IPV4, storage_list, is_wildcard_host) == false)
				{
					return false;
				}
			}

			if (ipv6_supported)
			{
				if (Resolve(OV_SOCKET_WILDCARD_IPV6, storage_list, is_wildcard_host) == false)
				{
					return false;
				}
			}

			OV_ASSERT2(*is_wildcard_host == true);
			return true;
		}

		if (host == OV_SOCKET_WILDCARD_IPV4)
		{
			if (ipv4_supported == false)
			{
				logtw("The wildcard \"" OV_SOCKET_WILDCARD_IPV4 "\" is used, but IPv4 is not supported");
				return true;
			}

			// IPv4: INADDR_ANY
			host = "0.0.0.0";
			*is_wildcard_host = true;
		}
		else if (host == OV_SOCKET_WILDCARD_IPV6)
		{
			if (ipv6_supported == false)
			{
				logtw("The wildcard \"" OV_SOCKET_WILDCARD_IPV6 "\" is used, but IPv6 is not supported");
				return true;
			}

			// IPv6: in6addr_any
			*is_wildcard_host = true;
		}
		else
		{
			*is_wildcard_host = false;
		}

		auto addr_info = GetAddrInfo(host);

		if (addr_info == nullptr)
		{
			OV_ASSERT2(addr_info != nullptr);
			return false;
		}

		sockaddr_storage storage;
		const addrinfo *item = addr_info;
		ov::String description;

		while (item != nullptr)
		{
			::memset(&storage, 0, sizeof(storage));
			storage.ss_family = item->ai_family;

			if (item->ai_addr != nullptr)
			{
				switch (item->ai_family)
				{
					case AF_INET:
						if (ipv4_supported)
						{
							*ov::ToSockAddrIn4(&storage) = *ov::ToSockAddrIn4(item->ai_addr);
							storage_list->insert(storage);
						}
						break;

					case AF_INET6:
						if (ipv6_supported)
						{
							*ov::ToSockAddrIn6(&storage) = *ov::ToSockAddrIn6(item->ai_addr);
							storage_list->insert(storage);
						}
						break;

					default:
						break;
				}
			}
			else
			{
				logtc("ai_addr must not be nullptr");
				OV_ASSERT2(false);
			}

			item = item->ai_next;
		}

		return true;
	}

	SocketAddress::SocketAddress()
	{
		::memset(&_address_storage, 0, sizeof(_address_storage));
		_address_storage.ss_family = AF_UNSPEC;
	}

	SocketAddress::SocketAddress(const ov::String &hostname, const sockaddr_in &addr_in)
		: SocketAddress()
	{
		_hostname = hostname;
		*ov::ToSockAddrIn4(&_address_storage) = addr_in;

		UpdateFromStorage();
	}

	SocketAddress::SocketAddress(const ov::String &hostname, const sockaddr_in6 &addr_in)
		: SocketAddress()
	{
		_hostname = hostname;
		*ov::ToSockAddrIn6(&_address_storage) = addr_in;

		UpdateFromStorage();
	}

	SocketAddress::SocketAddress(const ov::String &hostname, const sockaddr_storage &address)
		: SocketAddress()
	{
		OV_ASSERT(IsValidFamily(address.ss_family),
				  "Unknown address family: %d", address.ss_family);

		_hostname = hostname;
		_address_storage = address;

		UpdateFromStorage();
	}

	SocketAddress::SocketAddress(const SocketAddress &address) noexcept
		: SocketAddress()
	{
		_address_storage = address._address_storage;

		_is_wildcard_host = address._is_wildcard_host;
		_hostname = address._hostname;
		_ip_address = address._ip_address;
		_port_set = address._port_set;
		_port = address._port;
	}

	SocketAddress::SocketAddress(SocketAddress &&address) noexcept
		: SocketAddress()
	{
		std::swap(_address_storage, address._address_storage);

		std::swap(_is_wildcard_host, address._is_wildcard_host);
		std::swap(_hostname, address._hostname);
		std::swap(_ip_address, address._ip_address);
		std::swap(_port_set, address._port_set);
		std::swap(_port, address._port);
	}

	SocketAddress::~SocketAddress()
	{
	}

	bool SocketAddress::IsValid() const
	{
		return IsValidFamily(_address_storage.ss_family);
	}

	SocketAddress &SocketAddress::operator=(const SocketAddress &address) noexcept
	{
		_address_storage = address._address_storage;

		_is_wildcard_host = address._is_wildcard_host;
		_hostname = address._hostname;
		_ip_address = address._ip_address;
		_port_set = address._port_set;
		_port = address._port;

		return *this;
	}

	bool SocketAddress::operator==(const SocketAddress &address) const
	{
		return (_hostname == address._hostname) &&
			   (_ip_address == address._ip_address) &&
			   (_port_set == address._port_set) &&
			   (::memcmp(&_address_storage, &(address._address_storage), sizeof(_address_storage)) == 0);
	}

	bool SocketAddress::operator!=(const SocketAddress &address) const
	{
		return (operator==(address)) == false;
	}

	bool SocketAddress::operator<(const SocketAddress &address) const
	{
		return ::memcmp(&_address_storage, &(address._address_storage), sizeof(_address_storage)) < 0;
	}

	bool SocketAddress::operator>(const SocketAddress &address) const
	{
		return ::memcmp(&_address_storage, &(address._address_storage), sizeof(_address_storage)) > 0;
	}

	void SocketAddress::UpdateFromStorage()
	{
		if (IsValid())
		{
			String ip_address;
			ip_address.SetLength(GetIPAddrLength());

			if (::inet_ntop(
					_address_storage.ss_family,
					ToSockAddrIn(),
					ip_address.GetBuffer(), static_cast<socklen_t>(ip_address.GetLength())) != nullptr)
			{
				_ip_address = std::move(ip_address);
			}
			else
			{
				_ip_address = "";
			}

			if (_hostname.IsEmpty())
			{
				_is_wildcard_host = (IsIPv4() && (ov::ToSockAddrIn4(&_address_storage)->sin_addr.s_addr == INADDR_ANY)) ||
									(IsIPv6() && (::memcmp(&(ov::ToSockAddrIn6(&_address_storage)->sin6_addr), &in6addr_any, sizeof(in6addr_any)) == 0));
			}

			_port = NetworkToHost16(GetInPort(&_address_storage));
			_port_set = true;
		}
		else
		{
			_is_wildcard_host = false;
		}
	}

	ov::String SocketAddress::GetHostname() const noexcept
	{
		return _hostname;
	}

	bool SocketAddress::SetToAnyAddress() noexcept
	{
		switch (GetFamily())
		{
			case ov::SocketFamily::Inet:
				ov::ToSockAddrIn4(&_address_storage)->sin_addr.s_addr = INADDR_ANY;
				break;

			case ov::SocketFamily::Inet6:
				::memset(&(ov::ToSockAddrIn6(&_address_storage)->sin6_addr), 0, sizeof(in6addr_any));
				break;

			default:
				return false;
		}

		_is_wildcard_host = true;

		return true;
	}

	SocketAddress SocketAddress::AnySocketAddress() const noexcept
	{
		auto instance = *this;
		instance.SetToAnyAddress();

		return instance;
	}

	ov::String SocketAddress::GetIpAddress() const noexcept
	{
		return _ip_address;
	}

	bool SocketAddress::SetPort(uint16_t port)
	{
		OV_ASSERT2(_address_storage.ss_family != AF_UNSPEC);

		GetInPort(&_address_storage) = HostToNetwork16(port);

		_port = port;
		_port_set = true;

		return true;
	}

	uint16_t SocketAddress::Port() const noexcept
	{
		return _port;
	}

	const sockaddr *SocketAddress::ToSockAddr() const noexcept
	{
		return IsValid() ? reinterpret_cast<const sockaddr *>(&_address_storage) : nullptr;
	}

	sockaddr *SocketAddress::ToSockAddr() noexcept
	{
		return IsValid() ? reinterpret_cast<sockaddr *>(&_address_storage) : nullptr;
	}

	const sockaddr_in *SocketAddress::ToSockAddrIn4() const noexcept
	{
		return IsIPv4() ? ov::ToSockAddrIn4(&_address_storage) : nullptr;
	}

	sockaddr_in *SocketAddress::ToSockAddrIn4() noexcept
	{
		return IsIPv4() ? ov::ToSockAddrIn4(&_address_storage) : nullptr;
	}

	const sockaddr_in6 *SocketAddress::ToSockAddrIn6() const noexcept
	{
		return IsIPv6() ? ov::ToSockAddrIn6(&_address_storage) : nullptr;
	}

	sockaddr_in6 *SocketAddress::ToSockAddrIn6() noexcept
	{
		return IsIPv6() ? ov::ToSockAddrIn6(&_address_storage) : nullptr;
	}

	const void *SocketAddress::ToInAddr() const noexcept
	{
		switch (_address_storage.ss_family)
		{
			case AF_INET:
				return ToIn4Addr();
			case AF_INET6:
				return ToIn6Addr();

			default:
				return nullptr;
		}
	}

	void *SocketAddress::ToInAddr() noexcept
	{
		switch (_address_storage.ss_family)
		{
			case AF_INET:
				return ToIn4Addr();
			case AF_INET6:
				return ToIn6Addr();

			default:
				return nullptr;
		}
	}

	const in_addr *SocketAddress::ToIn4Addr() const noexcept
	{
		return IsIPv4() ? &(ov::ToSockAddrIn4(&_address_storage)->sin_addr) : nullptr;
	}

	in_addr *SocketAddress::ToIn4Addr() noexcept
	{
		return IsIPv4() ? &(ov::ToSockAddrIn4(&_address_storage)->sin_addr) : nullptr;
	}

	const in6_addr *SocketAddress::ToIn6Addr() const noexcept
	{
		return IsIPv6() ? &(ov::ToSockAddrIn6(&_address_storage)->sin6_addr) : nullptr;
	}

	in6_addr *SocketAddress::ToIn6Addr() noexcept
	{
		return IsIPv6() ? &(ov::ToSockAddrIn6(&_address_storage)->sin6_addr) : nullptr;
	}

	socklen_t SocketAddress::GetSockAddrInLength() const noexcept
	{
		return ov::GetSockAddrInLength(_address_storage);
	}

	socklen_t SocketAddress::GetInAddrLength() const noexcept
	{
		return ov::GetInAddrLength(_address_storage);
	}

	socklen_t SocketAddress::GetIPAddrLength() const noexcept
	{
		switch (_address_storage.ss_family)
		{
			case AF_INET:
				return INET_ADDRSTRLEN;

			case AF_INET6:
				return INET6_ADDRSTRLEN;

			default:
				OV_ASSERT2(false);
				return 0;
		}
	}

	ov::String SocketAddress::ToString(bool ignore_privacy_protect_config) const noexcept
	{
		auto server_config = cfg::ConfigManager::GetInstance()->GetServer();
		const bool protect_privacy = (ignore_privacy_protect_config == false) && ((server_config != nullptr) && server_config->IsPrivacyProtectionOn());

		ov::String description;

		auto hostname = GetHostname();
		if (protect_privacy && (hostname.IsEmpty() == false))
		{
			hostname = "xxxxxxxx";
		}

		if (IsValid())
		{
			auto ip = GetIpAddress();

			if (protect_privacy && (ip.IsEmpty() == false))
			{
				ip = IsIPv4() ? "xxx.xxx.xxx.xxx" : (IsIPv6() ? "x:x:x:x:x:x:x:x" : "?");
			}

			description = IsValid() ? "" : "[?] ";

			if (_is_wildcard_host)
			{
				description.Append(IsIPv4() ? OV_SOCKET_WILDCARD_IPV4 : (IsIPv6() ? "[" OV_SOCKET_WILDCARD_IPV6 "]" : "?"));
			}
			else
			{
				if (IsIPv6())
				{
					description.AppendFormat("[%s]", ip.CStr());
				}
				else
				{
					description.Append(ip);
				}
			}

			if (protect_privacy)
			{
				description.AppendFormat(":xxx");
			}
			else
			{
				description.AppendFormat(":%d", Port());
			}

			if ((_is_wildcard_host == false) && ((hostname.IsEmpty() == false) && (hostname != ip)))
			{
				description.AppendFormat(" (host: '%s')", hostname.CStr());
			}
		}
		else
		{
			description.Append(hostname);
			const auto port = Port();

			if (port > 0)
			{
				if (protect_privacy)
				{
					description.AppendFormat(":xxx");
				}
				else
				{
					description.AppendFormat(":%d", port);
				}
			}
		}

		return description;
	}
}  // namespace ov
