//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../common/options.h"
#include "./provider.h"

namespace cfg
{
	namespace bind
	{
		namespace pvd
		{
			template <typename Tport>
			struct ProviderWithOptions : public Provider<Tport>
			{
			protected:
				cmn::Options _options;

			public:
				using Item::IsParsed;

				explicit ProviderWithOptions(const char *port)
					: Provider<Tport>(port)
				{
				}

				ProviderWithOptions()
				{
				}

				ProviderWithOptions(const char *port, const char *tls_port)
					: Provider<Tport>(port, tls_port)
				{
				}

				CFG_DECLARE_CONST_REF_GETTER_OF(GetOptions, _options);

			protected:
				void MakeList() override
				{
					Provider<Tport>::MakeList();

					Item::Register<Optional>("Options", &_options);
				};
			};
		}  // namespace pvd
	}  // namespace bind
}  // namespace cfg
