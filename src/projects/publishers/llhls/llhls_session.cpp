//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#include <modules/http/server/http_exchange.h>
#include "llhls_session.h"
#include "llhls_application.h"
#include "llhls_stream.h"
#include "llhls_private.h"

std::shared_ptr<LLHlsSession> LLHlsSession::Create(session_id_t session_id, 
												const bool &origin_mode,
												const ov::String &session_key,
												const std::shared_ptr<pub::Application> &application,
												const std::shared_ptr<pub::Stream> &stream,
												uint64_t session_life_time)
{
	return LLHlsSession::Create(session_id, origin_mode, session_key, application, stream, nullptr, session_life_time);
}

std::shared_ptr<LLHlsSession> LLHlsSession::Create(session_id_t session_id, 
												const bool &origin_mode,
												const ov::String &session_key,
												const std::shared_ptr<pub::Application> &application,
												const std::shared_ptr<pub::Stream> &stream,
												const ov::String &user_agent,
												uint64_t session_life_time)
{
	auto session_info = info::Session(*std::static_pointer_cast<info::Stream>(stream), session_id);
	auto session = std::make_shared<LLHlsSession>(session_info, origin_mode, session_key, application, stream, user_agent, session_life_time);

	if (session->Start() == false)
	{
		return nullptr;
	} 

	return session;
}

LLHlsSession::LLHlsSession(const info::Session &session_info, 
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

	if (_origin_mode == true)
	{
		MonitorInstance->OnSessionConnected(*stream, PublisherType::LLHls);
		_number_of_players = 1;
	}

	logtt("LLHlsSession::LLHlsSession (%d)", session_info.GetId());
}

LLHlsSession::~LLHlsSession()
{
	logtt("LLHlsSession::~LLHlsSession(%d)", GetId());
	MonitorInstance->OnSessionsDisconnected(*GetStream(), PublisherType::LLHls, _number_of_players);
}

bool LLHlsSession::Start()
{
	auto llhls_conf = GetApplication()->GetConfig().GetPublishers().GetLLHlsPublisher();
	auto cache_control = llhls_conf.GetCacheControl();

	_master_playlist_max_age = cache_control.GetMasterPlaylistMaxAge();
	_chunklist_max_age = cache_control.GetChunklistMaxAge();
	_chunklist_with_directives_max_age = cache_control.GetChunklistWithDirectivesMaxAge();
	_segment_max_age = cache_control.GetSegmentMaxAge();
	_partial_segment_max_age = cache_control.GetPartialSegmentMaxAge();

	_hls_legacy = llhls_conf.GetDefaultQueryString().GetBoolValue("_HLS_legacy", kDefaultHlsLegacy);
	_hls_rewind = llhls_conf.GetDefaultQueryString().GetBoolValue("_HLS_rewind", kDefaultHlsRewind);
	
	return Session::Start();
}

bool LLHlsSession::Stop()
{
	logtt("LLHlsSession(%u) : Pending request size(%d)", GetId(), _pending_requests.size());

	return Session::Stop();
}

const ov::String &LLHlsSession::GetSessionKey() const
{
	return _session_key;
}

const ov::String &LLHlsSession::GetUserAgent() const
{
	return _user_agent;
}

void LLHlsSession::UpdateLastRequest(uint32_t connection_id)
{
	std::lock_guard<std::shared_mutex> lock(_last_request_time_guard);
	_last_request_time[connection_id] = ov::Clock::NowMSec();

	logtt("LLHlsSession(%u) : Request updated from %u : size(%d)", GetId(), connection_id, _last_request_time.size());
}

uint64_t LLHlsSession::GetLastRequestTime(uint32_t connection_id) const
{
	std::shared_lock<std::shared_mutex> lock(_last_request_time_guard);
	auto itr = _last_request_time.find(connection_id);
	if (itr == _last_request_time.end())
	{
		return 0;
	}

	return itr->second;
}

