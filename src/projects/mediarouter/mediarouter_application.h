//==============================================================================
//
//  MediaRouteApplication
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <config/items/items.h>

#include <algorithm>
#include <stdint.h>
#include <memory>
#include <vector>

#include "base/mediarouter/mediarouter_application_connector.h"
#include "base/mediarouter/mediarouter_application_interface.h"
#include "base/mediarouter/mediarouter_application_observer.h"
#include "base/mediarouter/mediarouter_interface.h"
#include "modules/managed_queue/managed_queue.h"

#include "mediarouter_stream.h"
#include "mediarouter_stream_tap.h"

class ApplicationInfo;
class Stream;
class RelayServer;
class RelayClient;

class MediaRouteApplication : public MediaRouteApplicationInterface
{
public:
	static std::shared_ptr<MediaRouteApplication> Create(
		const info::Application &application_info);

	explicit MediaRouteApplication(
		const info::Application &application_info);

	~MediaRouteApplication() override;

public:
	bool Start();
	bool Stop();

	// Get Application information
	const info::Application &GetApplicationInfo() const;

public:
	//////////////////////////////////////////////////////////////////////
	// Interface for Application Binding
	//////////////////////////////////////////////////////////////////////

	// Register/unregister connector from provider
	bool RegisterConnectorApp(
		std::shared_ptr<MediaRouterApplicationConnector> connector);

	bool UnregisterConnectorApp(
		std::shared_ptr<MediaRouterApplicationConnector> connector);

	// Register/unregister connector from publisher
	bool RegisterObserverApp(
		std::shared_ptr<MediaRouterApplicationObserver> observer);

	bool UnregisterObserverApp(
		std::shared_ptr<MediaRouterApplicationObserver> observer);

	//////////////////////////////////////////////////////////////////////
	// Interface for Stream Mirroring
	//////////////////////////////////////////////////////////////////////
	CommonErrorCode MirrorStream(std::shared_ptr<MediaRouterStreamTap> &stream_tap,
				const ov::String &stream_name,
				MediaRouterInterface::MirrorPosition position);

	CommonErrorCode UnmirrorStream(const std::shared_ptr<MediaRouterStreamTap> &stream_tap);

public:
	//////////////////////////////////////////////////////////////////////
	// Interface for Stream and MediaPacket
	//////////////////////////////////////////////////////////////////////
	bool OnStreamCreated(
		const std::shared_ptr<MediaRouterApplicationConnector> &app_conn,
		const std::shared_ptr<info::Stream> &stream) override;

	bool OnStreamDeleted(
		const std::shared_ptr<MediaRouterApplicationConnector> &app_conn,
		const std::shared_ptr<info::Stream> &stream) override;

	bool OnStreamUpdated(
		const std::shared_ptr<MediaRouterApplicationConnector> &app_conn,
		const std::shared_ptr<info::Stream> &stream) override;

	bool OnPacketReceived(
		const std::shared_ptr<MediaRouterApplicationConnector> &app_conn,
		const std::shared_ptr<info::Stream> &stream,
		const std::shared_ptr<MediaPacket> &packet) override;

	bool IsExistingInboundStream(ov::String stream_name) override;


public:
	bool NotifyStreamCreate(
		const std::shared_ptr<info::Stream> &stream_info,
		MediaRouterApplicationConnector::ConnectorType connector_type);

	bool NotifyStreamPrepared(
		std::shared_ptr<MediaRouteStream> &stream);
	
	bool NotifyStreamDeleted(
		const std::shared_ptr<info::Stream> &stream_info,
		const MediaRouterApplicationConnector::ConnectorType connector_type);

	bool NotifyStreamUpdated(
		const std::shared_ptr<info::Stream> &stream_info,
		const MediaRouterApplicationConnector::ConnectorType connector_type);

private:
	std::shared_ptr<MediaRouteStream> CreateInboundStream(const std::shared_ptr<info::Stream> &stream_info);
	std::shared_ptr<MediaRouteStream> CreateOutboundStream(const std::shared_ptr<info::Stream> &stream_info);

	bool DeleteInboundStream(const std::shared_ptr<info::Stream> &stream_info);
	bool DeleteOutboundStream(const std::shared_ptr<info::Stream> &stream_info);
	bool UnmirrorStream(const std::shared_ptr<info::Stream> &stream);

	// std::shared_ptr<MediaRouteStream> GetStream(uint8_t indicator, uint32_t stream_id);
	std::shared_ptr<MediaRouteStream> GetInboundStream(uint32_t stream_id);
	std::shared_ptr<MediaRouteStream> GetInboundStreamByName(const ov::String stream_name);
	std::shared_ptr<MediaRouteStream> GetOutboundStream(uint32_t stream_id);
	std::shared_ptr<MediaRouteStream> GetOutboundStreamByName(const ov::String stream_name);

private:
	// Application information from configuration file
	const info::Application _application_info;

	// Information of Connector instance
	std::vector<std::shared_ptr<MediaRouterApplicationConnector>> _connectors;
	std::shared_mutex _connectors_lock;

	// Information of Observer instance
	std::vector<std::shared_ptr<MediaRouterApplicationObserver>> _observers;
	std::shared_mutex _observers_lock;

	// Information of StreamTap instance, for performance reason, inbound/outbound stream taps are separated.
	// stream_id -> StreamTap
	std::multimap<uint32_t, std::shared_ptr<MediaRouterStreamTap>> _stream_taps;
	std::shared_mutex _stream_taps_lock;

	// Information of MediaStream instance
	// Inbound Streams
	// Key : Stream.id
	std::map<uint32_t, std::shared_ptr<MediaRouteStream>> _inbound_streams;

	// Outbound Streams
	// Key : Stream.id
	std::map<uint32_t, std::shared_ptr<MediaRouteStream>> _outbound_streams;
	std::shared_mutex _streams_lock;

private:
	uint32_t GetWorkerIDByStreamID(info::stream_id_t stream_id);
	void InboundWorkerThread(uint32_t worker_id);
	void OutboundWorkerThread(uint32_t worker_id);

	std::atomic<bool> _kill_flag{false};
	std::vector<std::thread> _inbound_threads;
	std::vector<std::thread> _outbound_threads;

	uint32_t _max_worker_thread_count;

private:
	std::vector<std::shared_ptr<ov::ManagedQueue<std::shared_ptr<MediaRouteStream>>>> _inbound_stream_indicator;
	std::vector<std::shared_ptr<ov::ManagedQueue<std::shared_ptr<MediaRouteStream>>>> _outbound_stream_indicator;
};
