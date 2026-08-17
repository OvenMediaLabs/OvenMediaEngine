//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "provider.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace pvd
			{
				struct MultiplexProvider : public Provider
				{
				protected:
					ov::String _mux_files_dir;

				public:
					ProviderType GetType() const override
					{
						return ProviderType::Multiplex;
					}

					CFG_DECLARE_CONST_REF_GETTER_OF(GetMuxFilesDir, _mux_files_dir)

				protected:
					void MakeList() override
					{
						Provider::MakeList();

						Register("MuxFilesDir", &_mux_files_dir);
					}
				};
			}  // namespace pvd
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg