#include "application.h"

#include <algorithm>

#include "publisher.h"
#include "publisher_private.h"
#include <base/event/media_event.h>

namespace pub
{
	ApplicationWorker::ApplicationWorker(uint32_t worker_id, ov::String vhost_app_name, ov::String worker_name)
		: _stream_data_queue(nullptr, 500)
	{
		_worker_id = worker_id;
		_vhost_app_name = vhost_app_name;
		_worker_name = worker_name;
		_stop_thread_flag = false;
	}

	bool ApplicationWorker::Start()
	{
		_stop_thread_flag = false;
		_worker_thread = std::thread(&ApplicationWorker::WorkerThread, this);

		auto name = ov::String::FormatString("AW-%s%d", _worker_name.CStr(), _worker_id);
		pthread_setname_np(_worker_thread.native_handle(), name.CStr());

		auto urn = std::make_shared<info::ManagedQueue::URN>(
			_vhost_app_name,
			nullptr,
			"pub",
			name.LowerCaseString());

		_stream_data_queue.SetUrn(urn);

		logtt("%s ApplicationWorker has been created", _worker_name.CStr());

		return true;
	}

	bool ApplicationWorker::Stop()
	{
		if (_stop_thread_flag == true)
		{
			return true;
		}

		_stream_data_queue.Clear();

		_stop_thread_flag = true;

		_queue_event.Stop();

		if (_worker_thread.joinable())
		{
			_worker_thread.join();
		}

		return true;
	}

	uint32_t ApplicationWorker::GetWorkerId() const
	{
		return _worker_id;
	}

	void ApplicationWorker::OnStreamCreated(const std::shared_ptr<info::Stream> &info)
	{
		logti("Stream(%s/%u) created on AppWorker (%s / %d)", info->GetName().CStr(), info->GetId(), _worker_name.CStr(), _worker_id);
		_stream_count++;
	}

	void ApplicationWorker::OnStreamDeleted(const std::shared_ptr<info::Stream> &info)
	{
		logti("Stream(%s/%u) deleted on AppWorker (%s / %d)", info->GetName().CStr(), info->GetId(), _worker_name.CStr(), _worker_id);
		_stream_count--;
	}

	uint32_t ApplicationWorker::GetStreamCount() const
	{
		return _stream_count.load();
	}

	bool ApplicationWorker::PushMediaPacket(const std::shared_ptr<Stream> &stream, const std::shared_ptr<MediaPacket> &media_packet)
	{
		auto data = std::make_shared<ApplicationWorker::StreamData>(stream, media_packet);
		_stream_data_queue.Enqueue(std::move(data));

		_queue_event.Notify();

		return true;
	}

	std::shared_ptr<ApplicationWorker::StreamData> ApplicationWorker::PopStreamData()
	{
		if (_stream_data_queue.IsEmpty())
		{
			return nullptr;
		}

		auto data = _stream_data_queue.Dequeue(0);
		if (data.has_value())
		{
			return data.value();
		}

		return nullptr;
	}

	void ApplicationWorker::WorkerThread()
	{
		ov::logger::ThreadHelper thread_helper;

		while (!_stop_thread_flag)
		{
			_queue_event.Wait();

			// Check media data is available
			auto stream_data = PopStreamData();
			if (stream_data == nullptr)
			{
				continue;
			}
			
			auto stream = stream_data->_stream;
			auto media_packet = stream_data->_media_packet;
			if (stream == nullptr || media_packet == nullptr)
			{
				continue;
			}

			// State::CREATED could be needed for some cases
			if (stream->GetState() == Stream::State::ERROR || stream->GetState() == Stream::State::STOPPED)
			{
				continue;
			}

			if (media_packet->GetMediaType() == cmn::MediaType::Video)
			{
				stream->SendVideoFrame(stream_data->_media_packet);
			}
			else if (media_packet->GetMediaType() == cmn::MediaType::Audio)
			{
				stream->SendAudioFrame(stream_data->_media_packet);
			}
			else if (media_packet->GetMediaType() == cmn::MediaType::Data || 
						media_packet->GetMediaType() == cmn::MediaType::Subtitle)
			{
				if (media_packet->GetBitstreamFormat() == cmn::BitstreamFormat::OVEN_EVENT)
				{
					auto event = std::dynamic_pointer_cast<MediaEvent>(media_packet);
					if (event == nullptr)
					{
						logtw("MediaPacket is not MediaEvent. Cannot process event. %s/%s(%u)", stream->GetApplicationName(), stream->GetName().CStr(), stream->GetId());
						continue;
					}

					stream->ProcessEvent(event);
					stream->OnEvent(event);
				}
				else
				{
					stream->SendDataFrame(stream_data->_media_packet);
				}
			}
			else
			{
				// Nothing can do
			}
		}
	}

