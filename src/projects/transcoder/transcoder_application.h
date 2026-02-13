//==============================================================================
//
//  TranscodeApplication
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include <stdint.h>

#include <algorithm>
#include <memory>
#include <thread>
#include <vector>

#include "base/info/stream.h"
#include "base/mediarouter/media_buffer.h"
#include "base/mediarouter/mediarouter_application_connector.h"
#include "base/mediarouter/mediarouter_application_observer.h"
#include "base/ovlibrary/ovlibrary.h"
#include "transcoder_stream.h"

class TranscodeApplication : public MediaRouterApplicationConnector, public MediaRouterApplicationObserver
{
public:
	static std::shared_ptr<TranscodeApplication> Create(const info::Application &application_info);

	explicit TranscodeApplication(const info::Application &application_info);
	~TranscodeApplication() override;

	bool Start();
	bool Stop();

	MediaRouterApplicationObserver::ObserverType GetObserverType() override
	{
		return MediaRouterApplicationObserver::ObserverType::Transcoder;
	}

	MediaRouterApplicationConnector::ConnectorType GetConnectorType() override
	{
		return MediaRouterApplicationConnector::ConnectorType::Transcoder;
	}

	////////////////////////////////////////////////////////////////////////////////////////////////
	// MediaRouterApplicationObserver Implementation
	////////////////////////////////////////////////////////////////////////////////////////////////
	bool OnStreamCreated(const std::shared_ptr<info::Stream> &stream) override;
	bool OnStreamDeleted(const std::shared_ptr<info::Stream> &stream) override;
	bool OnStreamPrepared(const std::shared_ptr<info::Stream> &stream) override;
	bool OnStreamUpdated(const std::shared_ptr<info::Stream> &stream) override;

	bool OnSendFrame(const std::shared_ptr<info::Stream> &stream, const std::shared_ptr<MediaPacket> &packet) override;

private:
	bool ValidateAppConfiguration();

private:
	const info::Application _application_info;
	std::map<int32_t, std::shared_ptr<TranscoderStream>> _streams;
	std::mutex _mutex;
};
