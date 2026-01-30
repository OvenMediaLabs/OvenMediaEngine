//==============================================================================
//
//  MediaRouteApplication
//
//  Created by Keukhan
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "mediarouter_application.h"

#include <base/info/stream.h>

#include "mediarouter_private.h"
#include "monitoring/monitoring.h"


#define MIN_APPLICATION_WORKER_COUNT 1
#define MAX_APPLICATION_WORKER_COUNT 64

#define CONNECTOR(var) MediaRouterApplicationConnector::ConnectorType::var
#define OBSERVER(var) MediaRouterApplicationObserver::ObserverType::var

#define IS_REPRENT_SOURCE(var) (var == StreamRepresentationType::Source)
#define IS_REPRENT_RELAY(var) (var == StreamRepresentationType::Relay)

#define IS_CONNECTOR_PROVIDER(var) (var == MediaRouterApplicationConnector::ConnectorType::Provider)
#define IS_CONNECTOR_TRANSCODER(var) (var == MediaRouterApplicationConnector::ConnectorType::Transcoder)

#define IS_OBSERVER_PUBLISHER(var) (var == MediaRouterApplicationObserver::ObserverType::Publisher)
#define IS_OBSERVER_TRANSCODER(var) (var == MediaRouterApplicationObserver::ObserverType::Transcoder)
#define IS_OBSERVER_RELAY(var) (var == MediaRouterApplicationObserver::ObserverType::Relay)
#define IS_OBSERVER_ORCHESTRATOR(var) (var == MediaRouterApplicationObserver::ObserverType::Orchestrator)

using namespace cmn;

std::shared_ptr<MediaRouteApplication> MediaRouteApplication::Create(const info::Application &application_info)
{
	auto media_route_application = std::make_shared<MediaRouteApplication>(application_info);
	if (!media_route_application->Start())
	{
		return nullptr;
	}
	return media_route_application;
}

MediaRouteApplication::MediaRouteApplication(const info::Application &application_info)
	: _application_info(application_info)
{
	_max_worker_thread_count = std::min(std::max((uint32_t)_application_info.GetConfig().GetPublishers().GetAppWorkerCount(), (uint32_t)MIN_APPLICATION_WORKER_COUNT), (uint32_t)MAX_APPLICATION_WORKER_COUNT);
	int delay_buffer_time_ms = _application_info.GetConfig().GetPublishers().GetDelayBufferTimeMs();

	logti("[%s(%u)] Created Mediarouter application. Worker(%d) DelayBufferTime(%d)", _application_info.GetVHostAppName().CStr(), _application_info.GetId(), _max_worker_thread_count, delay_buffer_time_ms);

	for (uint32_t worker_id = 0; worker_id < _max_worker_thread_count; worker_id++)
	{
		{
			auto urn = std::make_shared<info::ManagedQueue::URN>(_application_info.GetVHostAppName(), nullptr, "imr", ov::String::FormatString("aw_%d", worker_id));
			auto stream_data = std::make_shared<ov::ManagedQueue<std::shared_ptr<MediaRouteStream>>>(urn, 1000);
			_inbound_stream_indicator.push_back(stream_data);
		}

		{
			auto urn = std::make_shared<info::ManagedQueue::URN>(_application_info.GetVHostAppName(), nullptr, "omr", ov::String::FormatString("aw_%d", worker_id));
			auto stream_data = std::make_shared<ov::ManagedQueue<std::shared_ptr<MediaRouteStream>>>(urn, 1000);
			stream_data->SetBufferingDelay(delay_buffer_time_ms);
			_outbound_stream_indicator.push_back(stream_data);
		}
	}
}

MediaRouteApplication::~MediaRouteApplication()
{
	logti("[%s(%u)] Destroyed Mediarouter application. worker(%d)", _application_info.GetVHostAppName().CStr(), _application_info.GetId(), _max_worker_thread_count);
}

bool MediaRouteApplication::Start()
{
	_kill_flag = false;

	for (uint32_t worker_id = 0; worker_id < _max_worker_thread_count; worker_id++)
	{
		try
		{
			auto inbound_thread = std::thread(&MediaRouteApplication::InboundWorkerThread, this, worker_id);
			pthread_setname_np(inbound_thread.native_handle(), "InboundWorker");
			_inbound_threads.push_back(std::move(inbound_thread));
		}
		catch (const std::system_error &e)
		{
			_kill_flag = true;
			logte("Failed to start Mediarouter application thread.");
			return false;
		}
	}

	for (uint32_t worker_id = 0; worker_id < _max_worker_thread_count; worker_id++)
	{
		try
		{
			auto outbound_thread = std::thread(&MediaRouteApplication::OutboundWorkerThread, this, worker_id);
			pthread_setname_np(outbound_thread.native_handle(), "OutboundWorker");
			_outbound_threads.push_back(std::move(outbound_thread));
		}
		catch (const std::system_error &e)
		{
			_kill_flag = true;
			logte("Failed to start Mediarouter application thread.");
			return false;
		}
	}

	logtt("[%s(%u)] Started Mediarouter application.", _application_info.GetVHostAppName().CStr(), _application_info.GetId());


	return true;
}

