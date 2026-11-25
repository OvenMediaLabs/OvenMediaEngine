#include "stream.h"
#include "application.h"
#include "publisher_private.h"
#include <base/event/command/commands.h>

namespace pub
{
	StreamWorker::StreamWorker(const std::shared_ptr<Stream> &parent_stream)
		: _packet_queue(nullptr, 500)
	{
		_stop_thread_flag = true;
		_parent = parent_stream;
	}

	StreamWorker::~StreamWorker()
	{
	}

	bool StreamWorker::Start()
	{
		if (!_stop_thread_flag)
		{
			return true;
		}

		auto urn = std::make_shared<info::ManagedQueue::URN>(
			_parent->GetApplicationName(), 
			_parent->GetName(), 
			"pub", 
			ov::String::FormatString("streamworker_%s", _parent->GetApplication()->GetPublisherTypeName()).LowerCaseString());
		_packet_queue.SetUrn(urn);
		
		_stop_thread_flag = false;

		try
		{
			_worker_thread = std::thread(&StreamWorker::WorkerThread, this);
		}
		catch (const std::system_error &e)
		{
			_stop_thread_flag = true;

			logte("Cannot create StreamWorker thread of %s/%s/%s: %s",
				  _parent->GetApplicationTypeName(), _parent->GetApplicationName(), _parent->GetName().CStr(),
				  e.what());

			return false;
		}

		ov::String thread_name = ov::String::FormatString("SW-%s", _parent->GetApplication()->GetPublisherTypeName());
		pthread_setname_np(_worker_thread.native_handle(), thread_name.CStr());

		return true;
	}

	bool StreamWorker::Stop()
	{
		if (_stop_thread_flag)
		{
			return true;
		}

		ov::String worker_name = ov::String::FormatString("%s/%s/%s", _parent->GetApplicationTypeName(), _parent->GetApplicationName(), _parent->GetName().CStr());
		logtt("Try to stop StreamWorker thread of %s", worker_name.CStr());

		_stop_thread_flag = true;
		// Generate Event
		_packet_queue.Stop();
		_session_message_queue.Stop();
		_queue_event.Stop();
		
		if(_worker_thread.joinable())
		{
			_worker_thread.join();
		}

		logtt("StreamWorker thread of %s has been stopped successfully", worker_name.CStr());

		std::lock_guard<std::shared_mutex> lock(_session_map_mutex);

		logtt("Try to stop all sessions of %s", worker_name.CStr());
		for (auto const &x : _sessions)
		{
			auto session = std::static_pointer_cast<Session>(x.second);
			session->Stop();
		}
		_sessions.clear();
		_session_count.store(0);
		logtt("All sessions(%zu) of %s has been stopped successfully", _sessions.size(), worker_name.CStr());

		return true;
	}

	bool StreamWorker::AddSession(const std::shared_ptr<Session> &session)
	{
		// Cannot add session after StreamWorker is stopped
		if (_stop_thread_flag)
		{
			return true;
		}

		std::lock_guard<std::shared_mutex> lock(_session_map_mutex);
		_sessions[session->GetId()] = session;
		_session_count.store(_sessions.size());

		return true;
	}

	bool StreamWorker::RemoveSession(session_id_t id)
	{
		// Cannot remove session after StreamWorker is stopped
		if (_stop_thread_flag)
		{
			return true;
		}

		std::unique_lock<std::shared_mutex> lock(_session_map_mutex);
		if (_sessions.count(id) <= 0)
		{
			// `Stop()` sets the flag before it stops and clears every session under this lock
			if (_stop_thread_flag)
			{
				return true;
			}

			logte("Cannot find session : %u", id);
			return false;
		}

		auto session = _sessions[id];
		_sessions.erase(id);
		_session_count.store(_sessions.size());
		lock.unlock();

		session->Stop();

		return true;
	}

	std::shared_ptr<Session> StreamWorker::GetSession(session_id_t id)
	{
		std::shared_lock<std::shared_mutex> lock(_session_map_mutex);
		auto it = _sessions.find(id);
		if (it == _sessions.end())
		{
			// logte("Cannot find session : %u", id);
			return nullptr;
		}

		return it->second;
	}

	void StreamWorker::SendPacket(const std::any &packet)
	{
		// No session to deliver to, so the thread is not woken
		if (_session_count.load() == 0)
		{
			return;
		}

		_packet_queue.Enqueue(packet);
		_queue_event.Notify();
	}