	Application::Application(const std::shared_ptr<Publisher> &publisher, const info::Application &application_info)
		: info::Application(application_info)
	{
		_publisher = publisher;
	}

	Application::~Application()
	{
		Stop();
	}

	const char *Application::GetApplicationTypeName()
	{
		if (_publisher == nullptr)
		{
			return "";
		}

		if (_app_type_name.IsEmpty())
		{
			_app_type_name.Format("%s %s", _publisher->GetPublisherName(), "Application");
		}

		return _app_type_name.CStr();
	}

	const char *Application::GetPublisherTypeName()
	{
		if (_publisher_type_name.IsEmpty())
		{
			_publisher_type_name = StringFromPublisherType(_publisher->GetPublisherType());
		}

		return _publisher_type_name.CStr();
	}

	bool Application::Start()
	{
		_application_worker_count = GetConfig().GetAppWorkerCount();
		if (_application_worker_count < MIN_APPLICATION_WORKER_COUNT)
		{
			_application_worker_count = MIN_APPLICATION_WORKER_COUNT;
		}
		if (_application_worker_count > MAX_APPLICATION_WORKER_COUNT)
		{
			_application_worker_count = MAX_APPLICATION_WORKER_COUNT;
		}

		std::lock_guard<std::shared_mutex> worker_lock(_application_worker_lock);

		for (uint32_t i = 0; i < _application_worker_count; i++)
		{
			auto app_worker = std::make_shared<ApplicationWorker>(i, GetVHostAppName().CStr(), StringFromPublisherType(_publisher->GetPublisherType()));
			if (app_worker->Start() == false)
			{
				logte("Cannot create ApplicationWorker (%s/%s/%d)", GetApplicationTypeName(), GetVHostAppName().CStr(), i);
				Stop();

				return false;
			}

			_application_workers.push_back(app_worker);
		}

		logti("%s has created [%s] application", GetApplicationTypeName(), GetVHostAppName().CStr());

		return true;
	}

	bool Application::Stop()
	{
		std::unique_lock<std::shared_mutex> worker_lock(_application_worker_lock);
		for (const auto &worker : _application_workers)
		{
			worker->Stop();
		}

		_application_workers.clear();
		worker_lock.unlock();

		// release remaining streams
		DeleteAllStreams();

		logti("%s has deleted [%s] application", GetApplicationTypeName(), GetVHostAppName().CStr());

		return true;
	}

	bool Application::DeleteAllStreams()
	{
		std::unique_lock<std::shared_mutex> lock(_stream_map_mutex);

		for (const auto &x : _streams)
		{
			auto stream = x.second;
			stream->Stop();
		}

		_streams.clear();

		return true;
	}

	// Called by MediaRouterApplicationObserver
	bool Application::OnStreamCreated(const std::shared_ptr<info::Stream> &info)
	{
		auto stream_worker_count = GetConfig().GetStreamWorkerCount();

		auto stream = CreateStream(info, stream_worker_count);
		if (!stream)
		{
			return false;
		}

		MapStreamToWorker(info);

		std::lock_guard<std::shared_mutex> lock(_stream_map_mutex);
		_streams[info->GetId()] = stream;

		return true;
	}

	bool Application::OnStreamDeleted(const std::shared_ptr<info::Stream> &info)
	{
		std::unique_lock<std::shared_mutex> lock(_stream_map_mutex);

		auto stream_it = _streams.find(info->GetId());
		if (stream_it == _streams.end())
		{
			// Sometimes stream rejects stream creation if the input codec is not supported. So this is a normal situation.
			logtt("OnStreamDeleted failed. Cannot find stream : %s/%u", info->GetName().CStr(), info->GetId());
			return true;
		}

		auto stream = stream_it->second;

		lock.unlock();

		UnmapStreamToWorker(info);

		if (DeleteStream(info) == false)
		{
			return false;
		}

		lock.lock();
		_streams.erase(info->GetId());

		// Stop stream
		stream->Stop();

		return true;
	}

	bool Application::OnStreamPrepared(const std::shared_ptr<info::Stream> &info)
	{
		std::shared_lock<std::shared_mutex> lock(_stream_map_mutex);

		auto stream_it = _streams.find(info->GetId());
		if (stream_it == _streams.end())
		{
			// Sometimes stream rejects stream creation if the input codec is not supported. So this is a normal situation.
			logtt("OnStreamPrepared failed. Cannot find stream : %s/%u", info->GetName().CStr(), info->GetId());
			return true;
		}

		auto stream = stream_it->second;

		lock.unlock();

		// Start stream
		if (stream->Start() == false)
		{
			stream->SetState(Stream::State::ERROR);
			logtw("%s could not start [%s] stream.", GetApplicationTypeName(), info->GetName().CStr(), info->GetId());
			return false;
		}

		return true;
	}

