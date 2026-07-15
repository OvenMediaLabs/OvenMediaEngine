//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "http_parser.h"

namespace http
{
	namespace prot
	{
		namespace h1
		{
			class HttpRequestHeaderParser : public HttpParser
			{
			public:
				const ov::String &GetRequestTarget() const noexcept
				{
					return _request_target;
				}

			protected:
				// Parse Request Line
				StatusCode ParseFirstLine(const ov::String &line) override;

				// Information parsed when HTTP request
				ov::String _request_target;
			};
		}  // namespace h1
	}	   // namespace prot
}  // namespace http