	// Send to a specific session
	void StreamWorker::SendMessage(const std::shared_ptr<Session> &session, const std::any &message)
	{
		_session_message_queue.Enqueue(std::make_shared<SessionMessage>(session, message));
		_queue_event.Notify();
	}

	std::optional<std::any> StreamWorker::PopStreamPacket()
	{
		if (_packet_queue.IsEmpty())
		{
			return std::nullopt;
		}

		return _packet_queue.Dequeue();
	}

	std::shared_ptr<StreamWorker::SessionMessage> StreamWorker::PopSessionMessage()
	{
		if (_session_message_queue.IsEmpty())
		{
			return nullptr;
		}

		auto data = _session_message_queue.Dequeue();
		if(data.has_value())
		{
			return data.value();
		}

		return nullptr;
	}

	void StreamWorker::WorkerThread()
	{
		ov::logger::ThreadHelper thread_helper;

		std::shared_lock<std::shared_mutex> session_lock(_session_map_mutex, std::defer_lock);

		while (!_stop_thread_flag)
		{
			_queue_event.Wait();

			auto session_message = PopSessionMessage();
			if (session_message != nullptr && session_message->_session != nullptr && session_message->_message.has_value())
			{
				session_message->_session->OnMessageReceived(session_message->_message);
			}

			auto packet = PopStreamPacket();
			if (packet.has_value())
			{		
				session_lock.lock();
				for (auto const &x : _sessions)
				{
					auto session = x.second;
					session->SendOutgoingData(packet.value());
				}
				session_lock.unlock();
			}
		}
	}

	Stream::Stream(const std::shared_ptr<Application> application, const info::Stream &info)
		: info::Stream(info)
	{
		_application = application;
		_state = State::CREATED;
	}

	Stream::~Stream()
	{
	}

	void Stream::UpdateTrackFromPacket(const std::shared_ptr<MediaPacket> &media_packet)
	{
		auto new_track = media_packet->GetTrack();
		if (new_track == nullptr)
		{
			return;
		}

		auto track_id = static_cast<int32_t>(media_packet->GetTrackId());
		auto old_track = GetTrack(track_id);
		if (old_track == new_track)
		{
			return;
		}

		UpdateTrack(new_track);

		// The first published version replacing the setup skeleton is the
		// initial handover, not a configuration change
		if (old_track == nullptr || old_track->IsPublished() == false ||
			old_track->GetVersion() == new_track->GetVersion())
		{
			return;
		}

		OnTrackChanged(track_id, old_track, new_track);
	}

	bool Stream::IsStalePacket(const std::shared_ptr<MediaPacket> &media_packet) const
	{
		auto packet_track = media_packet->GetTrack();
		if (packet_track == nullptr)
		{
			return false;
		}

		auto current_track = GetTrack(media_packet->GetTrackId());
		if (current_track == nullptr)
		{
			return false;
		}

		if (packet_track->GetVersion() >= current_track->GetVersion())
		{
			return false;
		}

		// An older version with identical content (e.g. a label-only change)
		// is still valid under the current configuration
		return packet_track->HasSameContent(*current_track) == false;
	}

	void Stream::OnTrackChanged(int32_t track_id, const std::shared_ptr<const MediaTrack> &old_track, const std::shared_ptr<const MediaTrack> &new_track)
	{
		// A label-only change (e.g. a detected subtitle language) does not affect
		// the media itself, so the output stays correct without publisher support
		if (old_track->HasSameContent(*new_track))
		{
			logti("%s/%s(%u) Track(%d) metadata has been updated (version %u -> %u)",
				  GetApplicationName(), GetName().CStr(), GetId(),
				  track_id, old_track->GetVersion(), new_track->GetVersion());
			return;
		}

		// A publisher that does not override this cannot switch its output to the
		// new configuration, so the output may be broken from this point.
		logtw("%s/%s(%u) Track(%d) configuration has changed (version %u -> %u), but the %s publisher does not support runtime configuration changes. The output of this track may be broken",
			  GetApplicationName(), GetName().CStr(), GetId(),
			  track_id, old_track->GetVersion(), new_track->GetVersion(),
			  GetApplication()->GetPublisherTypeName());
	}