bool MediaRouteApplication::Stop()
{
	_kill_flag = true;

	for (auto &indicator : _inbound_stream_indicator)
	{
		indicator->Stop();
		indicator->Clear();
	}

	for (auto &indicator : _outbound_stream_indicator)
	{
		indicator->Stop();
		indicator->Clear();
	}

	for (auto &worker : _inbound_threads)
	{
		if (worker.joinable())
		{
			worker.join();
		}
	}

	for (auto &worker : _outbound_threads)
	{
		if (worker.joinable())
		{
			worker.join();
		}
	}

	_inbound_stream_indicator.clear();
	_outbound_stream_indicator.clear();
	_inbound_threads.clear();
	_outbound_threads.clear();

	_connectors.clear();
	_observers.clear();

	logtt("[%s(%u)] Mediarouter application has been stopped", _application_info.GetVHostAppName().CStr(), _application_info.GetId());

	return true;
}

const info::Application &MediaRouteApplication::GetApplicationInfo() const
{
	return _application_info;
}

// Called when an application is created
bool MediaRouteApplication::RegisterConnectorApp(std::shared_ptr<MediaRouterApplicationConnector> connector)
{
	std::lock_guard<std::shared_mutex> lock(_connectors_lock);

	if (connector == nullptr)
	{
		return false;
	}

	connector->SetMediaRouterApplication(GetSharedPtr());

	_connectors.push_back(connector);

	logtt("Registered connector. app(%s) type(%d)", _application_info.GetVHostAppName().CStr(), connector->GetConnectorType());

	return true;
}

// Called when an application is removed
bool MediaRouteApplication::UnregisterConnectorApp(std::shared_ptr<MediaRouterApplicationConnector> connector)
{
	std::lock_guard<std::shared_mutex> lock(_connectors_lock);

	if (!connector)
	{
		return false;
	}

	auto position = std::find(_connectors.begin(), _connectors.end(), connector);
	if (position == _connectors.end())
	{
		return true;
	}

	_connectors.erase(position);

	logti("Unregistered connector. app(%s) type(%d)", _application_info.GetVHostAppName().CStr(), connector->GetConnectorType());

	return true;
}

bool MediaRouteApplication::RegisterObserverApp(std::shared_ptr<MediaRouterApplicationObserver> observer)
{
	std::lock_guard<std::shared_mutex> lock(_observers_lock);

	if (!observer)
	{
		return false;
	}

	_observers.push_back(observer);

	logtt("Registered observer. app(%s) type(%d)", _application_info.GetVHostAppName().CStr(), observer->GetObserverType());

	return true;
}

bool MediaRouteApplication::UnregisterObserverApp(std::shared_ptr<MediaRouterApplicationObserver> observer)
{
	std::lock_guard<std::shared_mutex> lock(_observers_lock);

	if (!observer)
	{
		return false;
	}

	auto position = std::find(_observers.begin(), _observers.end(), observer);
	if (position == _observers.end())
	{
		return true;
	}

	_observers.erase(position);

	logti("Unregistered observer. app(%s) type(%d)", _application_info.GetVHostAppName().CStr(), observer->GetObserverType());

	return true;
}

CommonErrorCode MediaRouteApplication::MirrorStream(std::shared_ptr<MediaRouterStreamTap> &stream_tap, const ov::String &stream_name, MediaRouterInterface::MirrorPosition position)
{
	if (!stream_tap)
	{
		return CommonErrorCode::INVALID_PARAMETER;
	}

	std::shared_ptr<info::Stream> stream_info = nullptr;

	if (position == MediaRouterInterface::MirrorPosition::Inbound)
	{
		stream_info = GetInboundStreamByName(stream_name) != nullptr ? GetInboundStreamByName(stream_name)->GetStream() : nullptr;
	}
	else if (position == MediaRouterInterface::MirrorPosition::Outbound)
	{
		stream_info = GetOutboundStreamByName(stream_name) != nullptr ? GetOutboundStreamByName(stream_name)->GetStream() : nullptr;
	}

	if (!stream_info)
	{
		logtw("Failed to mirror stream. %s/%s is not found", _application_info.GetVHostAppName().CStr(), stream_name.CStr());
		return CommonErrorCode::NOT_FOUND;
	}

	stream_tap->SetStreamInfo(stream_info);
	stream_tap->SetState(MediaRouterStreamTap::State::Tapped);

	{
		std::lock_guard<std::shared_mutex> lock(_stream_taps_lock);
		_stream_taps.insert(std::make_pair(stream_info->GetId(), stream_tap));
	}

	return CommonErrorCode::SUCCESS;
}

