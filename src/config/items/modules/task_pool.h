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
			bool _auto_scale = true;
			int _max_thread_count = 32;
			int _idle_timeout_ms = 60000;

		public:
			CFG_DECLARE_CONST_REF_GETTER_OF(GetThreadCount, _thread_count)
			CFG_DECLARE_CONST_REF_GETTER_OF(GetMaxTasks, _max_tasks)
			CFG_DECLARE_CONST_REF_GETTER_OF(IsAutoScaleEnabled, _auto_scale)
			CFG_DECLARE_CONST_REF_GETTER_OF(GetMaxThreadCount, _max_thread_count)
			CFG_DECLARE_CONST_REF_GETTER_OF(GetIdleTimeoutMs, _idle_timeout_ms)

		protected:
			void MakeList() override
			{
				/**
					Shared worker threads that modules use to run short tasks off their own
					thread, such as a name lookup or a request to a remote service.

					server.xml:
						<Modules>
							<TaskPool>
								<!-- Workers the pool keeps once work arrives -->
								<ThreadCount>4</ThreadCount>
								<!-- Tasks that may wait to start. Reaching this either adds a worker or rejects the task. -->
								<MaxTasks>128</MaxTasks>
								<!-- Add a worker when the waiting tasks reach MaxTasks, up to MaxThreadCount -->
								<AutoScale>true</AutoScale>
								<MaxThreadCount>32</MaxThreadCount>
								<!-- A worker with no task for this long stops itself. Zero keeps the workers running. -->
								<IdleTimeoutMs>60000</IdleTimeoutMs>
							</TaskPool>
						</Modules>
				*/
				Register<Optional>("ThreadCount", &_thread_count);
				Register<Optional>("MaxTasks", &_max_tasks);
				Register<Optional>("AutoScale", &_auto_scale);
				Register<Optional>("MaxThreadCount", &_max_thread_count);
				Register<Optional>("IdleTimeoutMs", &_idle_timeout_ms);
			}
		};
	}  // namespace modules
}  // namespace cfg
