//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

namespace cfg
{
	namespace an
	{
		struct Forwarding : public Item
		{
		protected:
			bool _enable = false;
			// Default OvenConsole URL
			ov::String _collector = "tcp://collector.ovenconsole.io:21514";

		public:
			CFG_DECLARE_CONST_REF_GETTER_OF(IsEnabled, _enable)
			CFG_DECLARE_CONST_REF_GETTER_OF(GetCollector, _collector)

		protected:
			void MakeList() override
			{
				Register<Optional>("Enable", &_enable);
				Register<Optional>("Collector", &_collector);
			}
		};
	}  // namespace an
}  // namespace cfg