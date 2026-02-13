//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <config/config.h>

#include "./name_path.h"

namespace info
{
	/// VHostAppName is a name that consists of the same form as "#vhost#app_name"
	/// This is a combination of the VHost name and app_name.
	/// VHostAppName is immutable object
	class VHostAppName
	{
		friend class Application;

	public:
		VHostAppName(const ov::String &vhost_name, const ov::String &app_name);
		VHostAppName(const ov::String &vhost_app_name);
		
		static VHostAppName InvalidVHostAppName();

		bool IsValid() const;
		bool operator==(const VHostAppName &another) const;
		bool operator<(const VHostAppName &another) const;

		const ov::String &GetVHostName() const;
		const ov::String &GetAppName() const;

		const NamePath &GetNamePath() const;

		const ov::String &ToString() const;
		const char *CStr() const;

		std::size_t Hash() const
		{
			return std::hash<bool>()(_is_valid) ^
				   _name_path.Hash();
		}

	protected:
		VHostAppName();

		void UpdateNamePath();

		bool _is_valid = false;

		NamePath _name_path;

		ov::String _vhost_name;
		ov::String _app_name;
	};
}  // namespace info

namespace std
{
	template <>
	struct hash<info::VHostAppName>
	{
		std::size_t operator()(info::VHostAppName const &vhost_app_name) const
		{
			return vhost_app_name.Hash();
		}
	};
}  // namespace std
