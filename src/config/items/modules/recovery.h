//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan Kwon
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "module_template.h"

namespace cfg
{
	namespace modules
	{
		struct Recovery : public ModuleTemplate
		{
		protected:
			int _delete_lazy_stream_timeout = 0;

		public:
			// Experimental feature is disabled by default
			Recovery(bool enable) : ModuleTemplate(enable)
			{
			}

			CFG_DECLARE_CONST_REF_GETTER_OF(GetDeleteLazyStreamTimeout, _delete_lazy_stream_timeout)

		protected:
			void MakeList() override
			{
				ModuleTemplate::MakeList();

				/**
					[Experimental] Delete lazy stream

					If the size of the queue lasts beyond the limit for N seconds, it is determined to be an invalid stream. 
					For system recovery, the problematic stream is forcibly deleted.

					server.xml:
						<Modules>
							<Recovery>
								<!--  
								If the packet/frame queue is exceed for a certain period of time(millisecond, ms), it will be automatically deleted. 
								If this value is set to zero, the stream will not be deleted. 
								
								-->
								<DeleteLazyStreamTimeout>10000</DeleteLazyStreamTimeout>
							</Recovery>
						</Modules>
				*/
				Register<Optional>("DeleteLazyStreamTimeout", &_delete_lazy_stream_timeout);
			}
		};
	}  // namespace modules
}  // namespace cfg
