//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#include <modules/http/server/http_exchange.h>
#include "hls_session.h"
#include "hls_application.h"
#include "hls_stream.h"
#include "hls_private.h"

std::shared_ptr<HlsSession> HlsSession::Create(session_id_t session_id, 
												const bool &origin_mode,
												const ov::String &session_key,
												const std::shared_ptr<pub::Application> &application,
												const std::shared_ptr<pub::Stream> &stream,
												uint64_t session_life_time)
{
	return HlsSession::Create(session_id, origin_mode, session_key, application, stream, nullptr, session_life_time);
}

std::shared_ptr<HlsSession> HlsSession::Create(session_id_t session_id, 
												const bool &origin_mode,
												const ov::String &session_key,
												const std::shared_ptr<pub::Application> &application,
												const std::shared_ptr<pub::Stream> &stream,
												const ov::String &user_agent,
												uint64_t session_life_time)
{
	auto session_info = info::Session(*std::static_pointer_cast<info::Stream>(stream), session_id);
	auto session = std::make_shared<HlsSession>(session_info, origin_mode, session_key, application, stream, user_agent, session_life_time);

	if (session->Start() == false)
	{
		return nullptr;
	} 

	return session;
}

HlsSession::HlsSession(const info::Session &session_info, 
							const bool &origin_mode,
							const ov::String &session_key,
							const std::shared_ptr<pub::Application> &application, 
							const std::shared_ptr<pub::Stream> &stream,
							const ov::String &user_agent,
							uint64_t session_life_time)
	: pub::Session(session_info, application, stream), _user_agent(user_agent)
{
	_origin_mode = origin_mode;
	_session_life_time = session_life_time;
	if (session_key.IsEmpty())
	{
		_session_key = ov::Random::GenerateString(8);
	}

	logtt("TsSession::TsSession (%d)", session_info.GetId());
	MonitorInstance->OnSessionConnected(*stream, PublisherType::Hls);
	_number_of_players = 1;
}

HlsSession::~HlsSession()
{
	logtt("TsSession::~TsSession(%d)", GetId());
	MonitorInstance->OnSessionsDisconnected(*GetStream(), PublisherType::Hls, _number_of_players);
}

bool HlsSession::Start()
{
	auto ts_conf = GetApplication()->GetConfig().GetPublishers().GetHlsPublisher();
	
	_default_option_rewind = ts_conf.GetDefaultQueryString().GetBoolValue("_HLS_rewind", kDefaultHlsRewind);
	
	return Session::Start();
}

bool HlsSession::Stop()
{
	return Session::Stop();
}

const ov::String &HlsSession::GetSessionKey() const
{
	return _session_key;
}

const ov::String &HlsSession::GetUserAgent() const
{
	return _user_agent;
}

void HlsSession::UpdateLastRequest(uint32_t connection_id)
{
	std::lock_guard<std::shared_mutex> lock(_last_request_time_guard);
	_last_request_time[connection_id] = ov::Clock::NowMSec();

	logtt("TsSession(%u) : Request updated from %u : size(%zu)", GetId(), connection_id, _last_request_time.size());
}

uint64_t HlsSession::GetLastRequestTime(uint32_t connection_id) const
{
	std::shared_lock<std::shared_mutex> lock(_last_request_time_guard);
	auto itr = _last_request_time.find(connection_id);
	if (itr == _last_request_time.end())
	{
		return 0;
	}

	return itr->second;
}

void HlsSession::OnConnectionDisconnected(uint32_t connection_id)
{
	// lock
	std::lock_guard<std::shared_mutex> lock(_last_request_time_guard);
	_last_request_time.erase(connection_id);

	logtt("TsSession(%u) : Disconnected from %u : size(%d)", GetId(), connection_id, _last_request_time.size());
}

bool HlsSession::IsNoConnection() const
{
	std::shared_lock<std::shared_mutex> lock(_last_request_time_guard);
	return _last_request_time.empty();
}

// pub::Session Interface
void HlsSession::SendOutgoingData(const std::any &notification)
{
	// Not used
}

