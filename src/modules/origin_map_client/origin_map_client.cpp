//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================

#include "origin_map_client.h"

#include <cstring>

#define OV_LOG_TAG "OriginMapClient"

namespace
{
	// Leave room for a stalled call and successful reconnect before the 10-second origin key TTL expires.
	constexpr timeval kRedisTimeout{3, 0};
}

OriginMapClient::OriginMapClient(const ov::String &redis_host, const ov::String &redis_password)
{
	// Parse ip:port 
	auto ip_port = redis_host.Split(":");
	if (ip_port.size() != 2)
	{
		return;
	}

	_redis_ip = ip_port[0];
	_redis_port = ov::Converter::ToUInt16(ip_port[1]);
	_redis_password = redis_password;

	_update_timer.Push(
		[this](void *paramter) -> ov::DelayQueueAction {
			RetryRegister();
			NofifyStreamsAlive();
			ProcessPendingUnregisters();
			return ov::DelayQueueAction::Repeat;
		},
		500);
	_update_timer.Start();
}

OriginMapClient::~OriginMapClient()
{
	_update_timer.Stop();
	redisFree(_redis_context);
}

bool OriginMapClient::NofifyStreamsAlive()
{
	std::unique_lock<std::recursive_mutex> lock(_origin_map_mutex);
	auto origin_map = _origin_map;
	lock.unlock();

	for (auto &[key, value] : origin_map)
	{
		Update(key, value);
	}

	return true;
}

bool OriginMapClient::RetryRegister()
{
	decltype(_origin_map_candidates) candidates;

	{
		std::unique_lock<std::recursive_mutex> lock(_origin_map_mutex);
		if (_origin_map_candidates.empty())
		{
			return true;
		}

		candidates.swap(_origin_map_candidates);
	}

	decltype(_origin_map_candidates) failed;

	for (auto &kv : candidates)
	{
		auto &key = kv.first;
		auto &value = kv.second;

		if (!Register(key, value))
		{
			failed.emplace(key, std::move(value));
		}
	}

	if (failed.empty() == false)
	{
		std::unique_lock<std::recursive_mutex> lock(_origin_map_mutex);
		for (auto &kv : failed)
		{
			_origin_map_candidates.try_emplace(kv.first, std::move(kv.second));
		}
	}

	return true;
}

bool OriginMapClient::ProcessPendingUnregisters()
{
	decltype(_origin_map_remove_candidates) candidates;

	{
		std::unique_lock<std::recursive_mutex> lock(_origin_map_mutex);
		if (_origin_map_remove_candidates.empty())
		{
			return true;
		}

		candidates.swap(_origin_map_remove_candidates);
	}

	for (auto &[app_stream_name, session_id] : candidates)
	{
		bool stale = true;
		bool registered = false;
		{
			std::lock_guard<std::recursive_mutex> lock(_origin_map_mutex);
			// Only unregister if the session ID matches: a different ID means the stream
			// reconnected and RequestRegister() has already started a new session.
			auto session_it = _session_map.find(app_stream_name);
			if (session_it != _session_map.end() && session_it->second == session_id)
			{
				stale = false;
				_origin_map_candidates.erase(app_stream_name);
				registered = _origin_map.erase(app_stream_name) > 0;
				_session_map.erase(session_it);
			}
		}
		if (registered)
		{
			Unregister(app_stream_name);
		}
		else if (stale)
		{
			logti("OriginMapStore: <%s> unregister skipped (reconnected)", app_stream_name.CStr());
		}
		else
		{
			logti("OriginMapStore: <%s> unregister skipped (not registered)", app_stream_name.CStr());
		}
	}

	return true;
}

bool OriginMapClient::AddOriginMap(const ov::String &app_stream_name, const ov::String &origin_host)
{
	std::lock_guard<std::recursive_mutex> lock(_origin_map_mutex);
	_origin_map[app_stream_name] = origin_host;
	return true;
}

bool OriginMapClient::AddOriginMapCandidate(const ov::String &app_stream_name, const ov::String &origin_host)
{
	std::lock_guard<std::recursive_mutex> lock(_origin_map_mutex);
	_origin_map_candidates[app_stream_name] = origin_host;
	return true;
}

bool OriginMapClient::RequestUnregister(const ov::String &app_stream_name)
{
	std::lock_guard<std::recursive_mutex> lock(_origin_map_mutex);
	_origin_map_remove_candidates.push_back({app_stream_name, _session_map[app_stream_name]});
	return true;
}