void LLHlsSession::OnConnectionDisconnected(uint32_t connection_id)
{
	// lock
	std::lock_guard<std::shared_mutex> lock(_last_request_time_guard);
	_last_request_time.erase(connection_id);

	logtt("LLHlsSession(%u) : Disconnected from %u : size(%d)", GetId(), connection_id, _last_request_time.size());
}

bool LLHlsSession::IsNoConnection() const
{
	std::shared_lock<std::shared_mutex> lock(_last_request_time_guard);
	return _last_request_time.empty();
}

// pub::Session Interface
void LLHlsSession::SendOutgoingData(const std::any &notification)
{
	// Check expired time
	if(_session_life_time != 0 && _session_life_time < ov::Clock::NowMSec())
	{
		return;
	}

	std::shared_ptr<LLHlsStream::PlaylistUpdatedEvent> event;
	try
	{
		event = std::any_cast<std::shared_ptr<LLHlsStream::PlaylistUpdatedEvent>>(notification);
		if (event == nullptr)
		{
			return;
		}
	}
	catch (const std::bad_any_cast& e)
	{
		logtc("LLHlsSession : Invalid notification type : %s", e.what());
		return;
	}

	OnPlaylistUpdated(event->track_id, event->msn, event->part);
}

void LLHlsSession::OnMessageReceived(const std::any &message)
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
	
	RequestType file_type;
	int32_t track_id;
	int64_t segment_number;
	int64_t partial_number;
	ov::String stream_key;

	if (ParseFileName(file, file_type, track_id, segment_number, partial_number, stream_key) == false)
	{
		response->SetStatusCode(http::StatusCode::NotFound);
		ResponseData(exchange);
		return;
	}

	auto llhls_stream = std::static_pointer_cast<LLHlsStream>(GetStream());
	if (llhls_stream == nullptr)
	{
		response->SetStatusCode(http::StatusCode::InternalServerError);
		ResponseData(exchange);
		return;
	}

	if (_origin_mode == false && file_type != RequestType::Playlist && file_type != RequestType::Chunklist)
	{
		// All requests except playlist have a stream key
		if (stream_key != llhls_stream->GetStreamKey())
		{
			logtw("LLHlsSession::OnMessageReceived(%u) - Invalid stream key : %s (expected : %s)", GetId(), stream_key.CStr(), llhls_stream->GetStreamKey().CStr());
			response->SetStatusCode(http::StatusCode::NotFound);
			ResponseData(exchange);
			return;
		}
	}

	switch (file_type)
	{
	case RequestType::Playlist:
	{	
		bool legacy = _hls_legacy; // default value by config
		bool rewind = _hls_rewind; // default value by config

		if (request_uri->HasQueryKey("_HLS_legacy"))
		{
			if (request_uri->GetQueryValue("_HLS_legacy").UpperCaseString() == "YES")
			{
				legacy = true;
			}
			else
			{
				legacy = false;
			}
		}

		if (request_uri->HasQueryKey("_HLS_rewind"))
		{
			if (request_uri->GetQueryValue("_HLS_rewind").UpperCaseString() == "YES")
			{
				rewind = true;
			}
			else
			{
				rewind = false;
			}
		}

		ResponsePlaylist(exchange, file, legacy, rewind);
		break;
	}
	case RequestType::Chunklist:
	{
		int64_t msn = -1, part = -1;
		bool skip = false;
		bool legacy = _hls_legacy; // default value by config
		bool rewind = _hls_rewind; // default value by config

		// ?_HLS_msn=<M>&_HLS_part=<N>&_HLS_skip=YES|v2
		if (request_uri->HasQueryKey("_HLS_msn"))
		{
			msn = ov::Converter::ToInt64(request_uri->GetQueryValue("_HLS_msn").CStr());
		}

		if (request_uri->HasQueryKey("_HLS_part"))
		{
			part = ov::Converter::ToInt64(request_uri->GetQueryValue("_HLS_part").CStr());
		}

		if (request_uri->HasQueryKey("_HLS_skip"))
		{
			if (request_uri->GetQueryValue("_HLS_skip") == "YES")
			{
				skip = true;
			}
			// v2 is not supported yet
		}

		if (request_uri->HasQueryKey("_HLS_legacy"))
		{
			if (request_uri->GetQueryValue("_HLS_legacy").UpperCaseString() == "YES")
			{
				legacy = true;
			}
			else
			{
				legacy = false;
			}
		}

		if (request_uri->HasQueryKey("_HLS_rewind"))
		{
			if (request_uri->GetQueryValue("_HLS_rewind").UpperCaseString() == "YES")
			{
				rewind = true;
			}
			else
			{
				rewind = false;
			}
		}

		ResponseChunklist(exchange, file, track_id, msn, part, skip, legacy, rewind);
		break;
	}
	case RequestType::InitializationSegment:
		ResponseInitializationSegment(exchange, file, track_id);
		break;
	case RequestType::Segment:
		ResponseSegment(exchange, file, track_id, segment_number);
		break;
	case RequestType::PartialSegment:
		ResponsePartialSegment(exchange, file, track_id, segment_number, partial_number);
		break;
	default:
		break;
	}
}