	void Stream::LogInbandRecoverableTrackChange(int32_t track_id, const std::shared_ptr<const MediaTrack> &old_track, const std::shared_ptr<const MediaTrack> &new_track)
	{
		// A label-only change (e.g. a detected subtitle language) does not affect the
		// media, so report it as a metadata update like the base does, not a config change.
		if (old_track->HasSameContent(*new_track))
		{
			logti("%s/%s(%u) Track(%d) metadata has been updated (version %u -> %u)",
				  GetApplicationName(), GetName().CStr(), GetId(),
				  track_id, old_track->GetVersion(), new_track->GetVersion());
			return;
		}

		// A codec change requires renegotiating the running session, which these
		// publishers cannot do, so the track's output stops working from here.
		if (old_track->GetCodecId() != new_track->GetCodecId())
		{
			logtw("%s/%s(%u) Track(%d) codec has changed (%s -> %s), which the %s publisher cannot apply to a running session. The output of this track stops working from here",
				  GetApplicationName(), GetName().CStr(), GetId(), track_id,
				  cmn::GetCodecIdString(old_track->GetCodecId()), cmn::GetCodecIdString(new_track->GetCodecId()),
				  GetApplication()->GetPublisherTypeName());
			return;
		}

		// The codec is unchanged, so the new parameter sets are delivered in-band
		// and playback continues without renegotiation.
		logti("%s/%s(%u) Track(%d) configuration has changed (version %u -> %u); the %s publisher applies it in-band",
			  GetApplicationName(), GetName().CStr(), GetId(), track_id,
			  old_track->GetVersion(), new_track->GetVersion(),
			  GetApplication()->GetPublisherTypeName());
	}

	std::shared_ptr<const info::Playlist> Stream::GetDefaultPlaylist() const
	{
		auto info = GetDefaultPlaylistInfo();

		if (info != nullptr)
		{
			return GetPlaylist(info->file_name);
		}

		return nullptr;
	}

	bool Stream::EnterStart()
	{
		// If it is not in the Idle state before Start, it is an abnormal situation, so Start fails
		if (!LockIfIdle())
		{
			logte("Cannot start stream [%s(%u)] because it is not in the Idle state", GetName().CStr(), GetId());
			return false;
		}

		bool was_created = (_state == State::CREATED);
		bool ok			 = Start();

		// `Stop()` skips a stream that never started,
		// so the workers and the sessions of a failed start are stopped here
		if ((ok == false) && was_created)
		{
			std::vector<std::shared_ptr<StreamWorker>> workers;
			std::map<session_id_t, std::shared_ptr<Session>> sessions;

			{
				std::unique_lock<std::shared_mutex> worker_lock(_stream_worker_lock);

				_state = State::ERROR;

				std::lock_guard<std::shared_mutex> session_lock(_session_map_mutex);
				workers.swap(_stream_workers);
				sessions.swap(_sessions);
				_session_count.store(0);
			}

			StopWorkersAndSessions(workers, sessions);
		}

		Unlock();
		
		return ok;
	}

	bool Stream::EnterStop(StopCause cause)
	{
		WaitUntilIdleAndLock();

		// Set after the lock: two callers stopping the same stream would otherwise overwrite
		// each other's cause before the first `Stop()` reads it.
		_stop_cause = cause;

		bool ok		= Stop();

		// `Stop()` skips a stream that has not started, so it is marked stopped here and a later `Start()` refuses it
		{
			std::lock_guard<std::shared_mutex> lock(_stream_worker_lock);

			if (_state == State::CREATED)
			{
				_state = State::STOPPED;
			}
		}

		Unlock();

		return ok;
	}

	bool Stream::Start()
	{
		if (_state != State::CREATED)
		{
			return false;
		}

		logti("%s has started [%s(%u)] stream", GetApplicationTypeName(), GetName().CStr(), GetId());

		_started_time = std::chrono::system_clock::now();
		_state = State::STARTED;
		return true;
	}

	bool Stream::Stop()
	{
		logti("Try to stop %s stream [%s(%u)]", GetApplicationTypeName(), GetName().CStr(), GetId());

		// The workers and the sessions are taken out together,
		// so `RemoveSession()` finds a session with its worker or not at all
		std::vector<std::shared_ptr<StreamWorker>> workers;
		std::map<session_id_t, std::shared_ptr<Session>> sessions;

		{
			std::unique_lock<std::shared_mutex> worker_lock(_stream_worker_lock);

			if (_state != State::STARTED)
			{
				return false;
			}

			_state = State::STOPPED;

			std::lock_guard<std::shared_mutex> session_lock(_session_map_mutex);
			workers.swap(_stream_workers);
			sessions.swap(_sessions);
			_session_count.store(0);
		}

		OnStopping(sessions);

		StopWorkersAndSessions(workers, sessions);

		logti("[%s(%u)] %s stream has been stopped", GetName().CStr(), GetId(), GetApplicationTypeName());

		return true;
	}

