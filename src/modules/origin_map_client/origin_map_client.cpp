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

OriginMapClient::OriginMapClient(const ov::String &redis_host, const ov::String &redis_password, bool redis_cluster, bool redis_tls)
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
	_redis_cluster_enabled = redis_cluster;
	_redis_tls_enabled = redis_tls;

	_redis_connection_options.host = _redis_ip.CStr();
	_redis_connection_options.port = _redis_port;
	_redis_connection_options.password = _redis_password.CStr();
	_redis_connection_options.keep_alive = true;
	_redis_connection_options.tls.enabled = _redis_tls_enabled;
	_redis_connection_options.tls.sni = _redis_ip.CStr();
	// Pinned hiredis 1.0.2's redisCreateSSLContext() only loads a trust store when
	// cacert/capath is given -- unlike newer hiredis, it has no fallback to the
	// system default verify paths. Leaving cacert empty builds an SSL_CTX with no
	// trust anchors, so verify_mode's default REDIS_SSL_VERIFY_PEER can never
	// succeed and every handshake to a TLS-enabled Redis (e.g. ElastiCache
	// Serverless) fails. Point at the system CA bundle, present on the Amazon
	// Linux 2023 AMI this is built for.
	_redis_connection_options.tls.cacert = "/etc/ssl/certs/ca-certificates.crt";

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
		ov::String registered_origin;
		auto result = GetValue(app_stream_name, registered_origin);
		if (result == CommonErrorCode::NOT_FOUND)
		{
			// Not exist, keep going
		}
		else if (result == CommonErrorCode::SUCCESS)
		{
			if (origin_host == registered_origin)
			{
				// Already registered with same origin host, so no need to register again.
				same_registered = true;
			}
			else
			{
				logte("<%s> stream is already registered with different origin host (%s)", app_stream_name.CStr(), registered_origin.CStr());
				
				AddOriginMapCandidate(app_stream_name, origin_host);

				return false;
			}
		}
		else
		{
			return false;
		}
	}

	if (same_registered == false)
	{
		// Set origin host to redis
		// The EXPIRE option is to prevent locking the app/stream when OvenMediaEngine unexpectedly stops.
		// So _update_timer updates the expire time once every 2.5 seconds.
		// If it is successful, it will return REDIS_REPLY_STATUS with "OK".
		auto result = SetValue(app_stream_name, origin_host, true);
		if (result == SetResult::Error)
		{
			return false;
		}
		else if (result == SetResult::NotSet)
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
	auto result = SetValue(app_stream_name, origin_host, false);
	if (result == SetResult::Success)
	{
		// Updated
	}
	else
	{
		return false;
	}

	return true;
}

bool OriginMapClient::Unregister(const ov::String &app_stream_name)
{
	if (DeleteOriginMap(app_stream_name) == true)
	{
		if (DeleteValue(app_stream_name) == false)
		{
			return false;
		}
	}

	logti("OriginMapStore: <%s> stream is unregistered.", app_stream_name.CStr());

	return true;
}

CommonErrorCode OriginMapClient::GetOrigin(const ov::String &app_stream_name, ov::String &origin_host)
{
	return GetValue(app_stream_name, origin_host);
}

CommonErrorCode OriginMapClient::GetValue(const ov::String &key, ov::String &value)
{
	std::lock_guard<std::mutex> lock(_redis_mutex);
	if (EnsureClient() == false)
	{
		return CommonErrorCode::ERROR;
	}

	try
	{
		sw::redis::OptionalString result;
		if (_redis_cluster_enabled)
		{
			result = _redis_cluster->get(key.CStr());
		}
		else
		{
			result = _redis->get(key.CStr());
		}

		if (result)
		{
			value = result->c_str();
			return CommonErrorCode::SUCCESS;
		}
		return CommonErrorCode::NOT_FOUND;
	}
	catch (const sw::redis::Error &error)
	{
		logte("Failed to get key from Redis %s:%d: %s", _redis_ip.CStr(), _redis_port, error.what());
		ResetClient();
		return CommonErrorCode::ERROR;
	}
}

OriginMapClient::SetResult OriginMapClient::SetValue(const ov::String &key, const ov::String &value, bool only_if_absent)
{
	std::lock_guard<std::mutex> lock(_redis_mutex);
	if (EnsureClient() == false)
	{
		return SetResult::Error;
	}

	try
	{
		const auto update_type = only_if_absent ? sw::redis::UpdateType::NOT_EXIST : sw::redis::UpdateType::ALWAYS;
		bool result;
		if (_redis_cluster_enabled)
		{
			result = _redis_cluster->set(key.CStr(), value.CStr(), std::chrono::seconds(ORIGIN_MAP_STORE_KEY_EXPIRE_TIME), update_type);
		}
		else
		{
			result = _redis->set(key.CStr(), value.CStr(), std::chrono::seconds(ORIGIN_MAP_STORE_KEY_EXPIRE_TIME), update_type);
		}
		return result ? SetResult::Success : SetResult::NotSet;
	}
	catch (const sw::redis::Error &error)
	{
		logte("Failed to set key in Redis %s:%d: %s", _redis_ip.CStr(), _redis_port, error.what());
		ResetClient();
		return SetResult::Error;
	}
}

bool OriginMapClient::DeleteValue(const ov::String &key)
{
	std::lock_guard<std::mutex> lock(_redis_mutex);
	if (EnsureClient() == false)
	{
		return false;
	}

	try
	{
		if (_redis_cluster_enabled)
		{
			_redis_cluster->del(key.CStr());
		}
		else
		{
			_redis->del(key.CStr());
		}
		return true;
	}
	catch (const sw::redis::Error &error)
	{
		logte("Failed to delete key from Redis %s:%d: %s", _redis_ip.CStr(), _redis_port, error.what());
		ResetClient();
		return false;
	}
}

bool OriginMapClient::EnsureClient()
{
	try
	{
		if (_redis_cluster_enabled)
		{
			if (_redis_cluster == nullptr)
			{
				_redis_cluster = std::make_unique<sw::redis::RedisCluster>(_redis_connection_options);
			}
		}
		else if (_redis == nullptr)
		{
			_redis = std::make_unique<sw::redis::Redis>(_redis_connection_options);
		}
		return true;
	}
	catch (const sw::redis::Error &error)
	{
		logte("Failed to connect to Redis %s:%d: %s", _redis_ip.CStr(), _redis_port, error.what());
		ResetClient();
		return false;
	}
}

void OriginMapClient::ResetClient()
{
	if (_redis_cluster_enabled)
	{
		_redis_cluster.reset();
	}
	else
	{
		_redis.reset();
	}
}