bool LLHlsSession::ParseFileName(const ov::String &file_name, RequestType &type, int32_t &track_id, int64_t &segment_number, int64_t &partial_number, ov::String &stream_key) const
{
	// Split to filename.ext
	auto name_ext_items = file_name.Split(".");
	if (name_ext_items.size() < 2 || (name_ext_items[1] != "m4s" && name_ext_items[1] != "m3u8" && name_ext_items[1] != "vtt"))
	{
		logtw("Invalid file name requested: %s", file_name.CStr());
		return false;
	}

	auto name_items = name_ext_items[0].Split("_");
	if (name_ext_items[1] == "m3u8" && name_items[0] != "chunklist")
	{
		// *.m3u8 and NOT chunklist*.m3u8
		type = RequestType::Playlist;
	}
	else if (name_items[0] == "chunklist")
	{
		// chunklist_<track id>_<media type>_<stream_key>_llhls.m3u8?session_key=<key>&_HLS_msn=<M>&_HLS_part=<N>&_HLS_skip=YES|v2&_HLS_legacy=YES
		if (name_items.size() < 5 || name_ext_items[1] != "m3u8")
		{
			logtw("Invalid chunklist file name requested: %s", file_name.CStr());
			return false;
		}

		type = RequestType::Chunklist;
		track_id = ov::Converter::ToInt32(name_items[1].CStr());
	}
	else if (name_items[0] == "init")
	{
		// init_<track id>_<media type>_<stream key>_llhls
		if (name_items.size() < 5 || name_ext_items[1] != "m4s")
		{
			logtw("Invalid file name requested: %s", file_name.CStr());
			return false;
		}

		type = RequestType::InitializationSegment;
		track_id = ov::Converter::ToInt32(name_items[1].CStr());
		stream_key = name_items[3];
	}
	else if (name_items[0] == "seg" && (name_ext_items[1] == "m4s" || name_ext_items[1] == "vtt"))
	{
		// seg_<track id>_<segment number>_<media type>_<stream key>_llhls
		if (name_items.size() < 6)
		{
			logtw("Invalid file name requested: %s", file_name.CStr());
			return false;
		}

		type = RequestType::Segment;
		track_id = ov::Converter::ToInt32(name_items[1].CStr());
		segment_number = ov::Converter::ToInt64(name_items[2].CStr());
		stream_key = name_items[4];
	}
	else if (name_items[0] == "part" && (name_ext_items[1] == "m4s" || name_ext_items[1] == "vtt"))
	{
		// part_<track id>_<segment number>_<partial number>_<media type>_<stream key>_llhls
		if (name_items.size() < 7)
		{
			logtw("Invalid file name requested: %s", file_name.CStr());
			return false;
		}

		type = RequestType::PartialSegment;
		track_id = ov::Converter::ToInt32(name_items[1].CStr());
		segment_number = ov::Converter::ToInt64(name_items[2].CStr());
		partial_number = ov::Converter::ToInt64(name_items[3].CStr());
		stream_key = name_items[5];
	}
	else
	{
		logtw("Invalid file name requested: %s", file_name.CStr());
		return false;
	}

	return true;
}