CommonErrorCode MediaRouteApplication::UnmirrorStream(const std::shared_ptr<MediaRouterStreamTap> &stream_tap)
{
	if (!stream_tap)
	{
		return CommonErrorCode::INVALID_PARAMETER;
	}

	{
		std::lock_guard<std::shared_mutex> lock(_stream_taps_lock);
		auto it = _stream_taps.equal_range(stream_tap->GetStreamInfo()->GetId());
		for (auto iter = it.first; iter != it.second; ++iter)
		{
			if (iter->second == stream_tap)
			{
				iter->second->SetState(MediaRouterStreamTap::State::UnTapped);
				_stream_taps.erase(iter);
				break;
			}
		}
	}

	return CommonErrorCode::SUCCESS;
}

bool MediaRouteApplication::UnmirrorStream(const std::shared_ptr<info::Stream> &stream)
{
	if (!stream)
	{
		return false;
	}

	// Change the state of the stream tap to UnTapped
	{
		std::shared_lock<std::shared_mutex> lock(_stream_taps_lock);
		auto it = _stream_taps.equal_range(stream->GetId());
		for (auto iter = it.first; iter != it.second; ++iter)
		{
			iter->second->SetState(MediaRouterStreamTap::State::UnTapped);
		}
	}

	// Remove the stream tap from the list
	{
		std::lock_guard<std::shared_mutex> lock(_stream_taps_lock);
		_stream_taps.erase(stream->GetId());
	}

	return true;
}

// OnStreamCreated is called from Provider, Transcoder
bool MediaRouteApplication::OnStreamCreated(const std::shared_ptr<MediaRouterApplicationConnector> &app_conn, const std::shared_ptr<info::Stream> &stream_info)
{
	if (!app_conn || !stream_info)
	{
		OV_ASSERT2(false);
		return false;
	}

	logti("[%s/%s(%u)] Trying to create a stream", _application_info.GetVHostAppName().CStr(), stream_info->GetName().CStr(), stream_info->GetId());

	auto connector_type = app_conn->GetConnectorType();
	auto representation_type = stream_info->GetRepresentationType();

	std::shared_ptr<MediaRouteStream> stream = nullptr;

	if (IS_CONNECTOR_PROVIDER(connector_type) && IS_REPRENT_SOURCE(representation_type))
	{
		// Check there is a duplicate inbound stream
		if (GetInboundStreamByName(stream_info->GetName()) != nullptr)
		{
			logtw("[%s/%s] Reject stream creation : there is already an inbound stream with the same name.", stream_info->GetApplicationName(), stream_info->GetName().CStr());
			return false;
		}

		stream = CreateInboundStream(stream_info);
		if (stream == nullptr)
		{
			return false;
		}
	}
	else if ((IS_CONNECTOR_PROVIDER(connector_type) && IS_REPRENT_RELAY(representation_type)) ||
			 (IS_CONNECTOR_TRANSCODER(connector_type)))
	{
		if (GetOutboundStreamByName(stream_info->GetName()) != nullptr)
		{
			logtw("Reject stream creation : there is already an outbound stream with the same name. (%s/%s)", stream_info->GetApplicationName(), stream_info->GetName().CStr());
			return false;
		}

		stream = CreateOutboundStream(stream_info);
		if (stream == nullptr)
		{
			return false;
		}
	}
	else
	{
		logte("Unknown connector");
		return false;
	}

	// For Monitoring
	mon::Monitoring::GetInstance()->OnStreamCreated(*stream_info);

	// Create Stream Flow
	// - from Provider : The Observer is not notified that a stream has been created.
	//              Because, after the track information parsing in the inbound stream,
	// 			    Notifies the Observer that streaming has been created.
	// - from Transcoder : Notify to Observer(Publisher)
	// - from Relay : Notify to Observer(Publisher)
	if (!NotifyStreamCreate(stream->GetStream(), connector_type))
	{
		return false;
	}

	// If all track information is validity, Notify the observer that the current stream is prepared.
	if (stream->IsStreamPrepared() == false && stream->IsStreamReady() == true)
	{
		NotifyStreamPrepared(stream);
	}

	return true;
}