	bool Stream::WaitUntilStart(uint32_t timeout_ms)
	{
		ov::StopWatch	watch;
		
		watch.Start();

		while(_state != State::STARTED && watch.Elapsed() < timeout_ms)
		{
			usleep(100 * 1000); // 100ms
		}

		return _state == State::STARTED;
	}

	bool Stream::CreateStreamWorker(uint32_t worker_count)
	{
		if (worker_count > MAX_STREAM_WORKER_THREAD_COUNT)
		{
			worker_count = MAX_STREAM_WORKER_THREAD_COUNT;
		}

		// Held while the workers start, so a session added meanwhile waits for them instead of finding none
		std::lock_guard<std::shared_mutex> lock(_stream_worker_lock);

		std::vector<std::shared_ptr<StreamWorker>> workers;
		for (uint32_t i = 0; i < worker_count; i++)
		{
			auto stream_worker = std::make_shared<StreamWorker>(GetSharedPtr());
						
			if (stream_worker->Start() == false)
			{
				for (const auto &worker : workers)
				{
					worker->Stop();
				}

				return false;
			}

			workers.push_back(stream_worker);
		}

		_worker_count = worker_count;
		_stream_workers.insert(_stream_workers.end(), workers.begin(), workers.end());

		return true;
	}

	std::shared_ptr<pub::Session> Stream::CreatePushSession(std::shared_ptr<info::Push> &push)
	{
		logtw("The function was not implemented in the child class");
		return nullptr;
	}

	const std::chrono::system_clock::time_point &Stream::GetStartedTime() const
	{
		return _started_time;
	}

	std::shared_ptr<Application> Stream::GetApplication() const
	{
		return _application;
	}

	const char * Stream::GetApplicationTypeName() const
	{
		if(GetApplication() == nullptr)
		{
			return "Unknown";
		}

		return GetApplication()->GetApplicationTypeName();
	}

	void Stream::StopWorkersAndSessions(const std::vector<std::shared_ptr<StreamWorker>> &workers, const std::map<session_id_t, std::shared_ptr<Session>> &sessions)
	{
		if (workers.empty() && sessions.empty())
		{
			return;
		}

		for (const auto &worker : workers)
		{
			worker->Stop();
		}

		logti("[%s(%u)] %s - All StreamWorker has been stopped", GetName().CStr(), GetId(), GetApplicationTypeName());

		logti("[%s(%u)] %s - Try to stop all sessions (%zu)", GetName().CStr(), GetId(), GetApplicationTypeName(), sessions.size());

		for (const auto &x : sessions)
		{
			x.second->Stop();
		}
	}

	std::shared_ptr<StreamWorker> Stream::GetWorkerBySessionID(session_id_t session_id)
	{
		if(_worker_count == 0)
		{
			return nullptr;
		}

		size_t worker_id = session_id % _worker_count;
		if(worker_id >= _stream_workers.size())
		{
			logtw("Invalid worker id : %zu", worker_id);
			return nullptr;
		}

		return _stream_workers[worker_id];
	}

	bool Stream::AddSession(std::shared_ptr<Session> session)
	{
		std::shared_lock<std::shared_mutex> worker_lock(_stream_worker_lock);

		// `Stop()` and a failed `EnterStart()` change the state under this lock before they take the sessions out,
		// so a session that arrives later is refused here and its caller disposes of it
		if ((_state == State::STOPPED) || (_state == State::ERROR))
		{
			return false;
		}

		std::lock_guard<std::shared_mutex> session_lock(_session_map_mutex);

		std::shared_ptr<StreamWorker> worker;

		// The worker is found first, so a session that is refused is never left in `_sessions`
		if (_worker_count > 0)
		{
			worker = GetWorkerBySessionID(session->GetId());
			if (worker == nullptr)
			{
				logte("Cannot find worker for session : %u", session->GetId());
				return false;
			}
		}

		if (_sessions.count(session->GetId()) > 0)
		{
			logtw("Session ID (%u) already exists, existing session will be overwritten", session->GetId());
		}

		// For getting session, all sessions
		_sessions[session->GetId()] = session;

		bool result = (worker != nullptr) ? worker->AddSession(session) : true;

		// Counted only once its worker has it, so a sender that sees the count can deliver to it
		_session_count.store(_sessions.size());

		return result;
	}