void LLHlsSession::ResponsePlaylist(const std::shared_ptr<http::svr::HttpExchange> &exchange, const ov::String &file_name, bool legacy, bool rewind, bool holdIfAccepted /*= true*/)
{
	auto llhls_stream = std::static_pointer_cast<LLHlsStream>(GetStream());
	if (llhls_stream == nullptr)
	{
		return;
	}

	auto request = exchange->GetRequest();
	auto response = exchange->GetResponse();
	auto request_uri = exchange->GetRequest()->GetParsedUri();

	ov::String content_encoding = "identity";
	bool gzip = false;
	auto encodings = request->GetHeader("Accept-Encoding");
	if (encodings.IndexOf("gzip") >= 0 || encodings.IndexOf("*") >= 0)
	{
		gzip = true;
		content_encoding = "gzip";
	}

	// Get the playlist
	auto query_string = MakeQueryStringToPropagate(request_uri);
	auto [result, playlist] = llhls_stream->GetMasterPlaylist(file_name, query_string, gzip, legacy, rewind);
	if (result == LLHlsStream::RequestResult::Success)
	{
		// Send the playlist
		response->SetStatusCode(http::StatusCode::OK);
		// Set Content-Type header
		response->SetHeader("Content-Type", "application/vnd.apple.mpegurl");
		// gzip compression
		response->SetHeader("Content-Encoding", content_encoding);

		// Cache-Control header
		// When the stream is recreated, llhls.m3u8 file is changed.

		if (_master_playlist_max_age >= 0)
		{
			ov::String cache_control;
			if (_master_playlist_max_age == 0)
			{
				cache_control = ov::String::FormatString("no-cache, no-store");
			}
			else
			{
				cache_control = ov::String::FormatString("max-age=%d", _master_playlist_max_age);
			}

			response->SetHeader("Cache-Control", cache_control);
		}

		response->AppendData(playlist);

		if (_origin_mode == false)
		{
			MonitorInstance->OnSessionConnected(*GetStream(), PublisherType::LLHls);
			_number_of_players += 1;
		}
	}
	else if (result == LLHlsStream::RequestResult::Accepted && holdIfAccepted == true)
	{
		// llhls.m3u8 is transmitted when more than one segment (any track) is created.
		AddPendingRequest(exchange, RequestType::Playlist, file_name, 0, 1, 0, false, legacy, rewind);
		return ;
	}
	else
	{
		if (holdIfAccepted == false)
		{
			logtw("%s/%s/%s Failed to respond to pending request.", GetApplication()->GetVHostAppName().CStr(), GetStream()->GetName().CStr(), file_name.CStr());
		}

		// Send error response
		response->SetStatusCode(http::StatusCode::NotFound);
	}

	ResponseData(exchange);
}

