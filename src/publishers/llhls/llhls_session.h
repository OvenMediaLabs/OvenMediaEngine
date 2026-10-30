//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/publisher/session.h>
#include <chrono>
#include <list>
#include <optional>

#include <modules/access_control/access_controller.h>

#define MAX_PENDING_REQUESTS 10

class LLHlsSession : public pub::Session
{
public:
	static std::shared_ptr<LLHlsSession> Create(session_id_t session_id, 
												const bool &origin_mode,
												const ov::String &session_value, 
												const std::shared_ptr<pub::Application> &application,
												const std::shared_ptr<pub::Stream> &stream,
												uint64_t session_life_time);

	static std::shared_ptr<LLHlsSession> Create(session_id_t session_id, 
												const bool &origin_mode,
												const ov::String &session_value, 
												const std::shared_ptr<pub::Application> &application,
												const std::shared_ptr<pub::Stream> &stream,
												const ov::String &user_agent,
												uint64_t session_life_time);

	LLHlsSession(const info::Session &session_info, 
				const bool &origin_mode,
				const ov::String &session_value,
				const std::shared_ptr<pub::Application> &application, 
				const std::shared_ptr<pub::Stream> &stream,
				const ov::String &user_agent,
				uint64_t session_life_time);
	
	~LLHlsSession() override;

	bool Start() override;
	bool Stop() override;

	// pub::Session Interface
	void SendOutgoingData(const std::any &packet) override;
	void OnMessageReceived(const std::any &message) override;

	// Registers the connection, false once the session is closed
	bool UpdateLastRequest(uint32_t connection_id);
	// Whether the connection carried a request of this session within the last msec
	bool HasUsedConnectionWithin(uint32_t connection_id, int64_t msec) const;
	// Removes the connection. When it was the last one, returns the time the session became idle
	std::optional<uint64_t> OnConnectionDisconnected(uint32_t connection_id);
	// Closes the session if it has had no connection since idle_since, false when one came back meanwhile
	bool CloseIfIdleSince(uint64_t idle_since);

	// The session value its players carry in their URLs
	const ov::String &GetSessionValue() const;
	// Whether the value has the form OME issues, so it is safe to write back into a playlist
	static bool IsSessionValue(const ov::String &value);

	const ov::String &GetUserAgent() const;

private:

	enum class RequestType : uint8_t
	{
		Playlist,
		Chunklist,
		InitializationSegment,
		Segment,
		PartialSegment,
	};

	bool ParseFileName(const ov::String &file_name, RequestType &type, int32_t &track_id, int64_t &segment_number, int64_t &partial_number, int64_t &init_version, ov::String &stream_key) const;

	void ResponsePlaylist(const std::shared_ptr<http::svr::HttpExchange> &exchange, const ov::String &file_name, bool legacy, bool rewind, bool holdIfAccepted = true);
	void ResponseChunklist(const std::shared_ptr<http::svr::HttpExchange> &exchange, const ov::String &file_name, const int32_t &track_id, int64_t msn, int64_t part, bool skip, bool legacy, bool rewind, bool holdIfAccepted = true);
	void ResponseInitializationSegment(const std::shared_ptr<http::svr::HttpExchange> &exchange, const ov::String &file_name, const int32_t &track_id, const int64_t &init_version);
	void ResponseSegment(const std::shared_ptr<http::svr::HttpExchange> &exchange, const ov::String &file_name, const int32_t &track_id, const int64_t &segment_number);
	void ResponsePartialSegment(const std::shared_ptr<http::svr::HttpExchange> &exchange, const ov::String &file_name, const int32_t &track_id, const int64_t &segment_number, const int64_t &partial_number, bool holdIfAccepted = true);

	void ResponseData(const std::shared_ptr<http::svr::HttpExchange> &exchange);

	void OnPlaylistUpdated(const int32_t &track_id, const int64_t &msn, const int64_t &part);

	ov::String MakeQueryStringToPropagate(const std::shared_ptr<ov::Url> &request_uri);

	// Pending requests
	struct PendingRequest
	{
		RequestType type;
		ov::String file_name;
		int32_t track_id;
		int64_t segment_number = -1;
		int64_t partial_number = -1;
		bool skip = false;
		bool legacy = false;
		bool rewind = false;

		std::shared_ptr<http::svr::HttpExchange> exchange;
	};

	bool AddPendingRequest(const std::shared_ptr<http::svr::HttpExchange> &exchange, const RequestType &type, const ov::String &file_name, const int32_t &track_id, const int64_t &segment_number, const int64_t &partial_number, const bool &skip, const bool &legacy, const bool &rewind);

	std::list<PendingRequest> _pending_requests;

	// ID list of connections requesting this session
	// Connection ID : last request time
	std::map<uint32_t, std::chrono::steady_clock::time_point> _last_request_time;
	// When the last connection left; a closed session takes no connection again
	uint64_t _idle_since = 0;
	bool _closed = false;
	mutable std::shared_mutex _last_request_time_guard;
	
	// session life time
	uint64_t _session_life_time = 0;
	// Only an origin mode session counts itself as a viewer; the stream counts the others
	uint32_t _number_of_players = 0;

	// <session id>_<key> unless the session was started by a value from a URL
	ov::String _session_value;

	int _master_playlist_max_age = 0;
	int _chunklist_max_age = 0;
	int _chunklist_with_directives_max_age = 60;
	int _segment_max_age = -1;
	int _partial_segment_max_age = -1;

	bool _origin_mode = false;

	ov::String _user_agent;

	// default querystring value
	bool _hls_legacy = false;
	bool _hls_rewind = false;
};