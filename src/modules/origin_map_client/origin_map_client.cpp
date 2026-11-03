//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================

#include "origin_map_client.h"

#include <arpa/inet.h>
#include <unistd.h>

#include <cstdlib>

#define OV_LOG_TAG "OriginMapClient"

namespace
{
	// Every Redis call holds _redis_mutex, so an unreachable endpoint must not block forever.
	constexpr std::chrono::milliseconds REDIS_CONNECT_TIMEOUT{1000};
	constexpr std::chrono::milliseconds REDIS_SOCKET_TIMEOUT{1000};

	// Pinned hiredis 1.0.2's redisCreateSSLContext() only loads a trust store when
	// cacert/capath is given -- unlike newer hiredis, it has no fallback to the
	// system default verify paths. Without one, every TLS handshake fails.
	const char *const SYSTEM_CA_BUNDLE_PATHS[] = {
		"/etc/ssl/certs/ca-certificates.crt",	 // Debian, Ubuntu
		"/etc/pki/tls/certs/ca-bundle.crt",		 // RHEL, CentOS, Amazon Linux
		"/etc/ssl/ca-bundle.pem",				 // SUSE
		"/etc/ssl/cert.pem",					 // Alpine, macOS
	};

	std::string FindSystemCaBundle()
	{
		for (const auto *path : SYSTEM_CA_BUNDLE_PATHS)
		{
			if (::access(path, R_OK) == 0)
			{
				return path;
			}
		}

		return "";
	}

	bool IsIpAddress(const std::string &host)
	{
		unsigned char buffer[sizeof(struct in6_addr)];
		return ::inet_pton(AF_INET, host.c_str(), buffer) == 1 || ::inet_pton(AF_INET6, host.c_str(), buffer) == 1;
	}
}  // namespace

bool OriginMapClient::BuildConnectionOptions(const ov::String &redis_host, const ov::String &redis_password, bool redis_tls, const ov::String &redis_ca_cert, sw::redis::ConnectionOptions &options)
{
	// Parse host:port
	const std::string host_port = redis_host.CStr();
	const auto separator = host_port.rfind(':');
	if (separator == std::string::npos)
	{
		return false;
	}

	const auto host = host_port.substr(0, separator);
	const auto port = host_port.substr(separator + 1);
	if (host.empty() || host.find(':') != std::string::npos ||
		port.empty() || port.size() > 5 || port.find_first_not_of("0123456789") != std::string::npos)
	{
		return false;
	}

	const auto port_number = std::strtoul(port.c_str(), nullptr, 10);
	if (port_number == 0 || port_number > 65535)
	{
		return false;
	}

	options = sw::redis::ConnectionOptions();
	options.host = host;
	options.port = static_cast<int>(port_number);
	options.password = redis_password.CStr();
	options.keep_alive = true;
	options.connect_timeout = REDIS_CONNECT_TIMEOUT;
	options.socket_timeout = REDIS_SOCKET_TIMEOUT;
	options.tls.enabled = redis_tls;

	if (redis_tls)
	{
		// RFC 6066 does not allow an IP address as SNI
		if (IsIpAddress(host) == false)
		{
			options.tls.sni = host;
		}

		options.tls.cacert = redis_ca_cert.IsEmpty() ? FindSystemCaBundle() : std::string(redis_ca_cert.CStr());
		if (options.tls.cacert.empty())
		{
			logtw("No CA bundle found for Redis TLS. Set <CaCert> in <RedisServer>, otherwise server verification will fail.");
		}
	}

	return true;
}

OriginMapClient::OriginMapClient(const ov::String &redis_host, const ov::String &redis_password, bool redis_cluster, bool redis_tls, const ov::String &redis_ca_cert)
{
	_redis_cluster_enabled = redis_cluster;

	_redis_options_valid = BuildConnectionOptions(redis_host, redis_password, redis_tls, redis_ca_cert, _redis_connection_options);
	if (_redis_options_valid == false)
	{
		logte("Invalid Redis host \"%s\" > it must be host:port", redis_host.CStr());
		return;
	}

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
	// Join the timer thread before the Redis clients it uses are destroyed
	_update_timer.Stop();
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

template <typename Func>
bool OriginMapClient::WithClient(const char *operation, Func &&func)
{
	std::lock_guard<std::mutex> lock(_redis_mutex);
	if (EnsureClient() == false)
	{
		return false;
	}

	// redis++ reconnects broken connections and follows cluster redirects by itself,
	// so a failed command is only reported; the client is kept.
	try
	{
		if (_redis_cluster_enabled)
		{
			func(*_redis_cluster);
		}
		else
		{
			func(*_redis);
		}

		return true;
	}
	catch (const sw::redis::Error &error)
	{
		logte("Failed to %s on Redis %s:%d: %s", operation, _redis_connection_options.host.c_str(), _redis_connection_options.port, error.what());
		return false;
	}
}

CommonErrorCode OriginMapClient::GetValue(const ov::String &key, ov::String &value)
{
	sw::redis::OptionalString result;
	if (WithClient("get key", [&](auto &redis) { result = redis.get(key.CStr()); }) == false)
	{
		return CommonErrorCode::ERROR;
	}

	if (result)
	{
		value = result->c_str();
		return CommonErrorCode::SUCCESS;
	}

	return CommonErrorCode::NOT_FOUND;
}

OriginMapClient::SetResult OriginMapClient::SetValue(const ov::String &key, const ov::String &value, bool only_if_absent)
{
	const auto update_type = only_if_absent ? sw::redis::UpdateType::NOT_EXIST : sw::redis::UpdateType::ALWAYS;

	bool result = false;
	auto succeeded = WithClient("set key", [&](auto &redis) {
		result = redis.set(key.CStr(), value.CStr(), std::chrono::seconds(ORIGIN_MAP_STORE_KEY_EXPIRE_TIME), update_type);
	});

	if (succeeded == false)
	{
		return SetResult::Error;
	}

	return result ? SetResult::Success : SetResult::NotSet;
}

bool OriginMapClient::DeleteValue(const ov::String &key)
{
	return WithClient("delete key", [&](auto &redis) { redis.del(key.CStr()); });
}

// Must be called with _redis_mutex held
bool OriginMapClient::EnsureClient()
{
	if (_redis_options_valid == false)
	{
		return false;
	}

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
		logte("Failed to connect to Redis %s:%d: %s", _redis_connection_options.host.c_str(), _redis_connection_options.port, error.what());
		return false;
	}
}