void LLHlsSession::ResponseChunklist(const std::shared_ptr<http::svr::HttpExchange> &exchange, const ov::String &file_name, const int32_t &track_id, int64_t msn, int64_t part, bool skip, bool legacy, bool rewind, bool holdIfAccepted /*= true*/)
{
	auto llhls_stream = std::static_pointer_cast<LLHlsStream>(GetStream());
	if (llhls_stream == nullptr)
	{
		return;
	}

	auto request = exchange->GetRequest();
	auto request_uri = request->GetParsedUri();
	auto response = exchange->GetResponse();
	bool has_delivery_directives = request_uri->HasQueryKey("_HLS_msn");

	if (msn == -1 && part == -1)
	{
		// If there are not enough segments and chunks in the beginning, 
		// the player cannot start playing, so the request is pending until at least one segment is created.
		msn = 2;
		part = 0;
	}

	ov::String content_encoding = "identity";
	bool gzip = false;
	auto encodings = request->GetHeader("Accept-Encoding");
	if (encodings.IndexOf("gzip") >= 0 || encodings.IndexOf("*") >= 0)
	{
		gzip = true;
		content_encoding = "gzip";
	}

	// Get the chunklist
	auto query_string = MakeQueryStringToPropagate(request_uri);

	auto [result, chunklist] = llhls_stream->GetChunklist(query_string, track_id, msn, part, skip, gzip, legacy, rewind);
	if (result == LLHlsStream::RequestResult::Success)
	{
		// Send the chunklist
		response->SetStatusCode(http::StatusCode::OK);
		// Set Content-Type header
		response->SetHeader("Content-Type", "application/vnd.apple.mpegurl");
		// gzip compression
		response->SetHeader("Content-Encoding", content_encoding);

		// Cache-Control header
		ov::String cache_control;
		if (has_delivery_directives == false)
		{
			if (_chunklist_max_age >= 0)
			{
				if (_chunklist_max_age == 0)
				{
					cache_control = ov::String::FormatString("no-cache, no-store");
				}
				else
				{
					cache_control = ov::String::FormatString("max-age=%d", _chunklist_max_age);
				}
				response->SetHeader("Cache-Control", cache_control);
			}
		}
		else
		{
			if (_chunklist_with_directives_max_age >= 0)
			{
				if (_chunklist_with_directives_max_age == 0)
				{
					cache_control = ov::String::FormatString("no-cache, no-store");
				}
				else
				{
					cache_control = ov::String::FormatString("max-age=%d", _chunklist_with_directives_max_age);
				}
				response->SetHeader("Cache-Control", cache_control);
			}
		}

		response->AppendData(chunklist);

		// If a client uses previously cached llhls.m3u8 and requests chunklist
		if (_origin_mode == false && _number_of_players == 0)
		{
			MonitorInstance->OnSessionConnected(*GetStream(), PublisherType::LLHls);
			_number_of_players += 1;
		}
	}
	else if (result == LLHlsStream::RequestResult::Accepted && holdIfAccepted == true)
	{
		// Hold
		//TODO(Getroot): EXT-X-SKIP is under debugging

		skip = false;
		AddPendingRequest(exchange, RequestType::Chunklist, file_name, track_id, msn, part, skip, legacy, rewind);
		return ;
	}
	else
	{
		if (holdIfAccepted == false)
		{
			logtw("%s/%s/%s Failed to respond to pending request.", GetApplication()->GetVHostAppName().CStr(), GetStream()->GetName().CStr(), file_name.CStr());
		}

		// Send error response
		response->SetStatusCode(http::StatusCode::NotFound);
	}

	ResponseData(exchange);
}

void LLHlsSession::ResponseInitializationSegment(const std::shared_ptr<http::svr::HttpExchange> &exchange, const ov::String &file_name, const int32_t &track_id)
{
	auto llhls_stream = std::static_pointer_cast<LLHlsStream>(GetStream());
	if (llhls_stream == nullptr)
	{
		return;
	}

	auto response = exchange->GetResponse();

	// Get the initialization segment
	auto [result, initialization_segment] = llhls_stream->GetInitializationSegment(track_id);
	if (result == LLHlsStream::RequestResult::Success)
	{
		// Send the initialization segment
		response->SetStatusCode(http::StatusCode::OK);
		// Set Content-Type header
		// Set Content-Type header
		if (GetStream()->GetTrack(track_id)->GetMediaType() == cmn::MediaType::Video)
		{
			response->SetHeader("Content-Type", "video/mp4");
		}
		else
		{
			response->SetHeader("Content-Type", "audio/mp4");
		}

		if (_segment_max_age >= 0)
		{
			ov::String cache_control;
			if (_segment_max_age == 0)
			{
				cache_control = ov::String::FormatString("no-cache, no-store");
			}
			else
			{
				cache_control = ov::String::FormatString("max-age=%d", _segment_max_age);
			}
			response->SetHeader("Cache-Control", cache_control);
		}

		response->AppendData(initialization_segment);
	}
	else
	{
		// Send error response
		response->SetStatusCode(http::StatusCode::NotFound);
	}

	ResponseData(exchange);
}

