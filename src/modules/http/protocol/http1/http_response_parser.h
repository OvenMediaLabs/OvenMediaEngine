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
			class HttpResponseParser : public HttpParser
			{
			public:
				StatusCode GetStatusCode() const
				{
					return _status_code;
				}

			protected:
				// Parse Status Line
				StatusCode ParseFirstLine(const ov::String &line) override;

				StatusCode _status_code = StatusCode::Unknown;
				ov::String _reason_phrase;
			};
		}  // namespace h1
	}	   // namespace prot
}  // namespace http
