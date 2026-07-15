#pragma once

#include <utility>
#include <shared_mutex>
#include "base/common_types.h"
#include "base/info/stream.h"
#include "base/info/session.h"
#include "base/info/push.h"
#include "base/mediarouter/mediarouter_application_observer.h"
#include "base/ovlibrary/semaphore.h"
#include "base/ovlibrary/string.h"
#include "config/config.h"
#include "stream.h"
#include "modules/managed_queue/managed_queue.h"

#define MIN_APPLICATION_WORKER_COUNT		1
#define MAX_APPLICATION_WORKER_COUNT		72

namespace pub
{
	enum ApplicationState
	{
		Idle,
		Started,
		Stopped,
		Error
	};

	class Publisher;

	// Distribute Stream to applicationWorker.
	class ApplicationWorker
	{
	public:
		ApplicationWorker(uint32_t worker_id, ov::String vhost_app_name, ov::String worker_name);
		bool Start();
		bool Stop();
		bool PushMediaPacket(const std::shared_ptr<Stream> &stream, const std::shared_ptr<MediaPacket> &media_packet);

		uint32_t GetWorkerId() const;
		void OnStreamCreated(const std::shared_ptr<info::Stream> &info);
		void OnStreamDeleted(const std::shared_ptr<info::Stream> &info);
		uint32_t GetStreamCount() const;

	private:
		void WorkerThread();

		uint32_t	_worker_id = 0;
		ov::String  _vhost_app_name;
		ov::String	_worker_name;

		class StreamData
		{
		public:
			StreamData(const std::shared_ptr<Stream> &stream,
							const std::shared_ptr<MediaPacket> &media_packet)
			{
				_stream = stream;
				_media_packet = media_packet;
			}

			std::shared_ptr<Stream> _stream;
			std::shared_ptr<MediaPacket> _media_packet;
		};
		std::shared_ptr<ApplicationWorker::StreamData> PopStreamData();

		std::atomic<bool> _stop_thread_flag;
		std::thread _worker_thread;
		ov::Semaphore _queue_event;

		ov::ManagedQueue<std::shared_ptr<StreamData>> _stream_data_queue;

		[[maybe_unused]] int64_t _last_video_ts_ms = 0;
		[[maybe_unused]] int64_t _last_audio_ts_ms = 0;

		std::atomic<uint32_t> _stream_count = 0;
	};

	class Application : public info::Application, public MediaRouterApplicationObserver
	{
	public:
		const char* GetApplicationTypeName() final;
		const char* GetPublisherTypeName() final;
		
		// MediaRouterApplicationObserver Implementation
		bool OnStreamCreated(const std::shared_ptr<info::Stream> &info) override;
		bool OnStreamDeleted(const std::shared_ptr<info::Stream> &info) override;
		bool OnStreamPrepared(const std::shared_ptr<info::Stream> &info) override;
		bool OnStreamUpdated(const std::shared_ptr<info::Stream> &info) override;

		// Put data in ApplicationWorker's queue.
		bool OnSendFrame(const std::shared_ptr<info::Stream> &stream,
							  const std::shared_ptr<MediaPacket> &media_packet) override;

		uint32_t GetStreamCount();
		std::shared_ptr<Stream> GetStream(uint32_t stream_id);
		std::shared_ptr<Stream> GetStream(ov::String stream_name);
		template <typename T>
		std::enable_if_t<std::is_base_of<Stream, T>::value, std::shared_ptr<T>>
		GetStreamAs(uint32_t stream_id)
		{
			return std::dynamic_pointer_cast<T>(GetStream(stream_id));
		}

		template <typename T>
		std::enable_if_t<std::is_base_of<Stream, T>::value, std::shared_ptr<T>>
		GetStreamAs(ov::String stream_name)
		{
			return std::dynamic_pointer_cast<T>(GetStream(stream_name));
		}

		virtual bool Start();
		virtual bool Stop();

	protected:
		explicit Application(const std::shared_ptr<Publisher> &publisher, const info::Application &application_info);
		virtual ~Application();

		std::shared_mutex 		_stream_map_mutex;
		std::map<uint32_t, std::shared_ptr<Stream>> _streams;

	private:
		bool DeleteAllStreams();
		virtual std::shared_ptr<Stream> CreateStream(const std::shared_ptr<info::Stream> &info, uint32_t thread_count) = 0;
		virtual bool DeleteStream(const std::shared_ptr<info::Stream> &info) = 0;
		
		std::shared_ptr<ApplicationWorker> GetWorkerByStreamID(info::stream_id_t stream_id);
		std::shared_ptr<ApplicationWorker> GetLowestLoadWorker();

		void MapStreamToWorker(const std::shared_ptr<info::Stream> &info);
		void UnmapStreamToWorker(const std::shared_ptr<info::Stream> &info);

		uint32_t		_application_worker_count;
		std::shared_mutex _application_worker_lock;
		std::vector<std::shared_ptr<ApplicationWorker>>	_application_workers;
		// stream_id : worker_id
		std::map<info::stream_id_t, uint32_t> _stream_app_worker_map;
		std::shared_mutex _stream_app_worker_map_lock;

		std::shared_ptr<Publisher>		_publisher;
	};

}  // namespace pub
