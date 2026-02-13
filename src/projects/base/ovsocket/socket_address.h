//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../ovlibrary/string.h"
#include "./socket_address_error.h"
#include "./socket_utilities.h"

#define OV_SOCKET_WILDCARD_IPV4 "*"
#define OV_SOCKET_WILDCARD_IPV6 "::"

namespace ov
{
	/// This is a class that presents IPv4/IPv6 address and port
	class SocketAddress
	{
	public:
		struct PortRange
		{
			uint16_t start_port{0};
			uint16_t end_port{0};

			PortRange() = default;
			PortRange(uint16_t start_port, uint16_t end_port)
				: start_port(start_port),
				  end_port(end_port)
			{
			}
		};

		struct Address
		{
			ov::String host;
			std::vector<PortRange> port_range_list;

			Address() = default;
			Address(const ov::String &host, const std::vector<PortRange> &port_range_list)
				: host(host),
				  port_range_list(port_range_list)
			{
			}

			bool HasPortList() const
			{
				return (port_range_list.empty() == false);
			}

			using Iterator = std::function<bool(const ov::String &host, const uint16_t port)>;

			// Returns false if a callback returns false, otherwise returns true
			bool EachPort(Iterator callback, const bool *dummy = nullptr) const
			{
				for (const auto &port_range : port_range_list)
				{
					for (int port = port_range.start_port; port <= port_range.end_port; port++)
					{
						if (callback(host, port) == false)
						{
							return false;
						}
					}
				}

				return true;
			}
		};

	public:
		/**
		 * Get the start/end port from the string
		 * 
		 * 1000 = 1000
		 * 1000-1002 = 1000, 1001, 1002
		 * 1000-1002,1003 = 1000, 1001, 1002, 1003
		 */
		MAY_THROWS(ov::SocketAddressError)
		static std::vector<PortRange> ParsePort(const ov::String &string);

		MAY_THROWS(ov::SocketAddressError)
		static Address ParseAddress(const ov::String &string);

		/**
		 * Parse a host and a port from the string & create an SocketAddress instance
		 * 
		 * Those Create() APIs support various formats such as:
		 * 
		 * +----------------------------------+-------------------------+--------------------------------------------+-------+
		 * |                                  |                         | Result                                     |       |
		 * | Format                           | Example                 +----------------------+---------------------+-------|
		 * |                                  |                         | IP(s)                | Port(s)             | Count |
		 * +----------------------------------+-------------------------+----------------------+---------------------+-------+
		 * | <IPv4>:<PORT>                    | 1.2.3.4:1234            | 1.2.3.4              | 1234                | 1     |
		 * +----------------------------------+-------------------------+----------------------+---------------------+-------+
		 * | <IPv6>:<PORT>                    | [::]:1234               | :: (in6addr_any)     | 1234                | 1     |
		 * +----------------------------------+-------------------------+----------------------+---------------------+-------+
		 * | <IPv4>:<PORT>                    | *:1234                  | 0.0.0.0 (INADDR_ANY) | 1234                | 1     |
		 * +----------------------------------+-------------------------+----------------------+---------------------+-------+
		 * | <DOMAIN>:<PORT>                  | airensoft.com:1234      | 2.3.4.5              | 1234                | 1     |
		 * +----------------------------------+-------------------------+----------------------+---------------------+-------+
		 * | <IPv4>                           | 1.2.3.4                 | 1.2.3.4              | 0                   | 1     |
		 * +----------------------------------+-------------------------+----------------------+---------------------+-------+
		 * | <IPv6>                           | [::]                    | :: (in6addr_any)     | 0                   | 1     |
		 * +----------------------------------+-------------------------+----------------------+---------------------+-------+
		 * | <IPv6>                           | ::                      | :: (in6addr_any)     | 0                   | 1     |
		 * +----------------------------------+-------------------------+----------------------+---------------------+-------+
		 * | <DOMAIN>                         | airensoft.com           | 2.3.4.5              | 0                   | 1     |
		 * +----------------------------------+-------------------------+----------------------+---------------------+-------+
		 * | <PORT>                           | 1234                    | 0.0.0.0 (INADDR_ANY) | 1234                | 2     |
		 * |                                  |                         | :: (in6addr_any)     |                     |       |
		 * +----------------------------------+-------------------------+----------------------+---------------------+-------+
		 * | <START_PORT>-<END_PORT>          | 1234-1236               | 0.0.0.0 (INADDR_ANY) | 1234/1235/1236      | 6     |
		 * |                                  |                         | :: (in6addr_any)     |                     |       |
		 * +----------------------------------+-------------------------+----------------------+---------------------+-------+
		 * | <START_PORT1>-<END_PORT1>,       | 1234-1235,1237-1238     | 0.0.0.0 (INADDR_ANY) | 1234/1235/1236/1237 | 8     |
		 * | <START_PORT2>-<END_PORT2>        |                         | :: (in6addr_any)     |                     |       |
		 * +----------------------------------+-------------------------+----------------------+---------------------+-------+
		 * | <IPv4>:<START_PORT>-<END_PORT>   | *:1234-1236             | 0.0.0.0 (INADDR_ANY) | 1234/1235/1236      | 6     |
		 * +----------------------------------+-------------------------+----------------------+---------------------+-------+
		 * | <IPv6>:<START_PORT>-<END_PORT>   | [::]:1234-1236          | :: (in6addr_any)     | 1234/1235/1236      | 3     |
		 * +----------------------------------+-------------------------+----------------------+---------------------+-------+
		 * | <DOMAIN>:<START_PORT>-<END_PORT> | airensoft.com:1234-1236 | 2.3.4.5              | 1234/1235/1236      | 3     |
		 * +----------------------------------+-------------------------+----------------------+---------------------+-------+
		 * 
		 * @remarks This API may returns multiple SocketAddress instances (See the Count column in the table above)
		 */
		MAY_THROWS(ov::SocketAddressError)
		static std::vector<SocketAddress> Create(const ov::String &string);
		MAY_THROWS(ov::SocketAddressError)
		static std::vector<SocketAddress> Create(const std::vector<ov::String> &host_list, uint16_t port);
		MAY_THROWS(ov::SocketAddressError)
		static std::vector<SocketAddress> Create(const ov::String &host, uint16_t port);
		MAY_THROWS(ov::SocketAddressError)
		static std::vector<SocketAddress> Create(const ov::String &host, const std::vector<int> &port_list);
		MAY_THROWS(ov::SocketAddressError)
		static std::vector<SocketAddress> Create(const std::vector<ov::String> &host_list, const std::vector<int> &port_list);

