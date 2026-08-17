//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../common/cross_domains.h"

namespace cfg
{
	namespace dft
	{
		struct Defaults : public Item
		{
		protected:
			cmn::CrossDomains _cross_domains;

		public:
			CFG_DECLARE_CONST_REF_GETTER_OF(GetCrossDomains, _cross_domains)

			const auto &GetCrossDomainList(bool *is_parsed = nullptr) const
			{
				return GetCrossDomains(is_parsed).GetUrls();
			}

		protected:
			void MakeList() override
			{
				// default *
				Register<Optional>("CrossDomains", &_cross_domains);

			}
		};
	}  // namespace mgr
}  // namespace cfg