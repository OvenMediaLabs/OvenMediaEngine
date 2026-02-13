//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================

#include "origin_map_client.h"

#define OV_LOG_TAG "OriginMapClient"

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
	std::deque<ov::String> candidates;

	{
		std::unique_lock<std::recursive_mutex> lock(_origin_map_mutex);
		if (_origin_map_remove_candidates.empty())
		{
			return true;
		}

		candidates.swap(_origin_map_remove_candidates);
	}

	for (auto &app_stream_name : candidates)
	{
		Unregister(app_stream_name);
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
	_origin_map_remove_candidates.push_back(app_stream_name);
	return true;
}

bool OriginMapClient::DeleteOriginMap(const ov::String &app_stream_name)
{
	std::lock_guard<std::recursive_mutex> lock(_origin_map_mutex);
	auto origin_cand_it = _origin_map_candidates.find(app_stream_name);
	if (origin_cand_it != _origin_map_candidates.end())
	{
		_origin_map_candidates.erase(origin_cand_it);
	}

	auto origin_map_it = _origin_map.find(app_stream_name);
	if (origin_map_it != _origin_map.end())
	{
		_origin_map.erase(origin_map_it);
		return true;
	}

	return false;
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
	return AddOriginMapCandidate(app_stream_name, origin_host);
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
	if (DeleteOriginMap(app_stream_name) == true)
	{
		auto [reply_type, reply_str] = CommandToRedis("DEL %s", app_stream_name.CStr());
		if (reply_type == REDIS_ERR || reply_type == REDIS_REPLY_ERROR)
		{
			logte("Failed to delete origin host from redis : %s:%d (err(%d):%s)", _redis_ip.CStr(), _redis_port, reply_type, reply_str.CStr());
			return false;
		}
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
	std::lock_guard<std::mutex> lock(_redis_context_mutex);

	if (CheckConnection() == true)
	{
		return true;
	}

	// connect to redis server
	_redis_context = redisConnect(_redis_ip.CStr(), _redis_port);
	if (_redis_context == nullptr || _redis_context->err)
	{
		if (_redis_context == nullptr)
		{
			redisFree(_redis_context);
			_redis_context = nullptr;
		}

		logte("Failed to connect to redis server. ip: %s, port: %d, err: %s", _redis_ip.CStr(), _redis_port, _redis_context->errstr);
		return false;
	}

	// Auth
	if (_redis_password.IsEmpty() == false)
	{
		redisReply *reply = (redisReply *)redisCommand(_redis_context, "AUTH %s", _redis_password.CStr());
		if (reply == nullptr || reply->type == REDIS_REPLY_ERROR)
		{
			logte("Failed to auth to redis server. ip: %s, port: %d, err: %s", _redis_ip.CStr(), _redis_port, reply!=nullptr?reply->str:"nil");

			if (reply != nullptr)
			{
				freeReplyObject(reply);
			}
			
			return false;
		}
	}

	return true;
}

bool OriginMapClient::CheckConnection()
{
	if (_redis_context == nullptr)
	{
		return false;
	}

	// Test with PING command
	redisReply *reply = (redisReply *)redisCommand(_redis_context, "PING");
	if (reply == nullptr)
	{
		return false;
	}

	return true;
}

std::tuple<int, ov::String> OriginMapClient::CommandToRedis(const char *format, ...)
{
	if (ConnectRedis() == false)
	{
		logte("Failed to connect redis server : %s:%d (err:%s)", _redis_ip.CStr(), _redis_port, _redis_context!=nullptr?_redis_context->errstr:"nil");
		return { REDIS_ERR, "Failed to connect redis server" };
	}

	std::unique_lock<std::mutex> lock(_redis_context_mutex);

	va_list args;
	va_start(args, format);
	redisReply *reply = (redisReply *)redisvCommand(_redis_context, format, args);
	va_end(args);

	if (reply == nullptr || reply->type == REDIS_REPLY_ERROR)
	{
		logte("Failed to execute command to redis : %s:%d (err:%s)", _redis_ip.CStr(), _redis_port, reply!=nullptr?reply->str:"nil");
		return { REDIS_ERR, reply!=nullptr?reply->str:"Failed to execute command to redis" };
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