void LLHlsSession::ResponseSegment(const std::shared_ptr<http::svr::HttpExchange> &exchange, const ov::String &file_name, const int32_t &track_id, const int64_t &segment_number)
{
	auto llhls_stream = std::static_pointer_cast<LLHlsStream>(GetStream());
	if (llhls_stream == nullptr)
	{
		return;
	}

	auto response = exchange->GetResponse();

	// Get the segment
	auto [result, segment] = llhls_stream->GetSegment(track_id, segment_number);
	if (result == LLHlsStream::RequestResult::Success)
	{
		// Send the segment
		response->SetStatusCode(http::StatusCode::OK);
		// Set Content-Type header
		if (GetStream()->GetTrack(track_id)->GetMediaType() == cmn::MediaType::Video)
		{
			response->SetHeader("Content-Type", "video/mp4");
		}
		else if (GetStream()->GetTrack(track_id)->GetMediaType() == cmn::MediaType::Audio)
		{
			response->SetHeader("Content-Type", "audio/mp4");
		}
		else if (GetStream()->GetTrack(track_id)->GetMediaType() == cmn::MediaType::Subtitle)
		{
			response->SetHeader("Content-Type", "text/vtt");
		}
		else
		{
			response->SetHeader("Content-Type", "application/octet-stream");
		}

		if (_segment_max_age >= 0)
		{
			ov::String cache_control;
			if (_segment_max_age == 0)
			{
				cache_control = ov::String::FormatString("no-cache, no-store");
			}
			else
			{
				cache_control = ov::String::FormatString("max-age=%d", _segment_max_age);
			}
			response->SetHeader("Cache-Control", cache_control);
		}

		response->AppendData(segment);
	}
	else
	{
		// Send error response
		response->SetStatusCode(http::StatusCode::NotFound);
	}

	ResponseData(exchange);
}

void LLHlsSession::ResponsePartialSegment(const std::shared_ptr<http::svr::HttpExchange> &exchange, const ov::String &file_name, const int32_t &track_id, const int64_t &segment_number, const int64_t &partial_number, bool holdIfAccepted /*= true*/)
{
	auto llhls_stream = std::static_pointer_cast<LLHlsStream>(GetStream());
	if (llhls_stream == nullptr)
	{
		return;
	}

	auto response = exchange->GetResponse();

	// Get the partial segment
	auto [result, partial_segment] = llhls_stream->GetPartial(track_id, segment_number, partial_number);
	if (result == LLHlsStream::RequestResult::Success)
	{
		// Send the partial segment
		response->SetStatusCode(http::StatusCode::OK);
		// Set Content-Type header
		if (GetStream()->GetTrack(track_id)->GetMediaType() == cmn::MediaType::Video)
		{
			response->SetHeader("Content-Type", "video/mp4");
		}
		else if (GetStream()->GetTrack(track_id)->GetMediaType() == cmn::MediaType::Audio)
		{
			response->SetHeader("Content-Type", "audio/mp4");
		}
		else if (GetStream()->GetTrack(track_id)->GetMediaType() == cmn::MediaType::Subtitle)
		{
			response->SetHeader("Content-Type", "text/vtt");
		}
		else
		{
			response->SetHeader("Content-Type", "application/octet-stream");
		}

		if (_partial_segment_max_age >= 0)
		{
			ov::String cache_control;
			if (_partial_segment_max_age == 0)
			{
				cache_control = ov::String::FormatString("no-cache, no-store");
			}
			else
			{
				cache_control = ov::String::FormatString("max-age=%d", _partial_segment_max_age);
			}
			response->SetHeader("Cache-Control", cache_control);
		}

		response->AppendData(partial_segment);
	}
	else if (result == LLHlsStream::RequestResult::Accepted && holdIfAccepted == true)
	{
		// Hold
		AddPendingRequest(exchange, RequestType::PartialSegment, file_name, track_id, segment_number, partial_number, false, false, false);
		return ;
	}
	else
	{
		if (holdIfAccepted == false)
		{
			logtw("%s/%s/%s Failed to respond to pending request.", GetApplication()->GetVHostAppName().CStr(), GetStream()->GetName().CStr(), file_name.CStr());
		}

		// Send error response
		response->SetStatusCode(http::StatusCode::NotFound);
	}

	ResponseData(exchange);
}

