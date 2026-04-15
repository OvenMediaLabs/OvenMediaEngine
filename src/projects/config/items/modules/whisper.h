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
		// Represents a single <PreloadModel> entry.
		// <Path> is the model file path (absolute or relative to config dir).
		// <Devices> is a comma-separated list of CUDA device indices to preload onto,
		// or "all" (or omitted) to load on every available GPU.
		struct WhisperPreloadModel : public Item
		{
		protected:
			ov::String _path;
			ov::String _devices;  // "", "all", "0", "0,1", "2", etc.

		public:
			CFG_DECLARE_CONST_REF_GETTER_OF(GetPath, _path)
			CFG_DECLARE_CONST_REF_GETTER_OF(GetDevices, _devices)

		protected:
			void MakeList() override
			{
				Register("Path", &_path);
				Register<Optional>("Devices", &_devices);
			}
		};

		struct Whisper : public Item
		{
		protected:
			std::vector<WhisperPreloadModel> _preload_model_list;

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