		// Temporary APIs for compatibility
		MAY_THROWS(ov::SocketAddressError)
		static SocketAddress CreateAndGetFirst(const ov::String &string);
		MAY_THROWS(ov::SocketAddressError)
		static SocketAddress CreateAndGetFirst(const ov::String &host, uint16_t port);

		/**
		 * Create an instance represents invalid socket address
		 */
		SocketAddress();

		/**
		 * Create an instance from sockaddr_in (IPv4)
		 * 
		 * @param address An address that represents IPv4 address
		 */
		explicit SocketAddress(const ov::String &hostname, const sockaddr_in &address);
		/**
		 * Create an instance from sockaddr_in6 (IPv6)
		 * 
		 * @param address An address that represents IPv6 address
		 */
		explicit SocketAddress(const ov::String &hostname, const sockaddr_in6 &address);
		/**
		 * Create an instance from sockaddr_storage
		 * 
		 * @param address An address that represents IPv4 or IPv6 address
		 */
		explicit SocketAddress(const ov::String &hostname, const sockaddr_storage &address);

		// copy ctor
		SocketAddress(const SocketAddress &address) noexcept;
		// move ctor
		SocketAddress(SocketAddress &&address) noexcept;

		~SocketAddress();

		bool IsValid() const;

		SocketAddress &operator=(const SocketAddress &address) noexcept;

		bool operator==(const SocketAddress &address) const;
		bool operator!=(const SocketAddress &address) const;
		bool operator<(const SocketAddress &address) const;
		bool operator>(const SocketAddress &address) const;

		void SetFamily(SocketFamily family)
		{
			_address_storage.ss_family = static_cast<sa_family_t>(family);
		}

		inline SocketFamily GetFamily() const
		{
			return static_cast<SocketFamily>(_address_storage.ss_family);
		}

		inline bool IsIPv4() const
		{
			return GetFamily() == SocketFamily::Inet;
		}

		inline bool IsIPv6() const
		{
			return GetFamily() == SocketFamily::Inet6;
		}

		ov::String GetHostname() const noexcept;

		// Make this instance point to INET_ADDR or in6addr_any
		bool SetToAnyAddress() noexcept;
		ov::String GetIpAddress() const noexcept;
		// Copy this instance and make it point to INET_ADDR or in6addr_any
		SocketAddress AnySocketAddress() const noexcept;

		bool SetPort(uint16_t port);
		uint16_t Port() const noexcept;

