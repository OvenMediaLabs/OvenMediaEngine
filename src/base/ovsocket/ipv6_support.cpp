//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./ipv6_support.h"

#include "./socket.h"

#define OV_LOG_TAG "Socket.IPv6"

#define CreateIPv6Error(...) ov::Error::CreateError(OV_LOG_TAG, __VA_ARGS__)
#define CreateIPv6ErrorWithErrno(...) ov::Error::CreateError(OV_LOG_TAG, __VA_ARGS__ ": %s", ov::Error::CreateErrorFromErrno()->What())

namespace ov
{
	namespace ipv6
	{
		namespace internal
		{
			using socket_ptr_t = std::unique_ptr<socket_t, std::function<void(socket_t *handle)>>;
			socket_ptr_t MakeSocketPtr(socket_t handle)
			{
				// For RAII
				return socket_ptr_t(
					// The handle passed here is not actually a pointer, but a socket descriptor
					reinterpret_cast<socket_t *>(handle), [](socket_t *handle) {
						::close(static_cast<socket_t>(reinterpret_cast<std::uintptr_t>(handle)));
					});
			}

			std::shared_ptr<ov::Error> CheckIPv6Support()
			{
				socket_t handle = ::socket(AF_INET6, SOCK_STREAM, 0);

				if (handle == -1)
				{
					auto error = ov::Error::CreateErrorFromErrno();

					return CreateIPv6Error(
						"This host is not support IPv6 (%s%s)",
						(error->GetCode() == EAFNOSUPPORT) ? "EAFNOSUPPORT, " : "",
						error->What());
				}

				auto raii_handle = MakeSocketPtr(handle);

				struct sockaddr_in6 addr;
				::memset(&addr, 0, sizeof(addr));
				addr.sin6_family = AF_INET6;
				addr.sin6_addr = in6addr_loopback;
				addr.sin6_port = ov::HostToNetwork16(0);

				const int flag = 1;
				int result = ::setsockopt(handle, IPPROTO_IPV6, IPV6_V6ONLY, &flag, sizeof(flag));

				if (result == -1)
				{
					return CreateIPv6ErrorWithErrno("Failed to setsockopt()");
				}

				result = ::bind(handle, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));

				if (result == -1)
				{
					return CreateIPv6ErrorWithErrno("Failed to bind()");
				}

				return nullptr;
			}

			std::shared_ptr<ov::Error> CheckIPv4Support()
			{
				socket_t handle = ::socket(AF_INET, SOCK_STREAM, 0);

				if (handle == -1)
				{
					auto error = ov::Error::CreateErrorFromErrno();

					return CreateIPv6Error(
						"This host is not support IPv4 (%s%s)",
						(error->GetCode() == EAFNOSUPPORT) ? "EAFNOSUPPORT, " : "",
						error->What());
				}

				auto raii_handle = MakeSocketPtr(handle);

				struct sockaddr_in addr;
				::memset(&addr, 0, sizeof(addr));
				addr.sin_family = AF_INET;
				addr.sin_addr.s_addr = ov::HostToNetwork32(INADDR_LOOPBACK);
				addr.sin_port = ov::HostToNetwork16(0);

				int result = ::bind(handle, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));

				if (result == -1)
				{
					return CreateIPv6ErrorWithErrno("Failed to bind()");
				}

				return nullptr;
			}
		}  // namespace internal

		static Version operator|(Version lhs, Version rhs)
		{
			return static_cast<Version>(static_cast<int>(lhs) | static_cast<int>(rhs));
		}

		static Version &operator|=(Version &lhs, Version rhs)
		{
			lhs = static_cast<Version>(static_cast<int>(lhs) | static_cast<int>(rhs));
			return lhs;
		}

		Checker::Checker()
		{
			_ipv4_error = internal::CheckIPv4Support();
			_ipv6_error = internal::CheckIPv6Support();

			if ((_ipv4_error != nullptr) && (_ipv6_error != nullptr))
			{
				logtw("Both of IPv4 and IPv6 are not supported, use IPv4 by default: %s, %s",
					  _ipv4_error->What(), _ipv6_error->What());

				_is_fallback = true;
			}

			_version |= (_is_fallback || (_ipv4_error == nullptr)) ? Version::IPv4 : Version::None;
			_version |= (_ipv6_error == nullptr) ? Version::IPv6 : Version::None;
		}

		ov::String Checker::ToString() const
		{
			switch (_version)
			{
				case Version::None:
					OV_ASSERT2(_ipv4_error != nullptr);
					OV_ASSERT2(_ipv6_error != nullptr);

					return ov::String::FormatString(
						"None (reason: v4: %s[%d], v6: %s[%d])",
						_ipv4_error->GetMessage().CStr(), _ipv4_error->GetCode(),
						_ipv6_error->GetMessage().CStr(), _ipv6_error->GetCode());

				case Version::IPv4:
					OV_ASSERT2(_ipv6_error != nullptr);

					if (_is_fallback)
					{
						OV_ASSERT2(_ipv4_error != nullptr);

						return ov::String::FormatString(
							"IPv4 only (fallbacked, reason: v4: %s[%d], v6: %s[%d])",
							_ipv4_error->GetMessage().CStr(), _ipv4_error->GetCode(),
							_ipv6_error->GetMessage().CStr(), _ipv6_error->GetCode());
					}
					else
					{
						return ov::String::FormatString(
							"IPv4 only (reason: v6: %s[%d])",
							_ipv6_error->GetMessage().CStr(), _ipv6_error->GetCode());
					}

				case Version::IPv6:
					OV_ASSERT2(_ipv4_error != nullptr);

					return ov::String::FormatString(
						"IPv6 only (reason: v4: %s[%d])",
						_ipv4_error->GetMessage().CStr(), _ipv4_error->GetCode());

				case Version::Both:
					return "IPv4 and IPv6";

				default:
					OV_ASSERT2(false);
					return "Unknown";
			}
		}
	}  // namespace ipv6
}  // namespace ov