	bool Application::OnStreamUpdated(const std::shared_ptr<info::Stream> &info)
	{
		std::shared_lock<std::shared_mutex> lock(_stream_map_mutex);

		auto stream_it = _streams.find(info->GetId());
		if (stream_it == _streams.end())
		{
			// Sometimes stream rejects stream creation if the input codec is not supported. So this is a normal situation.
			logtt("OnStreamUpdated failed. Cannot find stream : %s/%u", info->GetName().CStr(), info->GetId());
			return true;
		}

		auto stream = stream_it->second;

		lock.unlock();

		return stream->OnStreamUpdated(info);
	}

	std::shared_ptr<ApplicationWorker> Application::GetLowestLoadWorker()
	{
		std::shared_lock lock(_application_worker_lock);
		uint32_t min_load = UINT32_MAX;
		uint32_t min_load_worker_id = 0;

		for (const auto &worker : _application_workers)
		{
			auto stream_count = worker->GetStreamCount();
			if (stream_count < min_load)
			{
				min_load = stream_count;
				min_load_worker_id = worker->GetWorkerId();

				if (min_load == 0)
				{
					break;
				}
			}
		}

		return _application_workers[min_load_worker_id];
	}

	void Application::MapStreamToWorker(const std::shared_ptr<info::Stream> &info)
	{
		auto app_worker = GetLowestLoadWorker();
		if (app_worker == nullptr)
		{
			logte("Cannot find ApplicationWorker for stream mapping. %s / %u", info->GetName().CStr(), info->GetId());
			return;
		}

		app_worker->OnStreamCreated(info);

		std::unique_lock<std::shared_mutex> lock(_stream_app_worker_map_lock);
		_stream_app_worker_map[info->GetId()] = app_worker->GetWorkerId();
	}
	
	void Application::UnmapStreamToWorker(const std::shared_ptr<info::Stream> &info)
	{
		auto app_worker = GetWorkerByStreamID(info->GetId());
		if (app_worker != nullptr)
		{
			app_worker->OnStreamDeleted(info);
		}
		else
		{
			logte("Cannot find ApplicationWorker for stream unmapping. %s / %u", info->GetName().CStr(), info->GetId());
		}

		std::unique_lock<std::shared_mutex> lock(_stream_app_worker_map_lock);
		_stream_app_worker_map.erase(info->GetId());
	}

	std::shared_ptr<ApplicationWorker> Application::GetWorkerByStreamID(info::stream_id_t stream_id)
	{
		if (_application_worker_count == 0)
		{
			return nullptr;
		}
		
		uint32_t worker_id = 0;

		{
			std::shared_lock<std::shared_mutex> lock(_stream_app_worker_map_lock);
			auto it = _stream_app_worker_map.find(stream_id);
			if (it == _stream_app_worker_map.end())
			{
				logte("(%s/%s) cannot find ApplicationWorker for stream mapping. %u", GetApplicationTypeName(), GetVHostAppName().CStr(), stream_id);
				return nullptr;
			}

			worker_id = it->second;
		}

		std::shared_lock<std::shared_mutex> lock(_application_worker_lock);
		if (worker_id >= _application_workers.size())
		{
			logte("Cannot find ApplicationWorker for stream mapping. %u", stream_id);
			return nullptr;
		}

		return _application_workers[worker_id];
	}

	bool Application::OnSendFrame(const std::shared_ptr<info::Stream> &stream,
								  const std::shared_ptr<MediaPacket> &media_packet)
	{
		auto application_worker = GetWorkerByStreamID(stream->GetId());
		if (application_worker == nullptr)
		{
			return false;
		}

		return application_worker->PushMediaPacket(GetStream(stream->GetId()), media_packet);
	}

	uint32_t Application::GetStreamCount()
	{
		return _streams.size();
	}

	std::shared_ptr<Stream> Application::GetStream(uint32_t stream_id)
	{
		std::shared_lock<std::shared_mutex> lock(_stream_map_mutex);
		auto it = _streams.find(stream_id);
		if (it == _streams.end())
		{
			return nullptr;
		}

		return it->second;
	}

	std::shared_ptr<Stream> Application::GetStream(ov::String stream_name)
	{
		std::shared_lock<std::shared_mutex> lock(_stream_map_mutex);
		for (auto const &x : _streams)
		{
			auto stream = x.second;
			if (stream->GetName() == stream_name)
			{
				return stream;
			}
		}

		return nullptr;
	}
}  // namespace pub
