//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./multiple_status.h"

namespace api
{
	void MultipleStatus::AddStatusCode(http::StatusCode status_code)
	{
		if (_count == 0)
		{
			_last_status_code = status_code;
			_has_ok = (status_code == http::StatusCode::OK);
		}
		else
		{
			_has_ok = _has_ok || (status_code == http::StatusCode::OK);

			if (_last_status_code != status_code)
			{
				_last_status_code = http::StatusCode::MultiStatus;
			}
			else
			{
				// Keep the status code
			}
		}

		_count++;
	}

	void MultipleStatus::AddStatusCode(const std::shared_ptr<const ov::Error> &error)
	{
		http::StatusCode error_status_code = static_cast<http::StatusCode>(error->GetCode());

		AddStatusCode(IsValidStatusCode(error_status_code) ? error_status_code : http::StatusCode::InternalServerError);
	}

	http::StatusCode MultipleStatus::GetStatusCode() const
	{
		return _last_status_code;
	}
}  // namespace api
