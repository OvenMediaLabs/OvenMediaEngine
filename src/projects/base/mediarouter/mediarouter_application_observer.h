#pragma once

#include <base/info/stream.h>
#include <base/ovlibrary/enable_shared_from_this.h>
#include <stdint.h>

#include <memory>
#include <vector>

#include "mediarouter_application_interface.h"
#include "mediarouter_interface.h"

class MediaRouterApplicationObserver : public ov::EnableSharedFromThis<MediaRouterApplicationObserver>
{
public:
	enum class ObserverType : int8_t
	{
		Publisher = 0,
		Transcoder,
		Relay,

		// Temporarily used until Orchestrator takes stream management
		Orchestrator
	};

	////////////////////////////////////////////////////////////////////////////////////////////////
	// Interface
	////////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool OnStreamCreated(const std::shared_ptr<info::Stream> &info) = 0;
	virtual bool OnStreamDeleted(const std::shared_ptr<info::Stream> &info) = 0;
	virtual bool OnStreamUpdated(const std::shared_ptr<info::Stream> &info) = 0;
	virtual bool OnStreamPrepared(const std::shared_ptr<info::Stream> &info) = 0;

	// Delivery encoded video/audio frame
	virtual bool OnSendFrame(const std::shared_ptr<info::Stream> &info, const std::shared_ptr<MediaPacket> &packet) = 0;

	virtual ObserverType GetObserverType()
	{
		return ObserverType::Publisher;
	}
};