void HlsSession::OnMessageReceived(const std::any &message)
{
	std::shared_ptr<http::svr::HttpExchange> exchange = nullptr;
	try 
	{
        exchange = std::any_cast<std::shared_ptr<http::svr::HttpExchange>>(message);
		if(exchange == nullptr)
		{
			return;
		}
    }
    catch(const std::bad_any_cast& e) 
	{
        logtc("An incorrect type of packet was input from the stream.");
		return;
    }

	auto response = exchange->GetResponse();

	// Check expired time
	if(_session_life_time != 0 && _session_life_time < ov::Clock::NowMSec())
	{
		response->SetStatusCode(http::StatusCode::Unauthorized);
		ResponseData(exchange);
		return;
	}

	logtt("TsSession::OnMessageReceived(%u) - %s", GetId(), exchange->ToString().CStr());

	auto request = exchange->GetRequest();
	auto request_uri = request->GetParsedUri();

	if (request_uri == nullptr)
	{
		response->SetStatusCode(http::StatusCode::InternalServerError);
		ResponseData(exchange);
		return;
	}

	auto file = request_uri->File();
	if (file.IsEmpty())
	{
		return;
	}

	RequestType type;
	ov::String playlist;
	ov::String variant_name;
	uint32_t number = 0;

	if (ParseFileName(file, type, playlist, variant_name, number) == false)
	{
		response->SetStatusCode(http::StatusCode::BadRequest);
		ResponseData(exchange);
		return;
	}

	switch (type)
	{	
	case RequestType::Playlist:
	{
		// Response Playlist
		ResponseMasterPlaylist(exchange, playlist);
		break;
	}
	case RequestType::Chunklist:
	{
		// Response Chunklist
		ResponseMediaPlaylist(exchange, variant_name);
		break;
	}
	case RequestType::Segment:
	{
		// Response Segment
		ResponseSegment(exchange, variant_name, number);
		break;
	}
	default:
	{
		response->SetStatusCode(http::StatusCode::NotFound);
		ResponseData(exchange);
		break;
	}
	}
}

bool HlsSession::ParseFileName(const ov::String &file_name, RequestType &type, ov::String &playlist, ov::String &variant_name, uint32_t &number) const
{
	// [Legacy HLS (Version 3) URLs]

	// * Master Playlist
	// http[s]://<host>:<port>/<application_name>/<stream_name>/ts:playlist.m3u8 
	// http[s]://<host>:<port>/<application_name>/<stream_name>/playlist.m3u8?format=ts

	// * Media Playlist
	// http[s]://<host>:<port>/<application_name>/<stream_name>/medialist_<variant_name>_hls.m3u8

	// * Segment File
	// http[s]://<host>:<port>/<application_name>/<stream_name>/seg_<variant_name>_<number>_hls.ts

	// * VTT File
	// http[s]://<host>:<port>/<application_name>/<stream_name>/seg_<variant_name>_<number>_hls.vtt

	auto name_ext_items = file_name.Split(".");
	if (name_ext_items.size() < 2 || (name_ext_items[1] != "m3u8" && name_ext_items[1] != "ts" && name_ext_items[1] != "vtt"))
	{
		return false;
	}

	auto name = name_ext_items[0];
	auto ext = name_ext_items[1];

	auto name_items = name.Split("_");

	// Master Playlist
	// ts:playlist.m3u8 
	// playlist.m3u8?format=ts
	if (ext.LowerCaseString() == "m3u8" && name.HasPrefix("medialist") == false)
	{
		type = RequestType::Playlist;

		// If it has ts: prefix, remove it
		if (name.LowerCaseString().HasPrefix("ts:"))
		{
			name = name.Substring(3);
		}

		playlist = name;
		return true;
	}
	// Media Playlist
	// medialist_<variant_name>_hls.m3u8
	else if (ext == "m3u8" && name_items[0] == "medialist")
	{
		type = RequestType::Chunklist;

		if (name_items.size() < 2)
		{
			return false;
		}

		variant_name = name_items[1];

		playlist = name;
		return true;
	}
	// Segment File
	// seg_<variant_name>_<number>_hls.ts
	else if ((ext == "ts" && name_items[0] == "seg") || (ext == "vtt" && name_items[0] == "seg"))
	{
		type = RequestType::Segment;

		if (name_items.size() < 3)
		{
			return false;
		}

		variant_name = name_items[1];
		number = ov::Converter::ToInt32(name_items[2]);

		return true;
	}
	else
	{
		logte("TsSession::ParseFileName - Invalid file name(%s)", file_name.CStr());
		return false;
	}

	return false;
}

bool HlsSession::GetRewindOptionValue(const std::shared_ptr<ov::Url> &url) const
{
	bool rewind_option = _default_option_rewind; // Default value by config

	if (url->GetQueryValue("_HLS_rewind") == "YES")
	{
		rewind_option = true;
	}
	else if (url->GetQueryValue("_HLS_rewind") == "NO")
	{
		rewind_option = false;
	}

	return rewind_option;
}

