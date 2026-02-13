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

namespace ov
{
	// Url is immutable class
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

		// Getters and Setters (Setters are NOT THREAD-SAFE)
		OV_DEFINE_CONST_GETTER(Source, _source)
		bool SetSource(const ov::String &value);

		OV_DEFINE_CONST_GETTER(Scheme, _scheme)
		bool SetScheme(const ov::String &value);

		OV_DEFINE_CONST_GETTER(Host, _host)
		bool SetHost(const ov::String &value);

		OV_DEFINE_CONST_GETTER(Port, _port)
		bool SetPort(uint32_t port);

		OV_DEFINE_CONST_GETTER(Path, _path)
		bool SetPath(const ov::String &value);

		OV_DEFINE_CONST_GETTER(App, _app)
		bool SetApp(const ov::String &value);

		OV_DEFINE_CONST_GETTER(Stream, _stream)
		bool SetStream(const ov::String &value);

		OV_DEFINE_CONST_GETTER(File, _file)
		bool SetFile(const ov::String &value);

		OV_DEFINE_CONST_GETTER(Id, _id)
		bool SetId(const ov::String &value);

		OV_DEFINE_CONST_GETTER(Password, _password)
		bool SetPassword(const ov::String &value);

		bool HasQueryString() const;
		const ov::String &Query() const;
		const bool HasQueryKey(ov::String key) const;
		const ov::String GetQueryValue(ov::String key) const;
		const std::map<ov::String, ov::String> &QueryMap() const;
		bool PushBackQueryKey(const ov::String &key, const ov::String &value);
		bool PushBackQueryKey(const ov::String &key);
		bool AppendQueryString(const ov::String &query_string);
		bool RemoveQueryKey(const ov::String &key);

		void Print() const;
		ov::String ToUrlString(bool include_query_string = true) const;
		ov::String ToString() const;

		Url &operator=(const Url &other) noexcept;

		Url() = default;
		Url(const Url &other);

		std::shared_ptr<Url> Clone() const;

	protected:
		bool ParseFromSource();
		// Since _path is in the form of /app/stream/file, the index of `app` should be 1
		const ov::String &SetPathComponent(size_t index, const ov::String &value);
		bool UpdateSource();
		// Update _path from _path_components
		bool UpdatePathFromComponents();
		// Update _path_components/_app/_stream/_file from _path
		bool UpdatePathComponentsFromPath();

	private:
		void ParseQueryIfNeeded() const;

	private:
		// Full URL
		ov::String _source;

		ov::String _scheme;
		ov::String _id;
		ov::String _password;
		ov::String _host;
		uint32_t _port = 0;
		ov::String _path;
		// Path tokens (separated by '/')
		std::vector<ov::String> _path_components;
		ov::String _query_string;
		bool _has_query_string = false;
		// To reduce the cost of parsing the query map, parsing the query only when Query() or QueryMap() is called
		mutable bool _query_parsed = false;
		mutable std::mutex _query_map_mutex;
		mutable std::map<ov::String, ov::String> _query_map;

		// Valid for URLs of the form: <scheme>://<domain>[:<port>]/<app>/<stream>[<file>[/<remaining>]][?<query string>]
		ov::String _app;
		ov::String _stream;
		ov::String _file;
	};
}  // namespace ov