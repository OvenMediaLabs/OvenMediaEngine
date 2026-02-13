#pragma once


#include <stdint.h>
#include <memory>
#include <vector>

#include "base/ovlibrary/ovlibrary.h"

class MediaRouterApplicationObserver;
class MediaRouterApplicationConnector;
namespace info{class Stream;}
class MediaPacket;
class MediaRouteStream;

class RelayClient;

class MediaRouteApplicationInterface : public ov::EnableSharedFromThis<MediaRouteApplicationInterface>
{
public:
	virtual bool IsExistingInboundStream(ov::String stream_name) = 0;
	virtual bool OnStreamCreated(const std::shared_ptr<MediaRouterApplicationConnector> &application, const std::shared_ptr<info::Stream> &stream) = 0;
	virtual bool OnStreamDeleted(const std::shared_ptr<MediaRouterApplicationConnector> &application, const std::shared_ptr<info::Stream> &stream) = 0;
	virtual bool OnStreamUpdated(const std::shared_ptr<MediaRouterApplicationConnector> &application, const std::shared_ptr<info::Stream> &stream) = 0;
	virtual bool OnPacketReceived(const std::shared_ptr<MediaRouterApplicationConnector> &application, const std::shared_ptr<info::Stream> &stream, const std::shared_ptr<MediaPacket> &packet) = 0;
};