bool OriginMapClient::Register(const ov::String &app_stream_name, const ov::String &origin_host)
{
	bool same_registered = false;
	
	{
		// Check if already registered
		auto [reply_type, reply_str] = CommandToRedis("GET %s", app_stream_name.CStr());
		if (reply_type == REDIS_REPLY_NIL)
		{
			// Not exist, keep going
		}
		else if (reply_type == REDIS_REPLY_STRING)
		{
			if (origin_host == reply_str)
			{
				// Already registered with same origin host, so no need to register again.
				same_registered = true;
			}
			else
			{
				logte("<%s> stream is already registered with different origin host (%s)", app_stream_name.CStr(), reply_str.CStr());
				
				AddOriginMapCandidate(app_stream_name, origin_host);

				return false;
			}
		}
		else
		{
			logte("Failed to get origin host from redis : %s:%d (err(%d):%s)", _redis_ip.CStr(), _redis_port, reply_type, reply_str.CStr());
			return false;
		}
	}

	if (same_registered == false)
	{
		// Set origin host to redis
		// The EXPIRE option is to prevent locking the app/stream when OvenMediaEngine unexpectedly stops.
		// So _update_timer updates the expire time once every 2.5 seconds.
		// If it is successful, it will return REDIS_REPLY_STATUS with "OK".
		auto [reply_type, reply_str] = CommandToRedis("SET %s %s EX %d NX", app_stream_name.CStr(), origin_host.CStr(), ORIGIN_MAP_STORE_KEY_EXPIRE_TIME);
		if (reply_type == REDIS_ERR || reply_type == REDIS_REPLY_ERROR)
		{
			logte("Failed to register origin host to redis : %s:%d/%s/%s (err(%d):%s)", _redis_ip.CStr(), _redis_port, app_stream_name.CStr(), origin_host.CStr(), reply_type, reply_str.CStr());
			return false;
		}
		else if (reply_type == REDIS_REPLY_NIL)
		{
			logte("<%s> stream is already registered.", app_stream_name.CStr());
			AddOriginMapCandidate(app_stream_name, origin_host);
			return false;
		}
	}

	AddOriginMap(app_stream_name, origin_host);

	logti("OriginMapStore: <%s> stream is registered with origin host : %s", app_stream_name.CStr(), origin_host.CStr());

	return true;
}

bool OriginMapClient::RequestRegister(const ov::String &app_stream_name, const ov::String &origin_host)
{
	std::lock_guard<std::recursive_mutex> lock(_origin_map_mutex);
	// Increment session so any queued pending-unregister entry becomes stale and will be skipped
	// by ProcessPendingUnregisters() when it compares session IDs.
	_session_map[app_stream_name]++;
	_origin_map_candidates[app_stream_name] = origin_host;
	return true;
}

bool OriginMapClient::Update(const ov::String &app_stream_name, const ov::String &origin_host)
{
	// Set origin host to redis
	// XX option or EXPIRE cmd are not used because if redis server is restarted, update() can restore the origin stream info.
	auto [reply_type, reply_str] = CommandToRedis("SET %s %s EX %d", app_stream_name.CStr(), origin_host.CStr(), ORIGIN_MAP_STORE_KEY_EXPIRE_TIME);
	if (reply_type == REDIS_REPLY_STATUS && reply_str == "OK")
	{
		// Updated
	}
	else if (reply_type == REDIS_REPLY_NIL)
	{
		logte("Failed to update origin host to redis because the key does not exist : %s:%d (err(%d):%s)", _redis_ip.CStr(), _redis_port, reply_type, reply_str.CStr());
		return false;
	}
	else
	{
		logte("Failed to update origin host to redis : %s:%d (err(%d):%s)", _redis_ip.CStr(), _redis_port, reply_type, reply_str.CStr());
		return false;
	}

	return true;
}

bool OriginMapClient::Unregister(const ov::String &app_stream_name)
{
	auto [reply_type, reply_str] = CommandToRedis("DEL %s", app_stream_name.CStr());
	if (reply_type == REDIS_ERR || reply_type == REDIS_REPLY_ERROR)
	{
		logte("Failed to delete origin host from redis : %s:%d (err(%d):%s)", _redis_ip.CStr(), _redis_port, reply_type, reply_str.CStr());
		return false;
	}

	logti("OriginMapStore: <%s> stream is unregistered.", app_stream_name.CStr());

	return true;
}

