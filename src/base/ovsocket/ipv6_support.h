//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

namespace ov
{
	namespace ipv6
	{
		enum class Version : int
		{
			None = 0x00,
			// IPv4 Only
			IPv4 = 0x01,
			// IPv6 Only
			IPv6 = 0x02,
			Both = IPv4 | IPv6
		};

		// Internal class
		class Checker : public Singleton<Checker>
		{
		public:
			Version SupportedVersion() const;

			inline bool IsIPv4Supported() const
			{
				return CheckFlag(_version, Version::IPv4);
			}

			inline bool IsIPv6Supported() const
			{
				return CheckFlag(_version, Version::IPv6);
			}

			String ToString() const;

		protected:
			friend class Singleton<Checker>;

			Checker();

			std::shared_ptr<Error> _ipv4_error;
			std::shared_ptr<Error> _ipv6_error;

			bool _is_fallback = false;

			Version _version = Version::None;
		};
	}  // namespace ipv6
}  // namespace ov