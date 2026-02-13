//==============================================================================
//
//  Transcoder
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <config/config.h>
#include <orchestrator/orchestrator.h>
#include <stdint.h>

#include <algorithm>
#include <memory>
#include <thread>
#include <vector>

#include "base/info/stream.h"
#include "base/mediarouter/media_buffer.h"
#include "base/mediarouter/mediarouter_interface.h"
#include "transcoder_application.h"

class Transcoder : public ocst::TranscoderModuleInterface
{
	// class TranscodeApplication;
public:
	static std::shared_ptr<Transcoder> Create(std::shared_ptr<MediaRouterInterface> router);

	Transcoder(std::shared_ptr<MediaRouterInterface> router);
	~Transcoder() = default;

	bool Start();
	bool Stop();

	//--------------------------------------------------------------------
	// Implementation of ModuleInterface
	//--------------------------------------------------------------------
	bool OnCreateHost(const info::Host &host_info) override;
	bool OnDeleteHost(const info::Host &host_info) override;
	bool OnCreateApplication(const info::Application &app_info) override;
	bool OnDeleteApplication(const info::Application &app_info) override;

private:
	// Application Name으로 RouteApplication을 찾음
	std::shared_ptr<TranscodeApplication> GetApplicationById(info::application_id_t application_id);

	std::vector<info::Application> _app_info_list;
	std::map<info::application_id_t, std::shared_ptr<TranscodeApplication>> _transcode_apps;
	std::shared_ptr<MediaRouterInterface> _router;
};