std::shared_ptr<MediaRouteStream> MediaRouteApplication::CreateInboundStream(const std::shared_ptr<info::Stream> &stream_info)
{
	std::lock_guard<std::shared_mutex> lock_guard(_streams_lock);

	auto new_stream = std::make_shared<MediaRouteStream>(stream_info, cmn::MediaRouterStreamType::INBOUND);
	if (!new_stream)
	{
		return nullptr;
	}

	_inbound_streams.insert(std::make_pair(stream_info->GetId(), new_stream));

	return new_stream;
}

std::shared_ptr<MediaRouteStream> MediaRouteApplication::CreateOutboundStream(const std::shared_ptr<info::Stream> &stream_info)
{
	std::lock_guard<std::shared_mutex> lock_guard(_streams_lock);

	// Since the publisher creates the stream by copying the stream_info, 
	// it eventually loses the link to the original stream. 
	// For this, the relay stream must also be linked to the input stream.
	auto out_stream_info = stream_info;
	if (stream_info->GetRepresentationType() == StreamRepresentationType::Relay)
	{
		out_stream_info = std::make_shared<info::Stream>(*stream_info);
		if (!out_stream_info)
		{
			return nullptr;
		}
		out_stream_info->LinkInputStream(stream_info);
	}

	auto new_stream = std::make_shared<MediaRouteStream>(out_stream_info, cmn::MediaRouterStreamType::OUTBOUND);
	if (!new_stream)
	{
		return nullptr;
	}
	
	_outbound_streams.insert(std::make_pair(out_stream_info->GetId(), new_stream));

	return new_stream;
}

bool MediaRouteApplication::NotifyStreamCreate(const std::shared_ptr<info::Stream> &stream_info, MediaRouterApplicationConnector::ConnectorType connector_type)
{
	std::shared_lock<std::shared_mutex> lock(_observers_lock);

	logti("[%s/%s(%u)] Stream has been created", _application_info.GetVHostAppName().CStr(), stream_info->GetName().CStr(), stream_info->GetId());

	auto representation_type = stream_info->GetRepresentationType();

	for (auto observer : _observers)
	{
		auto observer_type = observer->GetObserverType();

		// Provider(source) => Transcoder, Relay(not used)
		if (IS_CONNECTOR_PROVIDER(connector_type) && IS_REPRENT_SOURCE(representation_type))
		{
			if (IS_OBSERVER_TRANSCODER(observer_type) || IS_OBSERVER_RELAY(observer_type) || IS_OBSERVER_ORCHESTRATOR(observer_type) )
			{
				logtt("[%s/%s(%u)] Notify created stream to relay or transcoder", stream_info->GetApplicationName(), stream_info->GetName().CStr(), stream_info->GetId());

				observer->OnStreamCreated(stream_info);
			}
		}
		// Provider(relay), Transcoder => Publisher
		else if ((IS_CONNECTOR_PROVIDER(connector_type) && IS_REPRENT_RELAY(representation_type)) || (IS_CONNECTOR_TRANSCODER(connector_type)))
		{
			if (IS_OBSERVER_PUBLISHER(observer_type) || IS_OBSERVER_ORCHESTRATOR(observer_type))
			{
				logtt("[%s/%s(%u)] Notify created stream to publisher", stream_info->GetApplicationName(), stream_info->GetName().CStr(), stream_info->GetId());

				observer->OnStreamCreated(stream_info);
			}
		}
	}

	return true;
}

