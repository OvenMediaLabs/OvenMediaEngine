//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#include "url.h"

#include <base/ovlibrary/converter.h>

#include <regex>

namespace ov
{
	static ov::Regex g_url_parse_regex = ov::Regex::CompiledRegex(
		// <scheme>://[id:password@]
		R"((?<scheme>.*):\/\/((?<id>.+):(?<password>.+)@)?)"
		// <host>
		R"((?<host>(\[[0-9A-Za-z:.]+\]|[^:\/?#]*)))"
		// [:<port>]
		R"((:(?<port>[0-9]+))?)"
		// [/<path/to/resource>]
		R"((?<path>\/([^\?]+)?)?)"
		// [?<query string>]
		R"((\?(?<qs>[^\?]+)?(.+)?)?)");

	ov::String Url::Encode(const ov::String &value)
	{
		static char hex_table[] = "0123456789abcdef";
		ov::String encoded_string;

		encoded_string.SetCapacity(value.GetLength() * 3);
		auto plain_buffer = value.CStr();

		while (true)
		{
			unsigned char buf = *plain_buffer;

			if (buf == '\0')
			{
				break;
			}
			else if (::isalnum(buf) || (buf == '-') || (buf == '_') || (buf == '.') || (buf == '~'))
			{
				// Append the character
				encoded_string.Append(buf);
			}
			else if (buf == ' ')
			{
				// Append '+' character if the buffer is a space
				encoded_string.Append('+');
			}
			else
			{
				// Escape
				encoded_string.Append('%');
				encoded_string.Append(hex_table[buf >> 4]);
				encoded_string.Append(hex_table[buf & 15]);
			}

			plain_buffer++;
		}

		return encoded_string;
	}

	ov::String Url::Decode(const ov::String &value)
	{
		if (value.IsEmpty())
		{
			return "";
		}

		const auto val	  = value.CStr();
		const auto length = value.GetLength();

		ov::String result_string;
		result_string.SetLength(length);
		auto result			= result_string.GetBuffer();
		size_t result_index = 0;

		char place_holder[3]{};

		for (size_t index = 0; index < length;)
		{
			char character = val[index];
			if (character == '%')
			{
				// Change '%??' to ascii character
				if ((length - index) > 2)
				{
					auto val1 = val[index + 1];
					auto val2 = val[index + 2];
					if (::isxdigit(val1) && ::isxdigit(val2))
					{
						place_holder[0]		 = val1;
						place_holder[1]		 = val2;
						result[result_index] = static_cast<char>(::strtol(place_holder, nullptr, 16));
						index += 3;
						result_index++;
						continue;
					}
				}
			}
			else if (character == '+')
			{
				// Change '+' to ' '
				character = ' ';
			}

			result[result_index] = val[index];
			index++;
			result_index++;
		}
		result_string.SetLength(result_index);
		return result_string;
	}

	bool Url::IsAbsolute(const char *url)
	{
		static ov::Regex absolute_url_regex = ov::Regex::CompiledRegex(R"(^[a-zA-Z][a-zA-Z0-9+.-]*:\/\/)");

		return absolute_url_regex.Matches(url).IsMatched();
	}

	ov::String Url::Source() const
	{
		SharedLockGuard lock(_mutex);
		return _source;
	}

	bool Url::SetSource(const ov::String &value)
	{
		LockGuard lock(_mutex);
		_source = value;
		return ParseFromSource();
	}

	ov::String Url::Scheme() const
	{
		SharedLockGuard lock(_mutex);
		return _scheme;
	}

	bool Url::SetScheme(const ov::String &value)
	{
		LockGuard lock(_mutex);
		_scheme = value;
		return UpdateSource();
	}

	ov::String Url::Host() const
	{
		SharedLockGuard lock(_mutex);
		return _host;
	}

	bool Url::SetHost(const ov::String &value)
	{
		LockGuard lock(_mutex);
		_host = value;
		return UpdateSource();
	}

	uint32_t Url::Port() const
	{
		SharedLockGuard lock(_mutex);
		return _port;
	}

	bool Url::SetPort(uint32_t port)
	{
		LockGuard lock(_mutex);
		_port = port;
		return UpdateSource();
	}

	ov::String Url::Path() const
	{
		SharedLockGuard lock(_mutex);
		return _path;
	}

	bool Url::SetPath(const ov::String &path)
	{
		LockGuard lock(_mutex);
		_path = path;
		return UpdatePathComponentsFromPath() && UpdateSource();
	}

	ov::String Url::App() const
	{
		SharedLockGuard lock(_mutex);
		return _app;
	}

	bool Url::SetApp(const ov::String &value)
	{
		LockGuard lock(_mutex);
		_app = SetPathComponent(1, value);
		return UpdatePathFromComponents() && UpdateSource();
	}

	ov::String Url::Stream() const
	{
		SharedLockGuard lock(_mutex);
		return _stream;
	}

