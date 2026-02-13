//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "decodes/decodes.h"
#include "event_generator/event_generator.h"
#include "origin.h"
#include "output_profiles/output_profiles.h"
#include "persistent_streams/persistent_streams.h"
#include "providers/providers.h"
#include "publishers/publishers.h"
#include "transcode_webhook/transcode_webhook.h"
#include "web_console/web_console.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			enum class ApplicationType
			{
				Unknown,
				Live,
				VoD
			};

			struct Application : public Item
			{
			protected:
				ov::String _name;
				ov::String _type;
				ApplicationType _type_value;

				dec::Decodes _decodes;
				oprf::OutputProfiles _output_profiles;
				pvd::Providers _providers;
				pub::Publishers _publishers;
				prst::PersistentStreams _persistent_streams;
				trwh::TranscodeWebhook _transcode_webhook;
				eg::EventGenerator _event_generator;

			public:
				CFG_DECLARE_CONST_REF_GETTER_OF(GetName, _name)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetType, _type_value)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetTypeString, _type)

				CFG_DECLARE_CONST_REF_GETTER_OF(GetDecodes, _decodes)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetOutputProfileList, _output_profiles.GetOutputProfileList())
				CFG_DECLARE_CONST_REF_GETTER_OF(GetOutputProfiles, _output_profiles)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetProviders, _providers)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetPublishers, _publishers)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetAppWorkerCount, _publishers.GetAppWorkerCount())
				CFG_DECLARE_CONST_REF_GETTER_OF(GetStreamWorkerCount, _publishers.GetStreamWorkerCount())
				CFG_DECLARE_CONST_REF_GETTER_OF(GetPersistentStreams, _persistent_streams)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetTranscodeWebhook, _transcode_webhook)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetEventGenerator, _event_generator)

				// Set Name, it is for dynamic application
				void SetName(const ov::String &name)
				{
					_name = name;
				}

			protected:
				void MakeList() override
				{
					Register("Name", &_name);
					Register("Type", &_type, nullptr, [=]() -> std::shared_ptr<ConfigError> {
						if (_type == "live")
						{
							_type_value = ApplicationType::Live;
							return nullptr;
						}
						else if (_type == "vod")
						{
							_type_value = ApplicationType::VoD;
							return nullptr;
						}

						return CreateConfigErrorPtr("Unknown type: %s", _type.CStr());
					});

					// TODO: Deprecated
					Register<Optional>("Decodes", &_decodes);
					Register<Optional>("OutputProfiles", &_output_profiles);
					Register<Optional>("Providers", &_providers);
					Register<Optional>("Publishers", &_publishers);
					// TODO: Deprecated
					Register<Optional>("PersistentStreams", &_persistent_streams);
					Register<Optional>("TranscodeWebhook", &_transcode_webhook);
					Register<Optional>("EventGenerator", &_event_generator);
				}
			};
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg