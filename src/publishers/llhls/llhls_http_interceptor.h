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

class LLHlsHttpInterceptor : public http::svr::DefaultInterceptor
{
protected:
	bool IsInterceptorForRequest(const std::shared_ptr<const http::svr::HttpExchange> &exchange) override
	{
		auto request = exchange->GetRequest();
		if (request->GetParsedUri() == nullptr)
		{
			loge("LLHlsHttpInterceptor", "LLHlsHttpInterceptor::IsInterceptorForRequest - Failed to get the parsed URI\n");
			return false;
		}

		auto parsed_uri = request->GetParsedUri();
		auto file = parsed_uri->File().LowerCaseString();

		if (request->GetMethod() == http::Method::Get || request->GetMethod() == http::Method::Head || request->GetMethod() == http::Method::Options)
		{
			// ts:*.m3u8 is the master playlist for this HLSv3 Publisher(HLSv3)
			if (file.HasPrefix("ts:") && file.HasSuffix(".m3u8"))
			{
				return false;
			}

			if (file.HasSuffix(".m3u8") && parsed_uri->GetQueryValue("format") == "ts")
			{
				return false;
			}

			// *hls.m3u8
			// *hls.ts is for this HLSv3 Publisher(HLSv3)
			else if (file.HasSuffix("_hls.m3u8"))
			{
				return false;
			}
			
			// Every other m3u8 file is for LL-HLS
			else if (file.HasSuffix(".m3u8"))
			{
				return true;
			}

			// *_llhls.m4s is for LL-HLS
			else if (file.HasSuffix("_llhls.m4s") || file.HasSuffix("_llhls.vtt"))
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
