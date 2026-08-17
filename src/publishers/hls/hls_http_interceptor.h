//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include <modules/http/server/http_default_interceptor.h>
#include <modules/http/server/http_exchange.h>

class TsHttpInterceptor : public http::svr::DefaultInterceptor
{
protected:
	bool IsInterceptorForRequest(const std::shared_ptr<const http::svr::HttpExchange> &exchange) override
	{
		auto request = exchange->GetRequest();
		if (request->GetParsedUri() == nullptr)
		{
			loge("TsHttpInterceptor", "TsHttpInterceptor::IsInterceptorForRequest - Failed to get the parsed URI\n");
			return false;
		}
		
		auto parsed_uri = request->GetParsedUri();
		auto file = parsed_uri->File().LowerCaseString();

		if (request->GetMethod() == http::Method::Get || request->GetMethod() == http::Method::Head || request->GetMethod() == http::Method::Options)
		{
		 	// ts:*.m3u8 is the master playlist for this HLS Publisher(HLSv3)
			if (file.HasPrefix("ts:") && file.HasSuffix(".m3u8"))
			{
				return true;
			}

			if (file.HasSuffix(".m3u8") && parsed_uri->GetQueryValue("format") == "ts")
			{
				return true;
			}

			// *_hls.m3u8 is media playlist for this HLS Publisher(HLSv3)
			else if (file.HasSuffix("_hls.m3u8"))
			{
				return true;
			}

			// *_hls.ts is segment file for this HLS Publisher(HLSv3)
			else if (file.HasSuffix("_hls.ts"))
			{
				return true;
			}
			// *_hls.vtt is WebVTT segment file for this HLS Publisher(HLSv3)
			else if (file.HasSuffix("_hls.vtt"))
			{
				return true;
			}
			
		}

		return false;
	}

	bool IsCacheable() const override
	{
		return false;
	}
};