bool MediaRouteApplication::NotifyStreamPrepared(std::shared_ptr<MediaRouteStream> &stream)
{
	std::shared_lock<std::shared_mutex> lock(_observers_lock);
	auto observers = _observers;  // Avoid deadlock
	lock.unlock();

	logti("[%s/%s(%u)] Stream has been prepared %s", _application_info.GetVHostAppName().CStr(), stream->GetStream()->GetName().CStr(), stream->GetStream()->GetId(), stream->GetStream()->GetInfoString().CStr());

	for (auto observer : observers)
	{
		auto observer_type = observer->GetObserverType();

		if (stream->IsInbound())
		{
			if (IS_OBSERVER_TRANSCODER(observer_type) || IS_OBSERVER_ORCHESTRATOR(observer_type))
			{
				logtt("[%s/%s(%u)] Notify prepared stream to transcoder", stream->GetStream()->GetApplicationName(), stream->GetStream()->GetName().CStr(), stream->GetStream()->GetId());

				observer->OnStreamPrepared(stream->GetStream());
			}
		}
		else if (stream->IsOutbound())
		{
			if (IS_OBSERVER_PUBLISHER(observer_type) || IS_OBSERVER_RELAY(observer_type) || IS_OBSERVER_ORCHESTRATOR(observer_type))
			{
				logtt("[%s/%s(%u)] Notify prepared stream to publisher", stream->GetStream()->GetApplicationName(), stream->GetStream()->GetName().CStr(), stream->GetStream()->GetId());

				observer->OnStreamPrepared(stream->GetStream());
			}
		}
		else
		{
			logtw("Unknown type of stream");
			return false;
		}
	}

	stream->OnStreamPrepared(true);

	MonitorInstance->OnStreamPrepared(*stream->GetStream());

	return true;
}

bool MediaRouteApplication::OnStreamUpdated(const std::shared_ptr<MediaRouterApplicationConnector> &app_conn, const std::shared_ptr<info::Stream> &stream_info)
{
	logti(" [%s/%s(%u)] Trying to update a stream", _application_info.GetVHostAppName().CStr(), stream_info->GetName().CStr(), stream_info->GetId());

	if (!app_conn || !stream_info)
	{
		logte("Invalid arguments: connector: %p, stream: %p", app_conn.get(), stream_info.get());
		return false;
	}

	auto connector_type = app_conn->GetConnectorType();
	auto representation_type = stream_info->GetRepresentationType();

	// For Monitoring
	mon::Monitoring::GetInstance()->OnStreamUpdated(*stream_info);

	// Provider(source) => Inbound Stream
	if (IS_CONNECTOR_PROVIDER(connector_type) && IS_REPRENT_SOURCE(representation_type))
	{
		auto stream = GetInboundStream(stream_info->GetId());
		if (stream)
		{
			//stream->Flush();
		}
	}
	// Provider(relay), Transcoder => Outbound Stream
	else if ((IS_CONNECTOR_PROVIDER(connector_type) && IS_REPRENT_RELAY(representation_type)) ||
			 (IS_CONNECTOR_TRANSCODER(connector_type)))
	{
		auto stream = GetOutboundStream(stream_info->GetId());
		if (stream)
		{
			//stream->Flush();
		}
	}
	else
	{
		logte("Unknown connector");
		return false;
	}

	if (!NotifyStreamUpdated(stream_info, app_conn->GetConnectorType()))
	{
		return false;
	}

	return true;
}

bool MediaRouteApplication::OnStreamDeleted(const std::shared_ptr<MediaRouterApplicationConnector> &app_conn, const std::shared_ptr<info::Stream> &stream_info)
{
	logti("[%s/%s(%u)] Trying to delete a stream", _application_info.GetVHostAppName().CStr(), stream_info->GetName().CStr(), stream_info->GetId());

	if (!app_conn || !stream_info)
	{
		logte("Invalid arguments: connector: %p, stream: %p", app_conn.get(), stream_info.get());
		return false;
	}

	auto connector_type = app_conn->GetConnectorType();
	auto representation_type = stream_info->GetRepresentationType();

	// For Monitoring
	mon::Monitoring::GetInstance()->OnStreamDeleted(*stream_info);

	if (!NotifyStreamDeleted(stream_info, app_conn->GetConnectorType()))
	{
		return false;
	}

	// Provider(source) => Inbound Stream
	if (IS_CONNECTOR_PROVIDER(connector_type) && IS_REPRENT_SOURCE(representation_type))
	{
		DeleteInboundStream(stream_info);
	}
	// Provider(relay), Transcoder => Outbound Stream
	else if ((IS_CONNECTOR_PROVIDER(connector_type) && IS_REPRENT_RELAY(representation_type)) ||
			 (IS_CONNECTOR_TRANSCODER(connector_type)))
	{
		DeleteOutboundStream(stream_info);
	}
	else
	{
		logte("Unknown connector");
		return false;
	}

	// Unmirror Stream
	UnmirrorStream(stream_info);

	return true;
}

