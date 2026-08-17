#pragma once

#include <stdint.h>
#include <memory>
#include <vector>

#include <base/ovlibrary/ovlibrary.h>
#include <base/info/application.h>
#include <modules/physical_port/physical_port.h>

class MediaRouterApplicationObserver;
class MediaRouterApplicationConnector;
class MediaRouterStreamTap;
class Stream;

class MediaRouterInterface : public ov::EnableSharedFromThis<MediaRouterInterface>
{
public:
	enum MirrorPosition : uint8_t
	{
		Inbound = 0,	// it may be before transcoding
		Outbound		// it may be after transcoding or by-passing
	};

	virtual CommonErrorCode MirrorStream(std::shared_ptr<MediaRouterStreamTap> &stream_tap, const info::VHostAppName &vhost_app_name, const ov::String &stream_name, MirrorPosition posision) = 0;
	virtual CommonErrorCode UnmirrorStream(const std::shared_ptr<MediaRouterStreamTap> &stream_tap) = 0;

	////////////////////////////////////////////////////////////////////////////////////////////////
	// Used by Provider modules
	////////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool RegisterConnectorApp(
		const info::Application &application_info,
		const std::shared_ptr<MediaRouterApplicationConnector> &application_connector) = 0;

	virtual bool UnregisterConnectorApp(
		const info::Application &application_info,
		const std::shared_ptr<MediaRouterApplicationConnector> &application_connector) = 0;

	////////////////////////////////////////////////////////////////////////////////////////////////
	// Used by Publisher modules
	////////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool RegisterObserverApp(
		const info::Application &application_info,
		const std::shared_ptr<MediaRouterApplicationObserver> &application_observer) = 0;

	virtual bool UnregisterObserverApp(
		const info::Application &application_info,
		const std::shared_ptr<MediaRouterApplicationObserver> &application_observer) = 0;
};