void LLHlsSession::ResponseData(const std::shared_ptr<http::svr::HttpExchange> &exchange)
{
	auto response = exchange->GetResponse();
	auto sent_size = response->Response();

	if (sent_size > 0)
	{
		MonitorInstance->IncreaseBytesOut(*GetStream(), PublisherType::LLHls, sent_size);
	}

	logtt("\n%s", exchange->GetDebugInfo().CStr());

	// Terminate the HTTP/2 stream
	exchange->Release();
}

void LLHlsSession::OnPlaylistUpdated(const int32_t &track_id, const int64_t &msn, const int64_t &part)
{
	logtt("LLHlsSession::OnPlaylistUpdated track_id: %d, msn: %lld, part: %lld", track_id, msn, part);
	// Find the pending request

	auto it = _pending_requests.begin();
	while (it != _pending_requests.end())
	{
		if ( (it->type == RequestType::Playlist) &&
			 ((it->segment_number < msn) || (it->segment_number <= msn && it->partial_number <= part)) )
		{
			// Send the playlist
			auto exchange = it->exchange;
			ResponsePlaylist(exchange, it->file_name, it->legacy, it->rewind, false);
			it = _pending_requests.erase(it);
		}
		else if ( (it->track_id == track_id) && 
			 ((it->segment_number < msn) || (it->segment_number <= msn && it->partial_number <= part)) )
		{
			// Resume the request
			switch (it->type)
			{
			case RequestType::Chunklist:
				ResponseChunklist(it->exchange, it->file_name, it->track_id, it->segment_number, it->partial_number, it->skip, it->legacy, it->rewind, false);
				break;
			case RequestType::PartialSegment:
				ResponsePartialSegment(it->exchange, it->file_name, it->track_id, it->segment_number, it->partial_number, false);
				break;
			case RequestType::Segment:
				ResponseSegment(it->exchange, it->file_name, it->track_id, it->segment_number);
				break;
			case RequestType::Playlist:
				// Playlist is processed already above
			case RequestType::InitializationSegment:
				// Initialization segment request is not pending 
			default:
				// Assertion
				OV_ASSERT2(false);
				break;
			}

			// Remove the request
			it = _pending_requests.erase(it);
		}
		else
		{
			// Next
			++it;
		}
	}
}

bool LLHlsSession::AddPendingRequest(const std::shared_ptr<http::svr::HttpExchange> &exchange, const RequestType &type, const ov::String &file_name, const int32_t &track_id, const int64_t &segment_number, const int64_t &partial_number, const bool &skip, const bool &legacy, const bool &rewind)
{
	// Add the request to the pending list
	PendingRequest request;
	request.type = type;
	request.file_name = file_name;
	request.track_id = track_id;
	request.segment_number = segment_number;
	request.partial_number = partial_number;
	request.skip = skip;
	request.legacy = legacy;
	request.rewind = rewind;
	request.exchange = exchange;

	// Add the request to the pending list
	_pending_requests.push_back(request);

	if (_pending_requests.size() > MAX_PENDING_REQUESTS)
	{
		logtt("[%s/%s/%u] Too many pending requests (%u)", 
				GetApplication()->GetVHostAppName().CStr(),
				GetStream()->GetName().CStr(),
				GetId(),
				_pending_requests.size());
	}

	return true;
}

ov::String LLHlsSession::MakeQueryStringToPropagate(const std::shared_ptr<ov::Url> &request_uri)
{
	auto query_string = ov::String::FormatString("session=%u_%s", GetId(), _session_key.CStr());
	if (_origin_mode == true)
	{
		// Origin mode doesn't need session key
		query_string.Clear();
	}

	// stream_key is propagated to the child resources
	ov::String stream_key;
	if (request_uri->HasQueryKey("stream_key"))
	{
		stream_key = request_uri->GetQueryValue("stream_key");
	}

	if (stream_key.IsEmpty() == false)
	{
		if (query_string.IsEmpty() == false)
		{
			query_string += "&";
		}

		query_string.AppendFormat("stream_key=%s", stream_key.CStr());
	}

	return query_string;
}