		const sockaddr *ToSockAddr() const noexcept;
		sockaddr *ToSockAddr() noexcept;
		const sockaddr_in *ToSockAddrIn4() const noexcept;
		sockaddr_in *ToSockAddrIn4() noexcept;
		const sockaddr_in6 *ToSockAddrIn6() const noexcept;
		sockaddr_in6 *ToSockAddrIn6() noexcept;

		const void *ToInAddr() const noexcept;
		void *ToInAddr() noexcept;
		const in_addr *ToIn4Addr() const noexcept;
		in_addr *ToIn4Addr() noexcept;
		const in6_addr *ToIn6Addr() const noexcept;
		in6_addr *ToIn6Addr() noexcept;

		inline operator const sockaddr *() const noexcept
		{
			return ToSockAddr();
		}
		inline operator sockaddr *() noexcept
		{
			return ToSockAddr();
		}

		inline operator in_addr *() noexcept
		{
			return ToIn4Addr();
		}

		inline operator in6_addr *() noexcept
		{
			return ToIn6Addr();
		}

		inline operator const in_addr *() const noexcept
		{
			return ToIn4Addr();
		}

		void *ToSockAddrIn()
		{
			return ov::ToSockAddrIn(&_address_storage);
		}

		socklen_t GetSockAddrInLength() const noexcept;
		socklen_t GetInAddrLength() const noexcept;
		socklen_t GetIPAddrLength() const noexcept;

		void UpdateFromStorage();

		ov::String ToString(bool ignore_privacy_protect_config = true) const noexcept;

		std::size_t Hash() const
		{
			switch (GetFamily())
			{
				case ov::SocketFamily::Inet: {
					auto addr_in = ToSockAddrIn4();
					return std::hash<uint32_t>{}(addr_in->sin_addr.s_addr) ^
						   std::hash<uint16_t>{}(addr_in->sin_port);
				}

				case ov::SocketFamily::Inet6: {
					auto addr_in6 = ToSockAddrIn6();
					return std::hash<uint64_t>{}(reinterpret_cast<const uint64_t *>(addr_in6->sin6_addr.s6_addr)[0]) ^
						   std::hash<uint64_t>{}(reinterpret_cast<const uint64_t *>(addr_in6->sin6_addr.s6_addr)[1]) ^
						   std::hash<uint16_t>{}(addr_in6->sin6_port);
				}

				default:
					return 0;
			}
		}

	protected:
		struct StorageCompare
		{
			bool operator()(const sockaddr_storage &storage1, const sockaddr_storage &storage2) const
			{
				if (storage1.ss_family != storage2.ss_family)
				{
					return storage1.ss_family < storage2.ss_family;
				}

				switch (storage1.ss_family)
				{
					case AF_INET:
						return (reinterpret_cast<const sockaddr_in *>(&storage1))->sin_addr.s_addr >
							   (reinterpret_cast<const sockaddr_in *>(&storage2))->sin_addr.s_addr;

					case AF_INET6:
						return ::memcmp(
								   &(reinterpret_cast<const sockaddr_in6 *>(&storage1))->sin6_addr,
								   &(reinterpret_cast<const sockaddr_in6 *>(&storage2))->sin6_addr,
								   sizeof(in6_addr)) > 0;

					default:
						return false;
				}
			}
		};

		using StorageList = std::set<sockaddr_storage, StorageCompare>;

	protected:
		static void CreateInternal(const ov::String &host, uint16_t port, std::vector<SocketAddress> *address_list);
		static void CreateInternal(const std::vector<ov::String> &host_list, uint16_t port, std::vector<SocketAddress> *address_list);

	public:
		void SetHostname(const ov::String &hostname)
		{
			_hostname = hostname;
		}

	protected:
		static RaiiPtr<addrinfo> GetAddrInfo(const ov::String &host);

		// Returns true if the host is resolved successfully
		// Returns false if the DNS doesn't work
		// Throws an exception if the host is invalid
		MAY_THROWS(ov::SocketAddressError)
		static bool Resolve(ov::String host, SocketAddress::StorageList *storage_list, bool *is_wildcard_host);

	protected:
		sockaddr_storage _address_storage{};

		bool _is_wildcard_host = false;
		ov::String _hostname;
		ov::String _ip_address;
		bool _port_set = false;
		uint16_t _port = 0;
	};
}  // namespace ov

namespace std
{
	template <>
	struct hash<ov::SocketAddress>
	{
		std::size_t operator()(ov::SocketAddress const &address) const
		{
			return address.Hash();
		}
	};
}  // namespace std
