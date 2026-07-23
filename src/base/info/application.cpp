
//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#include "application.h"

#include "application_private.h"
#include "host.h"

namespace info
{
	Application::Application(const info::Host &host_info, application_id_t app_id, const VHostAppName &name, cfg::vhost::app::Application app_config, bool is_dynamic_app)
		: _application_id(app_id),
		  _name(name),
		  _app_config(std::move(app_config)),
		  _is_dynamic_app(is_dynamic_app)
	{
		_host_info = std::make_shared<info::Host>(host_info);
	}

	Application::Application(const info::Host &host_info, application_id_t app_id, const VHostAppName &name, bool is_dynamic_app)
		: _application_id(app_id),
		  _name(name),
		  _is_dynamic_app(is_dynamic_app)

	{
		_host_info = std::make_shared<info::Host>(host_info);
	}

	ov::String Application::GetUUID() const
	{
		if (_host_info == nullptr)
		{
			return "";
		}

		return ov::String::FormatString("%s%s", _host_info->GetUUID().CStr(), GetVHostAppName().GetAppName().CStr());
	}

	const Application &Application::GetInvalidApplication()
	{
		static Application application(Host("InvalidHostName", "InvalidHostID", cfg::vhost::VirtualHost()), InvalidApplicationId, VHostAppName::InvalidVHostAppName(), false);

		return application;
	}

	bool Application::operator==(const Application &app_info) const
	{
		if (_application_id == app_info._application_id)
		{
			return true;
		}
		return false;
	}

	bool Application::IsValid() const
	{
		return (_application_id != InvalidApplicationId);
	}

	application_id_t Application::GetId() const
	{
		return _application_id;
	}

	const VHostAppName &Application::GetVHostAppName() const
	{
		return _name;
	}

	const NamePath &Application::GetNamePath() const
	{
		return _name.GetNamePath();
	}

	const Host &Application::GetHostInfo() const
	{
		return *_host_info;
	}

	const cfg::vhost::app::Application &Application::GetConfig() const
	{
		return _app_config;
	}

	cfg::vhost::app::Application &Application::GetConfig()
	{
		return _app_config;
	}

	bool Application::IsDynamicApp() const
	{
		return _is_dynamic_app;
	}

	void Application::AddAudioMapItem(const std::shared_ptr<AudioMapItem> &audio_map_item)
	{
		_audio_map_items.push_back(audio_map_item);
	}

	void Application::AddAudioMapItems(const cfg::vhost::app::pvd::AudioMap &audio_map)
	{
		auto cfg_audio_map_items = audio_map.GetAudioMapItems();
		for (auto &cfg_audio_map_item : cfg_audio_map_items)
		{
			auto audio_map_item = std::make_shared<AudioMapItem>(
				cfg_audio_map_item.GetIndex(),
				cfg_audio_map_item.GetName(),
				cfg_audio_map_item.GetLanguage(),
				cfg_audio_map_item.GetCharacteristics());

			AddAudioMapItem(audio_map_item);
		}
	}

	// Get number of Audio Map Item
	size_t Application::GetAudioMapItemCount() const
	{
		return _audio_map_items.size();
	}

	// Get Audio Map Item by index
	std::shared_ptr<AudioMapItem> Application::GetAudioMapItem(size_t index) const
	{
		if (index >= _audio_map_items.size())
		{
			return nullptr;
		}

		return _audio_map_items[index];
	}

	void Application::SetEventForwardPolicy(const std::shared_ptr<info::EventForward> &event_forward_policy)
	{
		_event_forward_policy = event_forward_policy;
	}

	std::shared_ptr<info::EventForward> Application::GetEventForwardPolicy() const
	{
		return _event_forward_policy;
	}

}  // namespace info