CommonErrorCode OriginMapClient::GetOrigin(const ov::String &app_stream_name, ov::String &origin_host)
{
	auto [reply_type, reply_str] = CommandToRedis("GET %s", app_stream_name.CStr());
	if (reply_type == REDIS_ERR || reply_type == REDIS_REPLY_ERROR)
	{
		logte("Failed to get origin host from redis : %s:%d (err(%d):%s)", _redis_ip.CStr(), _redis_port, reply_type, reply_str.CStr());
		return CommonErrorCode::ERROR;
	}
	else if (reply_type == REDIS_REPLY_NIL)
	{
		return CommonErrorCode::NOT_FOUND;
	}

	origin_host = reply_str;

	return CommonErrorCode::SUCCESS;
}

bool OriginMapClient::ConnectRedis()
{
	if (CheckConnection())
	{
		return true;
	}

	DisconnectRedis();
	_redis_context = redisConnectWithTimeout(_redis_ip.CStr(), _redis_port, kRedisTimeout);
	if (_redis_context == nullptr || _redis_context->err)
	{
		logte("Failed to connect to redis server. ip: %s, port: %d, err: %s", _redis_ip.CStr(), _redis_port,
			  _redis_context != nullptr ? _redis_context->errstr : "cannot allocate context");
		DisconnectRedis();
		return false;
	}

	// Bound AUTH, PING and stream commands when a connected peer stops responding.
	if (redisSetTimeout(_redis_context, kRedisTimeout) != REDIS_OK)
	{
		logte("Failed to set redis command timeout: %s", _redis_context->errstr);
		DisconnectRedis();
		return false;
	}

	if (_redis_password.IsEmpty() == false)
	{
		auto *reply = static_cast<redisReply *>(redisCommand(_redis_context, "AUTH %s", _redis_password.CStr()));
		const bool authenticated = reply != nullptr && reply->type == REDIS_REPLY_STATUS && reply->str != nullptr && std::strcmp(reply->str, "OK") == 0;
		if (authenticated == false)
		{
			logte("Failed to auth to redis server. ip: %s, port: %d, err: %s", _redis_ip.CStr(), _redis_port,
				  reply != nullptr && reply->str != nullptr ? reply->str : _redis_context->errstr);
			freeReplyObject(reply);
			DisconnectRedis();
			return false;
		}
		freeReplyObject(reply);
	}

	return true;
}

bool OriginMapClient::CheckConnection()
{
	if (_redis_context == nullptr)
	{
		return false;
	}

	auto *reply = static_cast<redisReply *>(redisCommand(_redis_context, "PING"));
	if (reply == nullptr)
	{
		return false;
	}

	freeReplyObject(reply);
	return true;
}

void OriginMapClient::DisconnectRedis()
{
	redisFree(_redis_context);
	_redis_context = nullptr;
}

std::tuple<int, ov::String> OriginMapClient::CommandToRedis(const char *format, ...)
{
	ov::LockGuard lock(_redis_context_mutex);
	if (ConnectRedis() == false)
	{
		logte("Failed to connect redis server : %s:%d", _redis_ip.CStr(), _redis_port);
		return { REDIS_ERR, "Failed to connect redis server" };
	}

	va_list args;
	va_start(args, format);
	auto *reply = static_cast<redisReply *>(redisvCommand(_redis_context, format, args));
	va_end(args);

	if (reply == nullptr)
	{
		logte("Failed to execute command to redis : %s:%d (err:%s)", _redis_ip.CStr(), _redis_port, _redis_context->errstr);
		DisconnectRedis();
		return { REDIS_ERR, "Failed to execute command to redis" };
	}
	if (reply->type == REDIS_REPLY_ERROR)
	{
		ov::String error = reply->str;
		logte("Failed to execute command to redis : %s:%d (err:%s)", _redis_ip.CStr(), _redis_port, error.CStr());
		freeReplyObject(reply);
		return { REDIS_ERR, error };
	}

	int reply_type = reply->type;
	ov::String reply_str;
	if (reply->type == REDIS_REPLY_STRING)
	{
		reply_str = reply->str;
	}
	else if (reply->type == REDIS_REPLY_INTEGER)
	{
		reply_str = ov::Converter::ToString((int64_t)reply->integer);
	}
	else if (reply->type == REDIS_REPLY_NIL)
	{
		reply_str = "NIL";
	}
	else if (reply->type == REDIS_REPLY_STATUS)
	{
		reply_str = reply->str;
	}
	else if (reply->type == REDIS_REPLY_ARRAY)
	{
		reply_str = ov::Converter::ToString((int)reply->elements) + " elements";
	}
	else
	{
		reply_str = "Unknown type";
	}

	freeReplyObject(reply);

	return { reply_type, reply_str };
}
