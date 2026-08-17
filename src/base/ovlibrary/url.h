//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "./ovlibrary.h"
#include "./string.h"
#include "./tsa/mutex.h"

// TODO(dimiden): Since `ov::Url` is mostly parsed once at initialization and then values are only read, protecting every field with a mutex may be an unnecessary overhead.
// Therefore, it would be better to make `ov::Url` immutable and return a new instance with `WithX()` methods when modification is needed.
namespace ov
{
	// Url is fully synchronized: a `SharedMutex` (`_mutex`) guards every field.
	// Read accessors take a shared lock and return BY VALUE (a snapshot);
	// mutators take an exclusive lock.
	// Concurrent reads and writes on a shared Url are therefore safe,
	// at the cost of a lock (and a copy of the returned value) per access.
	//
	// The parsed query map is cached lazily behind its own `_query_map_mutex`
	// (rebuilt from `_query_string` on first query read), so read accessors that
	// hold `_mutex` only in shared mode can still populate it without racing.
	// Lock order is always `_mutex` (outer) -> `_query_map_mutex` (inner).
	class Url
	{
	public:
		static ov::String Encode(const ov::String &value);
		static ov::String Decode(const ov::String &value);

		// <scheme>://<host>[:<port>][/<path/to/resource>][?<query string>]
		static std::shared_ptr<Url> Parse(const ov::String &url);

		// Checks if the given URL is absolute URL
		//
		// Absolute URL: <scheme>://<host>[:<port>][/<path/to/resource>][?<query string>]
		// Relative URL: <path/to/resource>[?<query string>]
		//
		// Example of absolute URL: http://airensoft.com/path/to/resource
		// Example of relative URL: /path/to/resource or path/to/resource
		static bool IsAbsolute(const char *url);

		// Getters return a snapshot taken under a shared lock; Setters mutate under an exclusive lock.
		ov::String Source() const;
		bool SetSource(const ov::String &value);

		ov::String Scheme() const;
		bool SetScheme(const ov::String &value);

		ov::String Host() const;
		bool SetHost(const ov::String &value);

		uint32_t Port() const;
		bool SetPort(uint32_t port);

		ov::String Path() const;
		bool SetPath(const ov::String &value);

		ov::String App() const;
		bool SetApp(const ov::String &value);

		ov::String Stream() const;
		bool SetStream(const ov::String &value);

		ov::String File() const;
		bool SetFile(const ov::String &value);

		ov::String Id() const;
		bool SetId(const ov::String &value);

		ov::String Password() const;
		bool SetPassword(const ov::String &value);

		bool HasQueryString() const;
		ov::String Query() const;
		bool HasQueryKey(ov::String key) const;
		ov::String GetQueryValue(ov::String key) const;
		bool PushBackQueryKey(const ov::String &key, const ov::String &value);
		bool PushBackQueryKey(const ov::String &key);
		bool AppendQueryString(const ov::String &query_string);
		bool RemoveQueryKey(const ov::String &key);

		void Print() const;
		ov::String ToUrlString(bool include_query_string = true) const;
		ov::String ToString() const;

		Url &operator=(const Url &other);

		Url() = default;
		Url(const Url &other);

		std::shared_ptr<Url> Clone() const;

	private:
		// Mutating helpers require the caller's exclusive lock; read-only helpers require at least shared.
		bool ParseFromSource() OV_REQUIRES(_mutex);
		// Since _path is in the form of /app/stream/file, the index of `app` should be 1
		const ov::String &SetPathComponent(size_t index, const ov::String &value) OV_REQUIRES(_mutex);
		bool UpdateSource() OV_REQUIRES(_mutex);
		// Update _path from _path_components
		bool UpdatePathFromComponents() OV_REQUIRES(_mutex);
		// Update _path_components/_app/_stream/_file from _path
		bool UpdatePathComponentsFromPath() OV_REQUIRES(_mutex);
		// Lock-free body of ToUrlString(); the public method takes the shared lock.
		ov::String ToUrlStringInternal(bool include_query_string) const OV_REQUIRES_SHARED(_mutex);

		// Query-map cache helpers (lock order: `_mutex` -> `_query_map_mutex`).
		// Marks the cached query map stale; called by mutators that change `_query_string`.
		void InvalidateQueryMap() const OV_REQUIRES(_mutex);
		// Rebuilds the cached query map from `_query_string` if stale (lazy parse).
		void EnsureQueryParsed() const OV_REQUIRES_SHARED(_mutex) OV_REQUIRES(_query_map_mutex);

	private:
		mutable SharedMutex _mutex;

		// Full URL
		ov::String _source OV_GUARDED_BY(_mutex);

		ov::String _scheme OV_GUARDED_BY(_mutex);
		ov::String _id OV_GUARDED_BY(_mutex);
		ov::String _password OV_GUARDED_BY(_mutex);
		ov::String _host OV_GUARDED_BY(_mutex);
		uint32_t _port OV_GUARDED_BY(_mutex) = 0;
		ov::String _path OV_GUARDED_BY(_mutex);
		// Path tokens (separated by '/')
		std::vector<ov::String> _path_components OV_GUARDED_BY(_mutex);
		ov::String _query_string OV_GUARDED_BY(_mutex);
		bool _has_query_string OV_GUARDED_BY(_mutex) = false;

		// `_query_map` is parsed lazily from `_query_string` on first query read and cached.
		// It has its OWN mutex so read accessors (which hold `_mutex` only in shared mode)
		// can populate the cache without racing each other.
		// Lock order is always `_mutex` (outer) -> `_query_map_mutex` (inner).
		mutable Mutex _query_map_mutex;
		mutable bool _query_parsed OV_GUARDED_BY(_query_map_mutex) = false;
		mutable std::map<ov::String, ov::String> _query_map OV_GUARDED_BY(_query_map_mutex);

		// Valid for URLs of the form: <scheme>://<domain>[:<port>]/<app>/<stream>[<file>[/<remaining>]][?<query string>]
		ov::String _app OV_GUARDED_BY(_mutex);
		ov::String _stream OV_GUARDED_BY(_mutex);
		ov::String _file OV_GUARDED_BY(_mutex);
	};
}  // namespace ov