void HlsSession::ResponseMasterPlaylist(const std::shared_ptr<http::svr::HttpExchange> &exchange, const ov::String &playlist)
{
	auto stream = std::static_pointer_cast<HlsStream>(GetStream());
	if (stream == nullptr)
	{
		return;
	}

	auto request = exchange->GetRequest();
	auto request_url = request->GetParsedUri();
	auto response = exchange->GetResponse();

	bool rewind_option = GetRewindOptionValue(request_url);

	auto [result, master_playlist] = stream->GetMasterPlaylistData(playlist, rewind_option);
	if (result == HlsStream::RequestResult::Success)
	{
		response->SetStatusCode(http::StatusCode::OK);
		response->SetHeader("Content-Type", "application/vnd.apple.mpegurl");
		response->AppendData(master_playlist);
	}
	else if (result == HlsStream::RequestResult::NotFound)
	{
		response->SetStatusCode(http::StatusCode::NotFound);
	}
	else
	{
		response->SetStatusCode(http::StatusCode::InternalServerError);
	}

	ResponseData(exchange);
}

void HlsSession::ResponseMediaPlaylist(const std::shared_ptr<http::svr::HttpExchange> &exchange, const ov::String &variant_name)
{
	auto stream = std::static_pointer_cast<HlsStream>(GetStream());
	if (stream == nullptr)
	{
		return;
	}

	auto request = exchange->GetRequest();
	auto request_url = request->GetParsedUri();
	auto response = exchange->GetResponse();
	bool rewind_option = GetRewindOptionValue(request_url);
	auto [result, media_playlist] = stream->GetMediaPlaylistData(variant_name, rewind_option);
	if (result == HlsStream::RequestResult::Success)
	{
		response->SetStatusCode(http::StatusCode::OK);
		response->SetHeader("Content-Type", "application/vnd.apple.mpegurl");
		response->AppendData(media_playlist);
	}
	else if (result == HlsStream::RequestResult::NotFound)
	{
		response->SetStatusCode(http::StatusCode::NotFound);
	}
	else
	{
		response->SetStatusCode(http::StatusCode::InternalServerError);
	}

	ResponseData(exchange);
}

void HlsSession::ResponseSegment(const std::shared_ptr<http::svr::HttpExchange> &exchange, const ov::String &variant_name, uint32_t number)
{
	auto stream = std::static_pointer_cast<HlsStream>(GetStream());
	if (stream == nullptr)
	{
		return;
	}

	auto file_name = exchange->GetRequest()->GetParsedUri()->File();
	auto file_items = file_name.Split(".");
	if (file_items.size() < 2)
	{
		logte("TsSession::ResponseSegment - Invalid file name(%s)", file_name.CStr());
		auto response = exchange->GetResponse();
		response->SetStatusCode(http::StatusCode::BadRequest);
		ResponseData(exchange);
		return;
	}
	auto ext = file_items[1];
	ov::String content_type;
	if (ext == "ts")
	{
		content_type = "video/mp2t";
	}
	else if (ext == "vtt")
	{
		content_type = "text/vtt";
	}
	else
	{
		logte("TsSession::ResponseSegment - Invalid segment file extension(%s)", ext.CStr());
		auto response = exchange->GetResponse();
		response->SetStatusCode(http::StatusCode::BadRequest);
		ResponseData(exchange);
		return;
	}

	auto response = exchange->GetResponse();

	auto [result, segment] = stream->GetSegmentData(variant_name, number);
	if (result == HlsStream::RequestResult::Success)
	{
		response->SetStatusCode(http::StatusCode::OK);
		response->SetHeader("Content-Type", content_type);
		response->AppendData(segment);
	}
	else if (result == HlsStream::RequestResult::NotFound)
	{
		response->SetStatusCode(http::StatusCode::NotFound);
	}
	else
	{
		response->SetStatusCode(http::StatusCode::InternalServerError);
	}

	ResponseData(exchange);
}

void HlsSession::ResponseData(const std::shared_ptr<http::svr::HttpExchange> &exchange)
{
	auto response = exchange->GetResponse();
	auto sent_size = response->Response();

	if (sent_size > 0)
	{
		MonitorInstance->IncreaseBytesOut(*GetStream(), PublisherType::Hls, sent_size);
	}

	logtt("\n%s", exchange->GetDebugInfo().CStr());

	// Terminate the HTTP/2 stream
	exchange->Release();
}