bool MediaRouteApplication::DeleteInboundStream(const std::shared_ptr<info::Stream> &stream_info)
{
	// Delete connected stream taps
	{
		std::shared_lock<std::shared_mutex> lock(_stream_taps_lock);
		auto it = _stream_taps.equal_range(stream_info->GetId());
		for (auto iter = it.first; iter != it.second; ++iter)
		{
			auto stream_tap = iter->second;

			if (stream_tap->GetState() == MediaRouterStreamTap::State::Tapped)
			{
				logtt("Deleting connected stream tap for inbound stream. %s/%s", stream_info->GetApplicationName(), stream_info->GetName().CStr());
				stream_tap->Stop();
			}
		}
	}
	std::lock_guard<std::shared_mutex> lock_guard(_streams_lock);
	_inbound_streams.erase(stream_info->GetId());

	return true;
}
bool MediaRouteApplication::DeleteOutboundStream(const std::shared_ptr<info::Stream> &stream_info)
{
	std::lock_guard<std::shared_mutex> lock_guard(_streams_lock);
	_outbound_streams.erase(stream_info->GetId());

	return true;
}

bool MediaRouteApplication::NotifyStreamDeleted(const std::shared_ptr<info::Stream> &stream_info, const MediaRouterApplicationConnector::ConnectorType connector_type)
{
	std::shared_lock<std::shared_mutex> lock_guard(_observers_lock);
	auto observers = _observers; // Avoid deadlock
	lock_guard.unlock();

	auto representation_type = stream_info->GetRepresentationType();

	for (auto it = observers.begin(); it != observers.end(); ++it)
	{
		auto observer = *it;

		auto observer_type = observer->GetObserverType();

		// Provider(source) => Transcoder, Relay(not used), Orchestrator
		if (IS_CONNECTOR_PROVIDER(connector_type) && IS_REPRENT_SOURCE(representation_type))
		{
			if (IS_OBSERVER_TRANSCODER(observer_type) ||
				IS_OBSERVER_RELAY(observer_type) ||
				IS_OBSERVER_ORCHESTRATOR(observer_type))
			{
				logtt("Notify deleted stream from provider(source) to transcoder, relay, orchestrator.  %s/%s", stream_info->GetApplicationName(), stream_info->GetName().CStr());
				observer->OnStreamDeleted(stream_info);
			}
		}
		// Provider(Relay), Transcoder  => Publisher, Relay(not used), Orchestrator
		else if ((IS_CONNECTOR_PROVIDER(connector_type) && IS_REPRENT_RELAY(representation_type)) ||
				 (IS_CONNECTOR_TRANSCODER(connector_type)))
		{
			if (IS_OBSERVER_PUBLISHER(observer_type) ||
				IS_OBSERVER_RELAY(observer_type) ||
				IS_OBSERVER_ORCHESTRATOR(observer_type))
			{
				logtt("Notify deleted stream from transcoder, provider(relay) to publisher, relay, orchestrator.  %s/%s", stream_info->GetApplicationName(), stream_info->GetName().CStr());
				observer->OnStreamDeleted(stream_info);
			}
		}
		else
		{
			logtw("Unknown Connector");
		}
	}

	return true;
}

bool MediaRouteApplication::NotifyStreamUpdated(const std::shared_ptr<info::Stream> &stream_info, const MediaRouterApplicationConnector::ConnectorType connector_type)
{
	std::shared_lock<std::shared_mutex> lock_guard(_observers_lock);
	auto observers = _observers; // Avoid deadlock
	lock_guard.unlock();

	auto representation_type = stream_info->GetRepresentationType();

	for (auto it = observers.begin(); it != observers.end(); ++it)
	{
		auto observer = *it;

		auto observer_type = observer->GetObserverType();

		// Provider(source) => Transcoder, Relay(not used), Orchestrator
		if (IS_CONNECTOR_PROVIDER(connector_type) && IS_REPRENT_SOURCE(representation_type))
		{
			if (IS_OBSERVER_TRANSCODER(observer_type) ||
				IS_OBSERVER_RELAY(observer_type) ||
				IS_OBSERVER_ORCHESTRATOR(observer_type))
			{
				logtt("[%s/%s] Notify updated stream from provider(source) to transcoder, relay, orchestrator", stream_info->GetApplicationName(), stream_info->GetName().CStr());
				observer->OnStreamUpdated(stream_info);
			}
		}
		// Provider(Relay), Transcoder  => Publisher, Relay(not used), Orchestrator
		else if ((IS_CONNECTOR_PROVIDER(connector_type) && IS_REPRENT_RELAY(representation_type)) ||
				 (IS_CONNECTOR_TRANSCODER(connector_type)))
		{
			if (IS_OBSERVER_PUBLISHER(observer_type) ||
				IS_OBSERVER_RELAY(observer_type) ||
				IS_OBSERVER_ORCHESTRATOR(observer_type))
			{
				logtt("[%s/%s] Notify updated stream from transcoder, provider(relay) to publisher, relay, orchestrator", stream_info->GetApplicationName(), stream_info->GetName().CStr());
				observer->OnStreamUpdated(stream_info);
			}
		}
		else
		{
			logtw("Unknown Connector");
		}
	}

	return true;
}

