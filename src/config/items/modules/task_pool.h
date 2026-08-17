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
		struct TaskPool : public Item
		{
		protected:
			int _thread_count = 4;
			int _max_tasks = 128;

		public:
			CFG_DECLARE_CONST_REF_GETTER_OF(GetThreadCount, _thread_count)
			CFG_DECLARE_CONST_REF_GETTER_OF(GetMaxTasks, _max_tasks)

		protected:
			void MakeList() override
			{
				/**
					Shared worker threads that modules use to run short tasks off their own
					thread, such as a name lookup or a request to a remote service.

					server.xml:
						<Modules>
							<TaskPool>
								<!-- Workers the pool runs on, started with the first task -->
								<ThreadCount>4</ThreadCount>
								<!-- Tasks that may wait to start. Reaching this rejects further tasks. -->
								<MaxTasks>128</MaxTasks>
							</TaskPool>
						</Modules>
				*/
				Register<Optional>("ThreadCount", &_thread_count);
				Register<Optional>("MaxTasks", &_max_tasks);
			}
		};
	}  // namespace modules
}  // namespace cfg
