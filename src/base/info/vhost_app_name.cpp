//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "vhost_app_name.h"

#include "application_private.h"

namespace info
{
	VHostAppName::VHostAppName()
		: _name_path(NamePath::UnknownNamePath())
	{
	}

	VHostAppName::VHostAppName(const ov::String &vhost_name, const ov::String &app_name)
		: _is_valid(true),

		  _vhost_name(vhost_name),
		  _app_name(app_name)
	{
		UpdateNamePath();
	}

	VHostAppName::VHostAppName(const ov::String &vhost_app_name)
		: _name_path(vhost_app_name)
	{
		auto tokens = vhost_app_name.Split("#");

		if (tokens.size() == 3)
		{
			_vhost_name = tokens[1];
			_app_name = tokens[2];
			_is_valid = true;
		}
		else
		{
			_is_valid = false;
		}
	}

	VHostAppName VHostAppName::InvalidVHostAppName()
	{
		static VHostAppName vhost_app_name;
		return vhost_app_name;
	}

	bool VHostAppName::IsValid() const
	{
		return _is_valid;
	}

	bool VHostAppName::operator==(const VHostAppName &another) const
	{
		return (_is_valid == another._is_valid) && (_name_path == another._name_path);
	}

	bool VHostAppName::operator<(const VHostAppName &another) const
	{
		return _name_path < another._name_path;
	}

	const ov::String &VHostAppName::GetVHostName() const
	{
		return _vhost_name;
	}

	const ov::String &VHostAppName::GetAppName() const
	{
		return _app_name;
	}

	const NamePath &VHostAppName::GetNamePath() const
	{
		return _name_path;
	}

	void VHostAppName::UpdateNamePath()
	{
		_name_path.Update("#%s#%s",
						  _vhost_name.Replace("#", "_").CStr(),
						  _app_name.Replace("#", "_").CStr());
	}

	const ov::String &VHostAppName::ToString() const
	{
		return _name_path.ToString();
	}

	const char *VHostAppName::CStr() const
	{
		return ToString().CStr();
	}
}  // namespace info
