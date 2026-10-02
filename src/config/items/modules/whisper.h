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
		// <Devices> selected which GPUs to preload onto. Whisper now runs on the
		// CPU, so the key is still accepted for configuration compatibility but
		// is ignored (a warning is logged when it is set).
		struct WhisperPreloadModel : public Item
		{
		protected:
			ov::String _path;
			ov::String _devices;  // Deprecated: ignored, kept for compatibility.

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
			// Total number of inference threads Whisper may use across every STT
			// track on this server. 0 means the number of hardware threads.
			int32_t _max_threads = 0;

		public:
			CFG_DECLARE_CONST_REF_GETTER_OF(GetPreloadModels, _preload_model_list)
			CFG_DECLARE_CONST_REF_GETTER_OF(GetMaxThreads, _max_threads)

		protected:
			void MakeList() override
			{
				Register<Optional>("PreloadModel", &_preload_model_list);
				Register<Optional>("MaxThreads", &_max_threads);
			}
		};
	}  // namespace modules
}  // namespace cfg
