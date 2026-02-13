//==============================================================================
//
//  Provider Base Class 
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "config/config.h"

#include "base/common_types.h"
#include "base/ovlibrary/ovlibrary.h"

#include "base/mediarouter/mediarouter_application_connector.h"
#include "stream.h"

#include <shared_mutex>

namespace pvd
{
	class Provider;
	
	class Application : public info::Application, public MediaRouterApplicationConnector
	{
	public:
		enum class ApplicationState : int8_t
		{
			Idle,
			Started,
			Stopped,
			Error
		};

		virtual bool Start();
		virtual bool Stop();

		// Get all streams
		const std::map<uint32_t, std::shared_ptr<Stream>> GetStreams();
		const std::shared_ptr<Stream> GetStreamById(uint32_t stream_id);
		const std::shared_ptr<Stream> GetStreamByName(ov::String stream_name);

		static uint32_t 	IssueUniqueStreamId();

		virtual bool AddStream(const std::shared_ptr<Stream> &stream);
		virtual bool DeleteStream(const std::shared_ptr<Stream> &stream);
		virtual bool UpdateStream(const std::shared_ptr<Stream> &stream);
		virtual bool DeleteAllStreams();

		const char* GetApplicationTypeName() final;

		std::shared_ptr<Provider> GetParentProvider()
		{
			return _provider;
		}

		ApplicationState GetState()
		{
			return _state;
		}

	protected:
		Application(const std::shared_ptr<Provider> &provider, const info::Application &application_info);
		virtual ~Application() override;
	
		virtual bool NotifyStreamCreated(const std::shared_ptr<Stream> &stream);
		virtual bool NotifyStreamUpdated(const std::shared_ptr<info::Stream> &stream);
		virtual bool NotifyStreamDeleted(const std::shared_ptr<Stream> &stream);

		
	private:
		std::shared_ptr<Provider> _provider;
		ApplicationState		_state = ApplicationState::Idle;

		std::shared_mutex _streams_guard;
		std::map<uint32_t, std::shared_ptr<Stream>> _streams;
	};
}