	bool Url::SetStream(const ov::String &value)
	{
		LockGuard lock(_mutex);
		_stream = SetPathComponent(2, value);
		return UpdatePathFromComponents() && UpdateSource();
	}

	ov::String Url::File() const
	{
		SharedLockGuard lock(_mutex);
		return _file;
	}

	bool Url::SetFile(const ov::String &value)
	{
		LockGuard lock(_mutex);
		_file = SetPathComponent(3, value);
		return UpdatePathFromComponents() && UpdateSource();
	}

	ov::String Url::Id() const
	{
		SharedLockGuard lock(_mutex);
		return _id;
	}

	bool Url::SetId(const ov::String &value)
	{
		LockGuard lock(_mutex);
		_id = value;
		return UpdateSource();
	}

	ov::String Url::Password() const
	{
		SharedLockGuard lock(_mutex);
		return _password;
	}

	bool Url::SetPassword(const ov::String &value)
	{
		LockGuard lock(_mutex);
		_password = value;
		return UpdateSource();
	}

	bool Url::ParseFromSource()
	{
		auto matches = g_url_parse_regex.Matches(_source);

		if (matches.GetError() != nullptr)
		{
			// Invalid URL
			_scheme	  = "";
			_id		  = "";
			_password = "";
			_host	  = "";
			_port	  = 0;
			_path	  = "";
			_path_components.clear();
			_query_string	  = "";
			_has_query_string = false;
			ParseQuery();

			_app			  = "";
			_stream			  = "";
			_file			  = "";

			return false;
		}

		auto group_list = matches.GetNamedGroupList();

		_scheme			= group_list["scheme"].GetValue();
		_id				= group_list["id"].GetValue();
		_password		= group_list["password"].GetValue();
		_host			= group_list["host"].GetValue();
		_port			= ov::Converter::ToUInt32(group_list["port"].GetValue());
		_path			= group_list["path"].GetValue();
		// _path_components, _app, _stream, _file will be set in UpdatePathComponentsFromPath()
		if (UpdatePathComponentsFromPath() == false)
		{
			return false;
		}
		_query_string	  = group_list["qs"].GetValue();
		_has_query_string = (_query_string.IsEmpty() == false);
		ParseQuery();

		return true;
	}

	std::shared_ptr<Url> Url::Parse(const ov::String &url)
	{
		auto object = std::make_shared<Url>();

		if (object->SetSource(url))
		{
			return object;
		}

		return nullptr;
	}

	bool Url::PushBackQueryKey(const ov::String &key)
	{
		LockGuard lock(_mutex);

		if (_has_query_string)
		{
			_query_string.Append("&");
		}

		_query_string.Append(key);
		_has_query_string = true;
		ParseQuery();

		return true;
	}

	bool Url::PushBackQueryKey(const ov::String &key, const ov::String &value)
	{
		LockGuard lock(_mutex);

		if (_has_query_string)
		{
			_query_string.Append("&");
		}

		_query_string.AppendFormat("%s=%s", key.CStr(), Encode(value).CStr());
		_has_query_string = true;
		ParseQuery();

		return true;
	}

	bool Url::AppendQueryString(const ov::String &query_string)
	{
		LockGuard lock(_mutex);

		if (_has_query_string)
		{
			_query_string.Append("&");
		}

		if (query_string.HasPrefix('?') || query_string.HasPrefix('&'))
		{
			_query_string.Append(query_string.Substring(1));
		}
		else
		{
			_query_string.Append(query_string);
		}

		_has_query_string = true;
		ParseQuery();

		return true;
	}

	// Keep the order of queries.
	bool Url::RemoveQueryKey(const ov::String &remove_key)
	{
		LockGuard lock(_mutex);

		if (_has_query_string == false)
		{
			return false;
		}

		ov::String new_query_string;
		bool first_query = true;
		// Split the query string into the map
		if (_query_string.IsEmpty() == false)
		{
			const auto &query_list = _query_string.Split("&");
			for (auto &query : query_list)
			{
				auto tokens = query.Split("=", 2);
				ov::String key;
				if (tokens.size() == 2)
				{
					key = tokens[0];
				}
				else
				{
					key = query;
				}

				if (key.UpperCaseString() != remove_key.UpperCaseString())
				{
					if (first_query == true)
					{
						first_query = false;
					}
					else
					{
						new_query_string.Append("&");
					}

					new_query_string.Append(query);
				}
			}
		}

		_query_string = new_query_string;
		ParseQuery();

		return true;
	}

	const ov::String &Url::SetPathComponent(size_t index, const ov::String &value)
	{
		if (_path_components.size() <= index)
		{
			_path_components.resize(index + 1);
		}

		_path_components[index] = value;

		return value;
	}

	bool Url::UpdateSource()
	{
		_source = ToUrlStringInternal(true);
		return true;
	}

	bool Url::UpdatePathFromComponents()
	{
		_path = ov::String::Join(_path_components, "/");
		return true;
	}

