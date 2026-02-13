#include <utility>

//==============================================================================
//
//  Transcoder
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#include <unistd.h>

#include <iostream>

#include "config/config_manager.h"
#include "transcoder.h"
#include "transcoder_gpu.h"
#include "transcoder_private.h"

std::shared_ptr<Transcoder> Transcoder::Create(std::shared_ptr<MediaRouterInterface> router)
{
	auto transcoder = std::make_shared<Transcoder>(router);
	if (!transcoder->Start())
	{
		logte("An error occurred while creating Transcoder");
		return nullptr;
	}
	return transcoder;
}

Transcoder::Transcoder(std::shared_ptr<MediaRouterInterface> router)
{
	_router = std::move(router);
}

bool Transcoder::Start()
{
	logtt("Transcoder has been started");

	SetModuleAvailable(true);

	TranscodeGPU::GetInstance()->Initialize();

	return true;
}

bool Transcoder::Stop()
{
	logtt("Transcoder has been stopped");

	TranscodeGPU::GetInstance()->Uninitialize();

	return true;
}

bool Transcoder::OnCreateHost(const info::Host &host_info)
{
	return true;
}

bool Transcoder::OnDeleteHost(const info::Host &host_info)
{
	return true;
}

// Create Application
bool Transcoder::OnCreateApplication(const info::Application &app_info)
{
	auto application_id = app_info.GetId();

	auto application = TranscodeApplication::Create(app_info);
	if(application == nullptr)
	{
		logte("Could not create the transcoder application. [%s]", app_info.GetVHostAppName().CStr());
		return false;
	}

	_transcode_apps[application_id] = application;

	// Register to MediaRouter
	if (_router->RegisterObserverApp(app_info, application) == false)
	{
		logte("Could not register transcoder application to mediarouter. [%s]", app_info.GetVHostAppName().CStr());

		return false;
	}

	// Register to MediaRouter
	if (_router->RegisterConnectorApp(app_info, application) == false)
	{
		logte("Could not register transcoder application to mediarouter. [%s]", app_info.GetVHostAppName().CStr());

		return false;
	}

	logti("Transcoder has created [%s][%s] application", app_info.IsDynamicApp() ? "dynamic" : "config", app_info.GetVHostAppName().CStr());

	return true;
}

// Delete Application
bool Transcoder::OnDeleteApplication(const info::Application &app_info)
{
	auto application_id = app_info.GetId();
	auto it = _transcode_apps.find(application_id);
	if (it == _transcode_apps.end())
	{
		return false;
	}

	auto application = it->second;
	if(application == nullptr)
	{
		_transcode_apps.erase(it);
		return true;
	}

	// Unregister to MediaRouter
	if (_router->UnregisterObserverApp(app_info, application) == false)
	{
		logte("Could not unregister the application: %p", application.get());
	}

	// Unregister to MediaRouter
	if (_router->UnregisterConnectorApp(app_info, application) == false)
	{
		logte("Could not unregister the application: %p", application.get());
	}

	_transcode_apps.erase(it);

	logti("Transcoder has deleted [%s][%s] application", app_info.IsDynamicApp() ? "dynamic" : "config", app_info.GetVHostAppName().CStr());

	return true;
}

//  Application Name으로 TranscodeApplication 찾음
std::shared_ptr<TranscodeApplication> Transcoder::GetApplicationById(info::application_id_t application_id)
{
	auto obj = _transcode_apps.find(application_id);
	if (obj == _transcode_apps.end())
	{
		return nullptr;
	}

	return obj->second;
}
