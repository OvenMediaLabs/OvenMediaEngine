//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <modules/http/http_datastructure.h>

namespace api
{
	class MultipleStatus
	{
	public:
		void AddStatusCode(http::StatusCode status_code);
		void AddStatusCode(const std::shared_ptr<const ov::Error> &error);
		http::StatusCode GetStatusCode() const;

		bool HasOK() const
		{
			return _has_ok;
		}

	protected:
		int _count = 0;
		http::StatusCode _last_status_code = http::StatusCode::OK;

		bool _has_ok = true;
	};
}  // namespace api
