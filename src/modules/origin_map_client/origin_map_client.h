//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/common_types.h>
#include <base/ovlibrary/ovlibrary.h>
#include <base/ovlibrary/delay_queue.h>
#include <sw/redis++/redis++.h>

// redis key expire time (sec)
#define ORIGIN_MAP_STORE_KEY_EXPIRE_TIME 10

// If Origins-Edges cluster uses OriginMapStore, app/stream must be unique in the cluster.
class OriginMapClient
{
public:
	// redis_host: redis server host (ex: 192.168.0.160:6379)
	// redis_password: redis server password (ex: password!@#)
	// redis_ca_cert: CA bundle used to verify the server when redis_tls is true. If empty, common system paths are tried.
	OriginMapClient(const ov::String &redis_host, const ov::String &redis_password, bool redis_cluster, bool redis_tls, const ov::String &redis_ca_cert = "");
	~OriginMapClient();

	// Builds redis++ connection options. Returns false if redis_host is not a valid host:port.
	static bool BuildConnectionOptions(const ov::String &redis_host, const ov::String &redis_password, bool redis_tls, const ov::String &redis_ca_cert, sw::redis::ConnectionOptions &options);

	// Non-blocking request to register origin host
	bool RequestRegister(const ov::String &app_stream_name, const ov::String &origin_host); 
	bool RequestUnregister(const ov::String &app_stream_name);

	CommonErrorCode GetOrigin(const ov::String &app_stream_name, ov::String &origin_host);

private:
	// if return false, it means that the app_stream_name is already registered from other origin server
	// app_stream_name : app/stream name (ex: app/stream)
	// origin_host : OVT provided origin host info (ex: 192.168.0.160:9000)
	bool Register(const ov::String &app_stream_name, const ov::String &origin_host);
	bool Update(const ov::String &app_stream_name, const ov::String &origin_host);
	bool Unregister(const ov::String &app_stream_name);

	bool NofifyStreamsAlive();
	bool RetryRegister();
	bool ProcessPendingUnregisters();

	enum class SetResult
	{
		Success,
		NotSet,
		Error
	};

	CommonErrorCode GetValue(const ov::String &key, ov::String &value);
	SetResult SetValue(const ov::String &key, const ov::String &value, bool only_if_absent);
	bool DeleteValue(const ov::String &key);
	bool EnsureClient();

	// Runs func(client) on the standalone or cluster client under _redis_mutex.
	// Returns false (after logging) if the client is unavailable or redis++ throws.
	template <typename Func>
	bool WithClient(const char *operation, Func &&func);

	bool AddOriginMap(const ov::String &app_stream_name, const ov::String &origin_host);
	bool AddOriginMapCandidate(const ov::String &app_stream_name, const ov::String &origin_host);
	bool DeleteOriginMap(const ov::String &app_stream_name);

	bool _redis_options_valid = false;
	bool _redis_cluster_enabled = false;
	sw::redis::ConnectionOptions _redis_connection_options;

	std::map<ov::String, ov::String> _origin_map;
	std::map<ov::String, ov::String> _origin_map_candidates;
	std::deque<ov::String> _origin_map_remove_candidates;
	std::recursive_mutex _origin_map_mutex;

	std::unique_ptr<sw::redis::Redis> _redis;
	std::unique_ptr<sw::redis::RedisCluster> _redis_cluster;
	std::mutex _redis_mutex;

	// Declared last so it is destroyed first: its thread uses the members above.
	ov::DelayQueue _update_timer{"OMapC"};
};