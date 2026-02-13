//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#define INIT_MODULE(variable, name, create)                          \
	decltype(create) variable = nullptr;                             \
                                                                     \
	if (succeeded)                                                   \
	{                                                                \
		logti("Trying to create " name "...");                       \
                                                                     \
		variable = create;                                           \
                                                                     \
		if (variable == nullptr)                                     \
		{                                                            \
			logte("Failed to initialize " name);                     \
			succeeded = false;                                       \
		}                                                            \
		else                                                         \
		{                                                            \
			if (variable->IsModuleAvailable() == true)               \
			{                                                        \
				if (orchestrator->RegisterModule(variable) == false) \
				{                                                    \
					logte("Failed to register " name);               \
					succeeded = false;                               \
				}                                                    \
			}                                                        \
			else                                                     \
			{                                                        \
				variable.reset();                                    \
			}                                                        \
		}                                                            \
	}

#define RELEASE_MODULE(variable, name)                         \
	if (variable != nullptr)                                   \
	{                                                          \
		logti("Trying to release " name "...");                \
                                                               \
		if (orchestrator->UnregisterModule(variable) != false) \
		{                                                      \
			variable->Stop();                                  \
		}                                                      \
		else                                                   \
		{                                                      \
			logte("Failed to unregister " name);                \
		}                                                      \
	}

#define INIT_EXTERNAL_MODULE(name, func)                               \
	if (succeeded)                                                     \
	{                                                                  \
		logtt("Trying to initialize " name "...");                     \
		auto error = func();                                           \
                                                                       \
		if (error != nullptr)                                          \
		{                                                              \
			logte("Could not initialize " name ": %s", error->What()); \
			succeeded = false;                                         \
		}                                                              \
	}

#define TERMINATE_EXTERNAL_MODULE(name, func)                         \
	{                                                                 \
		logtt("Trying to terminate " name "...");                     \
		auto error = func();                                          \
                                                                      \
		if (error != nullptr)                                         \
		{                                                             \
			logte("Could not terminate " name ": %s", error->What()); \
			return 1;                                                 \
		}                                                             \
	}
