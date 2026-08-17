//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

namespace cfg
{
	namespace cmn
	{
		class CrossDomainSupport
		{
		protected:
			cmn::CrossDomains _cross_domains;

		public:
			const auto &GetCrossDomains(bool *is_parsed = nullptr) const
			{
				if (is_parsed != nullptr)
				{
					*is_parsed = _cross_domains.IsParsed();
				}

				return _cross_domains;
			}

			const auto &GetCrossDomainList(bool *is_parsed = nullptr) const
			{
				return GetCrossDomains(is_parsed).GetUrls();
			}
		};
	}  // namespace cmn
}  // namespace cfg