	bool Stream::RemoveSession(session_id_t id)
	{
		std::shared_ptr<StreamWorker> worker;

		{
			std::shared_lock<std::shared_mutex> worker_lock(_stream_worker_lock);
			std::lock_guard<std::shared_mutex> session_lock(_session_map_mutex);

			auto session_iterator = _sessions.find(id);
			if (session_iterator == _sessions.end())
			{
				logtt("Cannot find session : %u", id);
				return false;
			}
			_sessions.erase(session_iterator);
			_session_count.store(_sessions.size());

			if (_worker_count == 0)
			{
				return true;
			}

			worker = GetWorkerBySessionID(id);
		}

		if (worker == nullptr)
		{
			logte("Cannot find worker for session : %u", id);
			return false;
		}

		return worker->RemoveSession(id);
	}

	std::shared_ptr<Session> Stream::GetSession(session_id_t id)
	{
		std::shared_lock<std::shared_mutex> session_lock(_session_map_mutex);
		if (_sessions.count(id) <= 0)
		{
			return nullptr;
		}

		return _sessions.at(id);
	}

	const std::map<session_id_t, std::shared_ptr<Session>> Stream::GetAllSessions()
	{
		std::shared_lock<std::shared_mutex> session_lock(_session_map_mutex);
		return _sessions;
	}

	uint32_t Stream::GetSessionCount()
	{
		return _session_count.load();
	}

	bool Stream::BroadcastPacket(const std::any &packet)
	{
		if (_session_count.load() == 0)
		{
			return true;
		}

		if(_worker_count > 0)
		{
			std::shared_lock<std::shared_mutex> worker_lock(_stream_worker_lock);
			for (uint32_t i = 0; i < _stream_workers.size(); i++)
			{
				_stream_workers[i]->SendPacket(packet);
			}
		}
		else
		{
			std::shared_lock<std::shared_mutex> session_lock(_session_map_mutex);
			for (auto const &x : _sessions)
			{
				auto session = std::static_pointer_cast<Session>(x.second);
				session->SendOutgoingData(packet);
			}
		}
	
		return true;
	}

	bool Stream::SendMessage(const std::shared_ptr<Session> &session, const std::any &message)
	{
		if(_worker_count > 0)
		{
			std::shared_ptr<StreamWorker> worker;

			{
				std::shared_lock<std::shared_mutex> lock(_stream_worker_lock);

				// `Stop()` and a failed `EnterStart()` take the workers out when they change the state,
				// so there is no worker to find
				if ((_state == State::STOPPED) || (_state == State::ERROR))
				{
					return false;
				}

				worker = GetWorkerBySessionID(session->GetId());
			}

			if(worker == nullptr)
			{
				logtw("Cannot find worker for session : %u", session->GetId());
				return false;
			}
			worker->SendMessage(session, message);
		}
		else
		{
			session->OnMessageReceived(message);
		}

		return true;
	}

	bool Stream::ProcessEvent(const std::shared_ptr<MediaEvent> &event)
	{
		if (event == nullptr)
		{
			return false;
		}

		switch (event->GetCommandType())
		{
			case EventCommand::Type::UpdateSubtitleLanguage:
			{
				auto command = event->GetCommand<EventCommandUpdateLanguage>();
				if (command == nullptr)
				{
					break;
				}

				auto track = GetTrackByLabel(command->GetTrackLabel());
				if (track == nullptr)
				{
					logtw("Cannot find subtitle track by label : %s - %s/%s(%u)", command->GetTrackLabel().CStr(), GetApplicationName(), GetName().CStr(), GetId());
					break;
				}

				if (track->GetLanguage() == command->GetLanguage())
				{
					break;
				}

				// The urgent event applies immediately with the version number the
				// router is about to publish, so the stamped packet that follows
				// converges silently without a second callback
				auto new_track = std::make_shared<MediaTrack>(*track);
				new_track->SetLanguage(command->GetLanguage());
				new_track->SetVersion(track->GetVersion() + 1);
				UpdateTrack(new_track);

				OnTrackChanged(track->GetId(), track, new_track);
				break;
			}

			default:
				// Do nothing
				break;
		}

		return true;
	}

	std::atomic<session_id_t> Stream::_next_session_id = 100;

	uint32_t Stream::IssueUniqueSessionId()
	{
		std::shared_lock<std::shared_mutex> lock(_session_map_mutex);

		// Once the counter wraps around, a session that has lived through it may still hold the next id
		auto session_id = _next_session_id++;
		while (_sessions.find(session_id) != _sessions.end())
		{
			session_id = _next_session_id++;
		}

		return session_id;
	}
}  // namespace pub