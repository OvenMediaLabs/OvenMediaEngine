//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

#include <functional>
#include <memory>
#include <unordered_map>

#include "../http_datastructure.h"

namespace http::clnt
{
	using ResponseHandler = std::function<void(StatusCode status_code, const std::shared_ptr<ov::Data> &data, const std::shared_ptr<const ov::Error> &error)>;

	class CancelToken
	{
	public:
		// TODO: Implement cancel token
		void Cancel()
		{
		}

		// TODO: Implement cancel token
		void Cancel(std::shared_ptr<const ov::Error> error)
		{
		}
	};
}  // namespace http::clnt
