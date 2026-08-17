//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

namespace cfg
{
	namespace modules
	{
		struct Jemalloc : public Item
		{
		protected:
			// Run memory decay/purge on a dedicated background thread instead of the allocating
			// thread, smoothing out latency spikes. Enabled by default; effective only when OME is
			// built with jemalloc.
			bool _background_purge = true;

		public:
			CFG_DECLARE_CONST_REF_GETTER_OF(IsBackgroundPurgeEnabled, _background_purge)

		protected:
			void MakeList() override
			{
				Register<Optional>("BackgroundPurge", &_background_purge);
			}
		};
	}  // namespace modules
}  // namespace cfg