	bool Url::UpdatePathComponentsFromPath()
	{
		// split <path> to /<app>/<stream>[/<file>[/<remaining>]] (Up to 5 tokens)
		_path_components = _path.Split("/");

		_app			 = "";
		_stream			 = "";
		_file			 = "";

		switch (_path_components.size())
		{
			default:
			case 4:
				_file = _path_components[3];
				[[fallthrough]];
			case 3:
				_stream = _path_components[2];
				[[fallthrough]];
			case 2:
				_app = _path_components[1];
				[[fallthrough]];
			case 1:
			case 0:
				// Nothing to do
				break;
		}

		return true;
	}

	void Url::ParseQuery()
	{
		_query_map.clear();

		if ((_has_query_string == false) || _query_string.IsEmpty())
		{
			return;
		}

		// Split the query string into the map
		const auto &query_list = _query_string.Split("&");

		for (auto &query : query_list)
		{
			auto tokens = query.Split("=", 2);

			if (tokens.size() == 2)
			{
				_query_map[tokens[0]] = Decode(tokens[1]);
			}
			else
			{
				_query_map[query] = "";
			}
		}
	}

	bool Url::HasQueryString() const
	{
		SharedLockGuard lock(_mutex);
		return _has_query_string;
	}

	ov::String Url::Query() const
	{
		SharedLockGuard lock(_mutex);
		return _query_string;
	}

	bool Url::HasQueryKey(ov::String key) const
	{
		SharedLockGuard lock(_mutex);
		return _query_map.find(key) != _query_map.end();
	}

	ov::String Url::GetQueryValue(ov::String key) const
	{
		SharedLockGuard lock(_mutex);

		auto item = _query_map.find(key);
		if (item == _query_map.end())
		{
			return "";
		}

		return Decode(item->second);
	}

	Url &Url::operator=(const Url &other) noexcept
	{
		if (this == &other)
		{
			return *this;
		}

		// Snapshot `other` under its shared lock, then assign into `this` under its exclusive
		// lock. The two locks are never held at the same time, so cross-assignment cannot
		// deadlock and no lock ordering is required.
		ov::String source, scheme, id, password, host, path, query_string, app, stream, file;
		uint32_t port = 0;
		bool has_query_string = false;
		std::vector<ov::String> path_components;
		std::map<ov::String, ov::String> query_map;

		{
			SharedLockGuard lock(other._mutex);

			source			 = other._source;
			scheme			 = other._scheme;
			id				 = other._id;
			password		 = other._password;
			host			 = other._host;
			port			 = other._port;
			path			 = other._path;
			path_components	 = other._path_components;
			query_string	 = other._query_string;
			has_query_string = other._has_query_string;
			query_map		 = other._query_map;
			app				 = other._app;
			stream			 = other._stream;
			file			 = other._file;
		}

		{
			LockGuard lock(_mutex);

			_source			  = source;
			_scheme			  = scheme;
			_id				  = id;
			_password		  = password;
			_host			  = host;
			_port			  = port;
			_path			  = path;
			_path_components  = path_components;
			_query_string	  = query_string;
			_has_query_string = has_query_string;
			_query_map		  = query_map;
			_app			  = app;
			_stream			  = stream;
			_file			  = file;
		}

		return *this;
	}

	Url::Url(const Url &other)
	{
		operator=(other);
	}

	std::shared_ptr<Url> Url::Clone() const
	{
		return std::make_shared<Url>(*this);
	}

	void Url::Print() const
	{
		SharedLockGuard lock(_mutex);

		logi("URL Parser", "%s %s %d %s %s %s %s",
			 _scheme.CStr(), _host.CStr(), _port,
			 _app.CStr(), _stream.CStr(), _file.CStr(), _query_string.CStr());
	}

	ov::String Url::ToUrlString(bool include_query_string) const
	{
		SharedLockGuard lock(_mutex);
		return ToUrlStringInternal(include_query_string);
	}

	ov::String Url::ToUrlStringInternal(bool include_query_string) const
	{
		ov::String url;

		url.AppendFormat("%s://", _scheme.CStr());

		if (_id.IsEmpty() == false)
		{
			url.Append(_id.CStr());

			if (_password.IsEmpty())
			{
				url.Append('@');
			}
		}

		if (_password.IsEmpty() == false)
		{
			url.AppendFormat(":%s@", _password.CStr());
		}

		url.Append(_host.CStr());
		if (_port > 0)
		{
			url.AppendFormat(":%d", _port);
		}

		url.Append(_path);

		if (include_query_string && (_query_string.IsEmpty() == false))
		{
			url.Append("?");
			url.Append(_query_string);
		}

		return url;
	}

	ov::String Url::ToString() const
	{
		SharedLockGuard lock(_mutex);

		auto url = ToUrlStringInternal(true);

		url.AppendFormat(" (app: %s, stream: %s, file: %s)", _app.CStr(), _stream.CStr(), _file.CStr());

		return url;
	}
}  // namespace ov