// @from Provider
// @from TranscoderProvider
bool MediaRouteApplication::OnPacketReceived(const std::shared_ptr<MediaRouterApplicationConnector> &app_conn, const std::shared_ptr<info::Stream> &stream_info, const std::shared_ptr<MediaPacket> &packet)
{
	if (!app_conn || !stream_info)
	{
		return false;
	}

	auto connector_type = app_conn->GetConnectorType();
	auto representation_type = stream_info->GetRepresentationType();

	// Provider(source) => Inbound Stream
	if (IS_CONNECTOR_PROVIDER(connector_type) && IS_REPRENT_SOURCE(representation_type))
	{
		auto stream = GetInboundStream(stream_info->GetId());
		if (!stream)
		{
			return false;
		}

		stream->Push(packet);

		_inbound_stream_indicator[GetWorkerIDByStreamID(stream_info->GetId())]->Enqueue(stream, packet->IsHighPriority());
	}
	// Provider(relay), Transcoder => Outbound Stream
	else if ((IS_CONNECTOR_PROVIDER(connector_type) && IS_REPRENT_RELAY(representation_type)) ||
			 (IS_CONNECTOR_TRANSCODER(connector_type)))
	{
		auto stream = GetOutboundStream(stream_info->GetId());
		if (!stream)
		{
			return false;
		}

		stream->Push(packet);

		_outbound_stream_indicator[GetWorkerIDByStreamID(stream_info->GetId())]->Enqueue(stream, packet->IsHighPriority());
	}
	else
	{
		logte("Unknown connector");
		return false;
	}

	return true;
}

std::shared_ptr<MediaRouteStream> MediaRouteApplication::GetInboundStream(uint32_t stream_id)
{
	std::shared_lock<std::shared_mutex> lock_guard(_streams_lock);

	auto bucket = _inbound_streams.find(stream_id);
	if (bucket == _inbound_streams.end())
	{
		return nullptr;
	}

	return bucket->second;
}

std::shared_ptr<MediaRouteStream> MediaRouteApplication::GetOutboundStream(uint32_t stream_id)
{
	std::shared_lock<std::shared_mutex> lock_guard(_streams_lock);

	auto bucket = _outbound_streams.find(stream_id);
	if (bucket == _outbound_streams.end())
	{
		return nullptr;
	}

	return bucket->second;
}

std::shared_ptr<MediaRouteStream> MediaRouteApplication::GetInboundStreamByName(const ov::String stream_name)
{
	std::shared_lock<std::shared_mutex> lock_guard(_streams_lock);

	for (const auto &item : _inbound_streams)
	{
		auto stream = item.second;
		if (stream->GetStream()->GetName() == stream_name)
		{
			return stream;
		}
	}

	return nullptr;
}

std::shared_ptr<MediaRouteStream> MediaRouteApplication::GetOutboundStreamByName(const ov::String stream_name)
{
	std::shared_lock<std::shared_mutex> lock_guard(_streams_lock);

	for (const auto &item : _outbound_streams)
	{
		auto stream = item.second;
		if (stream->GetStream()->GetName() == stream_name)
		{
			return stream;
		}
	}

	return nullptr;
}

bool MediaRouteApplication::IsExistingInboundStream(ov::String stream_name)
{
	std::shared_lock<std::shared_mutex> lock_guard(_streams_lock);

	for (const auto &item : _inbound_streams)
	{
		auto stream = item.second;
		if (stream->GetStream()->GetName() == stream_name)
		{
			return true;
		}
	}

	return false;
}

uint32_t MediaRouteApplication::GetWorkerIDByStreamID(info::stream_id_t stream_id)
{
	return stream_id % _max_worker_thread_count;
}

