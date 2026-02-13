//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

#include "./socket_datastructure.h"

namespace ov
{
	class Socket;

	String GetStat(Socket socket);

#define _SOCK_ADDRESS_CONVERTER(function_name, argument_type, return_type, const_type)            \
	inline static const_type return_type *function_name(const_type argument_type *value) noexcept \
	{                                                                                             \
		return (reinterpret_cast<const_type return_type *>(value));                               \
	}

	inline static bool IsValidFamily(int family)
	{
		return (family == AF_INET || family == AF_INET6);
	}

	inline static bool IsIPv4(const SocketFamily &family)
	{
		return family == SocketFamily::Inet;
	}

	inline static bool IsIPv4(const int family)
	{
		return static_cast<SocketFamily>(family) == SocketFamily::Inet;
	}

	inline static bool IsIPv6(const SocketFamily &family)
	{
		return family == SocketFamily::Inet6;
	}

	inline static bool IsIPv6(const int family)
	{
		return static_cast<SocketFamily>(family) == SocketFamily::Inet6;
	}

	inline static int GetSocketProtocolFamily(const SocketFamily &family)
	{
		switch (family)
		{
			case SocketFamily::Inet:
				return PF_INET;

			case SocketFamily::Inet6:
				return PF_INET6;

			default:
				return PF_UNSPEC;
		}
	}

	_SOCK_ADDRESS_CONVERTER(ToSockAddrIn4, sockaddr_storage, sockaddr_in, )
	_SOCK_ADDRESS_CONVERTER(ToSockAddrIn4, sockaddr_storage, sockaddr_in, const)
	_SOCK_ADDRESS_CONVERTER(ToSockAddrIn4, sockaddr, sockaddr_in, )
	_SOCK_ADDRESS_CONVERTER(ToSockAddrIn4, sockaddr, sockaddr_in, const)

	_SOCK_ADDRESS_CONVERTER(ToSockAddrIn6, sockaddr_storage, sockaddr_in6, )
	_SOCK_ADDRESS_CONVERTER(ToSockAddrIn6, sockaddr_storage, sockaddr_in6, const)
	_SOCK_ADDRESS_CONVERTER(ToSockAddrIn6, sockaddr, sockaddr_in6, )
	_SOCK_ADDRESS_CONVERTER(ToSockAddrIn6, sockaddr, sockaddr_in6, const)

	inline static const void *ToSockAddrIn(const sockaddr *addr)
	{
		if (addr == nullptr)
		{
			return nullptr;
		}

		return IsIPv4(addr->sa_family) ? reinterpret_cast<const void *>(&ToSockAddrIn6(addr)->sin6_addr)
									   : reinterpret_cast<const void *>(&ToSockAddrIn4(addr)->sin_addr);
	}

	inline static void *ToSockAddrIn(sockaddr *addr)
	{
		if (addr == nullptr)
		{
			return nullptr;
		}

		return IsIPv4(addr->sa_family) ? reinterpret_cast<void *>(&ToSockAddrIn6(addr)->sin6_addr)
									   : reinterpret_cast<void *>(&ToSockAddrIn4(addr)->sin_addr);
	}

	inline static const void *ToSockAddrIn(const sockaddr_storage *storage)
	{
		if (storage == nullptr)
		{
			return nullptr;
		}

		return IsIPv6(storage->ss_family) ? reinterpret_cast<const void *>(&ToSockAddrIn6(storage)->sin6_addr)
										  : reinterpret_cast<const void *>(&ToSockAddrIn4(storage)->sin_addr);
	}

	inline static void *ToSockAddrIn(sockaddr_storage *storage)
	{
		if (storage == nullptr)
		{
			return nullptr;
		}

		return IsIPv6(storage->ss_family) ? reinterpret_cast<void *>(&ToSockAddrIn6(storage)->sin6_addr)
										  : reinterpret_cast<void *>(&ToSockAddrIn4(storage)->sin_addr);
	}

	inline static in_port_t &GetInPort(sockaddr_storage *storage)
	{
		return (storage->ss_family == AF_INET6) ? (ToSockAddrIn6(storage)->sin6_port) : ToSockAddrIn4(storage)->sin_port;
	}

	inline static const in_port_t &GetInPort(const sockaddr_storage *storage)
	{
		return (storage->ss_family == AF_INET6) ? ToSockAddrIn6(storage)->sin6_port : ToSockAddrIn4(storage)->sin_port;
	}

	inline static size_t GetInAddrLength(const sockaddr_storage &storage)
	{
		switch (storage.ss_family)
		{
			case AF_INET:
				return sizeof(in_addr);

			case AF_INET6:
				return sizeof(in6_addr);

			default:
				OV_ASSERT2(false);
				return 0;
		}
	}

	inline static size_t GetSockAddrInLength(const sockaddr_storage &storage)
	{
		switch (storage.ss_family)
		{
			case AF_INET:
				return sizeof(sockaddr_in);

			case AF_INET6:
				return sizeof(sockaddr_in6);

			default:
				OV_ASSERT2(false);
				return 0;
		}
	}
}  // namespace ov
