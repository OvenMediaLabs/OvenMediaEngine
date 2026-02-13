#pragma once

#include <shared_mutex>
#include "base/common_types.h"
#include "base/info/stream.h"
#include "base/info/push.h"
#include "base/mediarouter/media_buffer.h"
#include "base/event/media_event.h"
#include "modules/managed_queue/managed_queue.h"
#include "session.h"

#define MAX_STREAM_WORKER_THREAD_COUNT 72

namespace pub
{
	class StreamWorker
	{
	public:
		StreamWorker(const std::shared_ptr<Stream> &parent_stream);
		~StreamWorker();

		bool Start();
		bool Stop();

		bool AddSession(const std::shared_ptr<Session> &session);
		bool RemoveSession(session_id_t id);
		std::shared_ptr<Session> GetSession(session_id_t id);

		// Send to a specific session
		void SendMessage(const std::shared_ptr<Session> &session, const std::any &message);

		// Send to all sessions
		void SendPacket(const std::any &packet);

	private:
		void WorkerThread();

		std::map<session_id_t, std::shared_ptr<Session>> _sessions;
		std::shared_mutex _session_map_mutex;
		
		ov::Semaphore _queue_event;

		std::optional<std::any> PopStreamPacket();
		ov::ManagedQueue<std::any> _packet_queue;

		struct SessionMessage
		{
			SessionMessage(const std::shared_ptr<Session> &session, const std::any &message)
			{
				_session = session;
				_message = message;
			}
			
			std::shared_ptr<Session> _session;
			std::any _message;
		};

		std::shared_ptr<SessionMessage> PopSessionMessage();
		ov::Queue<std::shared_ptr<SessionMessage>> _session_message_queue;

		std::atomic<bool> _stop_thread_flag;
		std::thread _worker_thread;

		std::shared_ptr<Stream> _parent;
	};

	class Application;
	class Stream : public info::Stream, public ov::EnableSharedFromThis<Stream>
	{
	public:
		// Create stream --> Start stream --> Stop stream --> Delete stream
		enum class State : uint8_t
		{
			CREATED = 0,
			STARTED,
			STOPPED,
			ERROR,
		};

		struct DefaultPlaylistInfo
		{
			// Playlist name
			//
			// For example, in LL-HLS, the value is "llhls_default"
			ov::String name;

			// Playlist file name, used to retrieve a playlist from the Stream, such as with GetPlaylist()
			//
			// For example, in LL-HLS, the value is "llhls"
			ov::String file_name;

			// Used internally by the stream of publisher to distinguish playlists based on file names
			// (e.g., when caching the master playlist in LLHlsStream)
			//
			// For example, in LL-HLS, the value is "llhls.m3u8"
			ov::String internal_file_name;

			DefaultPlaylistInfo(
				const ov::String &name,
				const ov::String &file_name,
				const ov::String &internal_file_name)
				: name(name),
				  file_name(file_name),
				  internal_file_name(internal_file_name)
			{
			}
		};

	public:
		virtual std::shared_ptr<const DefaultPlaylistInfo> GetDefaultPlaylistInfo() const
		{
			return nullptr;
		}

		std::shared_ptr<const info::Playlist> GetDefaultPlaylist() const;

		// Session을 추가한다.
		bool AddSession(std::shared_ptr<Session> session);
		bool RemoveSession(session_id_t id);
		std::shared_ptr<Session> GetSession(session_id_t id);
		const std::map<session_id_t, std::shared_ptr<Session>> GetAllSessions();
		uint32_t GetSessionCount();

		// This function is only called by Push Publisher
		virtual	std::shared_ptr<pub::Session> CreatePushSession(std::shared_ptr<info::Push> &push);

		// A child call this function to delivery packet to all sessions
		bool BroadcastPacket(const std::any &packet);

		bool SendMessage(const std::shared_ptr<Session> &session, const std::any &message);

		bool ProcessEvent(const std::shared_ptr<MediaEvent> &event);

		// Child must implement this function for packetizing and call BroadcastPacket to delivery to all sessions.
		virtual void SendVideoFrame(const std::shared_ptr<MediaPacket> &media_packet) = 0;
		virtual void SendAudioFrame(const std::shared_ptr<MediaPacket> &media_packet) = 0;
		virtual void SendDataFrame(const std::shared_ptr<MediaPacket> &media_packet) = 0;
		virtual void OnEvent(const std::shared_ptr<MediaEvent> &event) {}

		bool EnterStart();
		bool EnterStop();
		bool EnterUpdate(const std::shared_ptr<info::Stream> &info);
		

		bool WaitUntilStart(uint32_t timeout_ms);

		bool CreateStreamWorker(uint32_t worker_count);

		uint32_t IssueUniqueSessionId();

		std::shared_ptr<Application> GetApplication() const;
		const char * GetApplicationTypeName() const;

		// Set the stream state
		void SetState(State state)
		{
			_state = state;
		}

		State GetState() const
		{
			return _state;
		}

		const std::chrono::system_clock::time_point &GetStartedTime() const;

	protected:
		Stream(const std::shared_ptr<Application> application, const info::Stream &info);
		virtual ~Stream();

		virtual bool Start();
		virtual bool Update(const std::shared_ptr<info::Stream> &info);
		virtual bool Stop();

	private:
		std::shared_ptr<StreamWorker> GetWorkerBySessionID(session_id_t session_id);
		std::map<session_id_t, std::shared_ptr<Session>> _sessions;
		std::shared_mutex _session_map_mutex;

		uint32_t _worker_count;
		
		std::shared_mutex _stream_worker_lock;
		std::vector<std::shared_ptr<StreamWorker>>	_stream_workers;
		std::shared_ptr<Application> _application;

		session_id_t _last_issued_session_id;

		std::chrono::system_clock::time_point _started_time;

		State _state = State::CREATED;

		bool LockIfIdle()
		{
			std::lock_guard<std::mutex> lock(_busy_lock);
			if (_busy)
			{
				return false;
			}
			_busy = true;
			return true;
		}

		void WaitUntilIdleAndLock()
		{
			std::unique_lock<std::mutex> lock(_busy_lock);
			_busy_condition.wait(lock, [this]() { return !_busy; });
			_busy = true;
		}

		void Unlock()
		{
			{
				std::lock_guard<std::mutex> lock(_busy_lock);
				_busy = false;
			}
			_busy_condition.notify_all();
		}

		mutable std::mutex _busy_lock;
		std::condition_variable _busy_condition;
		bool _busy = false;
	};
}  // namespace pub