//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

namespace info
{
	class OmeVersion : public ov::Singleton<OmeVersion>
	{
	public:
		void SetVersion(const ov::String &version, const ov::String &git_extra);

		ov::String GetVersion() const
		{
			return _version;
		}

		ov::String GetGitVersion() const
		{
			return _git_extra;
		}

		ov::String ToString() const
		{
			return _description;
		}

	protected:
		ov::String _version;
		ov::String _git_extra;

		ov::String _description;
	};
}  // namespace info
