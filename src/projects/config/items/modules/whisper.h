//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

namespace cfg
{
	namespace modules
	{
		struct Whisper : public Item
		{
		protected:
			// List of model file paths to preload at server start.
			// Each path may be absolute or relative to the config file directory.
			std::vector<ov::String> _preload_model_list;

		public:
			CFG_DECLARE_CONST_REF_GETTER_OF(GetPreloadModels, _preload_model_list)

		protected:
			void MakeList() override
			{
				Register<Optional>("PreloadModel", &_preload_model_list);
			}
		};
	}  // namespace modules
}  // namespace cfg
