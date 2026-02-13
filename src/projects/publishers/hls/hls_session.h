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
#include <list>

#include <modules/access_control/access_controller.h>

#define MAX_PENDING_REQUESTS 10

class HlsSession : public pub::Session
{
public:
	static std::shared_ptr<HlsSession> Create(session_id_t session_id, 
												const bool &origin_mode,
												const ov::String &session_key, 
												const std::shared_ptr<pub::Application> &application,
												const std::shared_ptr<pub::Stream> &stream,
												uint64_t session_life_time);

	static std::shared_ptr<HlsSession> Create(session_id_t session_id, 
												const bool &origin_mode,
												const ov::String &session_key, 
												const std::shared_ptr<pub::Application> &application,
												const std::shared_ptr<pub::Stream> &stream,
												const ov::String &user_agent,
												uint64_t session_life_time);

	HlsSession(const info::Session &session_info, 
				const bool &origin_mode,
				const ov::String &session_key,
				const std::shared_ptr<pub::Application> &application, 
				const std::shared_ptr<pub::Stream> &stream,
				const ov::String &user_agent,
				uint64_t session_life_time);
	
	~HlsSession() override;

	bool Start() override;
	bool Stop() override;

	// pub::Session Interface
	void SendOutgoingData(const std::any &packet) override;
	void OnMessageReceived(const std::any &message) override;

	void UpdateLastRequest(uint32_t connection_id);
	uint64_t GetLastRequestTime(uint32_t connection_id) const;
	void OnConnectionDisconnected(uint32_t connection_id);
	bool IsNoConnection() const;

	// Get session key
	const ov::String &GetSessionKey() const;
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

	bool ParseFileName(const ov::String &file_name, RequestType &type, ov::String &playlist, ov::String &variant_name, uint32_t &number) const;

	void ResponseMasterPlaylist(const std::shared_ptr<http::svr::HttpExchange> &exchange, const ov::String &playlist);
	void ResponseMediaPlaylist(const std::shared_ptr<http::svr::HttpExchange> &exchange, const ov::String &variant_name);
	void ResponseSegment(const std::shared_ptr<http::svr::HttpExchange> &exchange, const ov::String &variant_name, uint32_t number);

	void ResponseData(const std::shared_ptr<http::svr::HttpExchange> &exchange);

	bool GetRewindOptionValue(const std::shared_ptr<ov::Url> &url) const;

	// ID list of connections requesting this session
	// Connection ID : last request time
	std::map<uint32_t, uint64_t> _last_request_time;
	mutable std::shared_mutex _last_request_time_guard;
	
	// session life time
	uint64_t _session_life_time = 0;
	uint32_t _number_of_players = 0;

	ov::String _session_key;

	int _master_playlist_max_age = 0;
	int _chunklist_max_age = 0;
	int _chunklist_with_directives_max_age = 60;
	int _segment_max_age = -1;
	int _partial_segment_max_age = -1;

	bool _origin_mode = false;

	ov::String _user_agent;

	// default querystring value
	bool _default_option_rewind = true;
};