void MediaRouteApplication::InboundWorkerThread(uint32_t worker_id)
{
	ov::logger::ThreadHelper thread_helper;

	logtt("Created Inbound worker thread #%d", worker_id);

	while (!_kill_flag)
	{
		auto msg = _inbound_stream_indicator[worker_id]->Dequeue(ov::Infinite);
		if (msg.has_value() == false)
		{
			// It may be called due to a normal stop signal.
			continue;
		}

		auto stream = msg.value();
		if (stream == nullptr)
		{
			logtw("Not found stream info");
			continue;
		}

		// StreamDeliver media packet to Publisher(observer) of Transcoder(observer)
		auto media_packet = stream->PopAndNormalize();
		if (media_packet == nullptr)
		{
			continue;
		}

		// When the inbound stream is finished parsing track information,
		// Notify the Observer that the stream is parsed
		if (stream->IsStreamPrepared() == false && stream->IsStreamReady() == true)
		{
			NotifyStreamPrepared(stream);
		}

		std::shared_lock<std::shared_mutex> lock(_observers_lock);
		auto observers = _observers; // Avoid deadlock
		lock.unlock();
		for (const auto &observer : observers)
		{
			auto observer_type = observer->GetObserverType();

			if (observer_type == MediaRouterApplicationObserver::ObserverType::Transcoder)
			{
				// Get Stream Info
				auto stream_info = stream->GetStream();

				// observer->OnSendFrame(stream_info, std::move(media_packet->ClonePacket()));
				observer->OnSendFrame(stream_info, media_packet);
			}
		}

		// Mirror stream
		{
			std::shared_lock<std::shared_mutex> lock(_stream_taps_lock);
			auto it = _stream_taps.equal_range(stream->GetStream()->GetId());
			for (auto iter = it.first; iter != it.second; ++iter)
			{
				auto stream_tap = iter->second;

				if (stream_tap->GetState() == MediaRouterStreamTap::State::Tapped)
				{
					if (stream_tap->DoesNeedPastData())
					{
						stream_tap->SetNeedPastData(false);

						for (const auto &item : stream->GetMirrorBuffer())
						{
							if (item->GetElapsedMilliseconds() < MEDIA_ROUTE_STREAM_MAX_MIRROR_BUFFER_SIZE_MS)
							{
								stream_tap->Push(item->packet);
							}
						}
					}
					else
					{
						stream_tap->Push(media_packet);
					}
				}
			}
		}
	}

	logtt("Inbound worker thread #%d has been stopped", worker_id);
}

void MediaRouteApplication::OutboundWorkerThread(uint32_t worker_id)
{
	ov::logger::ThreadHelper thread_helper;

	logtt("Created outbound worker thread #%d", worker_id);

	while (!_kill_flag)
	{
		auto msg = _outbound_stream_indicator[worker_id]->Dequeue(ov::Infinite);
		if (msg.has_value() == false)
		{
			// It may be called due to a normal stop signal.
			continue;
		}
		auto stream = msg.value();
		if (stream == nullptr)
		{
			logtw("Not found stream info");
			continue;
		}

		// check stream is exist, there can be removed streams packet because of delay buffer
		if (GetOutboundStream(stream->GetStream()->GetId()) == nullptr)
		{
			continue;
		}

		// StreamDeliver media packet to Publisher(observer) of Transcoder(observer)
		auto media_packet = stream->PopAndNormalize();
		if (media_packet == nullptr)
		{
			continue;
		}

		if (stream->IsStreamPrepared() == false && stream->IsStreamReady() == true)
		{
			NotifyStreamPrepared(stream);
		}

		std::shared_lock<std::shared_mutex> lock(_observers_lock);
		auto observers = _observers; // Avoid deadlock
		lock.unlock();
		for (const auto &observer : observers)
		{
			auto observer_type = observer->GetObserverType();

			if (observer_type == MediaRouterApplicationObserver::ObserverType::Publisher)
			{
				// Get Stream Info
				auto stream_info = stream->GetStream();
				observer->OnSendFrame(stream_info, media_packet);
			}
		}

		// mirror stream
		{
			std::shared_lock<std::shared_mutex> lock(_stream_taps_lock);
			auto it = _stream_taps.equal_range(stream->GetStream()->GetId());
			for (auto iter = it.first; iter != it.second; ++iter)
			{
				auto stream_tap = iter->second;
				if (stream_tap->GetState() == MediaRouterStreamTap::State::Tapped)
				{
					if (stream_tap->DoesNeedPastData())
					{
						stream_tap->SetNeedPastData(false);

						for (const auto &item : stream->GetMirrorBuffer())
						{
							if (item->GetElapsedMilliseconds() < MEDIA_ROUTE_STREAM_MAX_MIRROR_BUFFER_SIZE_MS)
							{
								stream_tap->Push(item->packet);
							}
						}
					}
					else
					{
						stream_tap->Push(media_packet);
					}
				}
			}
		}
	}

	logtt("Outbound worker thread #%d has been stopped", worker_id);
}
