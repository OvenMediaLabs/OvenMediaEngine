//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#include "cors_manager.h"

#include "../http_private.h"

#define OV_LOG_TAG OV_LOG_TAG_PREFIX ".CORS"

namespace http
{
	void CorsManager::SetDefaultCrossDomains(const cfg::cmn::CrossDomains &cross_domain_cfg)
	{
		SetCrossDomains(info::VHostAppName::InvalidVHostAppName(), cross_domain_cfg);
	}

	void CorsManager::SetCrossDomains(const info::VHostAppName &vhost_app_name, const cfg::cmn::CrossDomains &cross_domain_cfg)
	{
		std::lock_guard lock_guard(_cors_mutex);

		auto url_list = cross_domain_cfg.GetUrls();
		_cors_cfg_map[vhost_app_name] = cross_domain_cfg;
		auto &cors_policy = _cors_policy_map[vhost_app_name];
		auto &cors_regex_list = _cors_item_list_map[vhost_app_name];
		ov::String cors_rtmp;

		auto cors_domains_for_rtmp = std::vector<ov::String>();

		cors_regex_list.clear();
		cors_rtmp = "";

		if (url_list.size() == 0)
		{
			cors_policy = CorsPolicy::Empty;
			return;
		}

		cors_policy = CorsPolicy::Origin;

		for (auto url : url_list)
		{
			if (url == "*")
			{
				cors_regex_list.clear();

				const auto regex = ov::Regex::CompiledRegex(ov::Regex::WildCardRegex("*"));
				cors_regex_list.emplace_back(url, regex);

				cors_policy = CorsPolicy::All;

				if (url_list.size() > 1)
				{
					// Ignore other items if "*" is specified
					logtw("Invalid CORS settings found for %s: '*' cannot be used with other CORS patterns. Other CORS patterns are ignored.", vhost_app_name.CStr());
				}

				break;
			}
			else if (url == "null")
			{
				if (url_list.size() > 1)
				{
					logtw("Invalid CORS settings found for %s: 'null' cannot be used with other CORS patterns. 'null' is ignored.", vhost_app_name.CStr());
				}
				else
				{
					cors_policy = CorsPolicy::Null;
				}

				continue;
			}

			auto regex = ov::Regex::WildCardRegex(url, false);

			if (url.IndexOf("://") < 0)
			{
				// Scheme doesn't exists in the URL - allow http:// and https:// for a request URL
				regex.Prepend(R"((http.?:\/\/)?)");
			}

			regex.Prepend("^");
			regex.Append("$");

			cors_regex_list.emplace_back(url, ov::Regex::CompiledRegex(regex));
			cors_domains_for_rtmp.push_back(url);
		}

		// Create crossdomain.xml for RTMP - OME does not support RTMP playback, so it is not used.
		std::ostringstream cross_domain_xml;

		cross_domain_xml
			<< R"(<?xml version="1.0"?>)" << std::endl
			<< R"(<!DOCTYPE cross-domain-policy SYSTEM "http://www.adobe.com/xml/dtds/cross-domain-policy.dtd">)" << std::endl
			<< R"(<cross-domain-policy>)" << std::endl;

		if (cors_policy == CorsPolicy::All)
		{
			cross_domain_xml
				<< R"(	<site-control permitted-cross-domain-policies="all" />)" << std::endl
				<< R"(	<allow-access-from domain="*" secure="false" />)" << std::endl
				<< R"(	<allow-http-request-headers-from domain="*" headers="*" secure="false"/>)" << std::endl;
		}
		else
		{
			for (auto &cors_url : cors_domains_for_rtmp)
			{
				cross_domain_xml
					<< R"(	<allow-access-from domain=")" << cors_url.CStr() << R"(" />)" << std::endl;
			}
		}

		cross_domain_xml
			<< R"(</cross-domain-policy>)";

		cors_rtmp = cross_domain_xml.str().c_str();

		if (_cors_rtmp.IsEmpty())
		{
			_cors_rtmp = cors_rtmp;
		}
		else
		{
			if (cors_rtmp != _cors_rtmp)
			{
				logtw("Different CORS settings found for RTMP: crossdomain.xml must be located / and cannot be declared per app");
				logtw("This CORS settings will be used\n%s", _cors_rtmp.CStr());
			}
		}
	}

	bool CorsManager::SetupRtmpCorsXml(const std::shared_ptr<http::svr::HttpResponse> &response) const
	{
		std::lock_guard lock_guard(_cors_mutex);

		if (_cors_rtmp.IsEmpty() == false)
		{
			response->SetHeader("Content-Type", "text/x-cross-domain-policy");
			response->AppendString(_cors_rtmp);
		}

		return true;
	}

	bool CorsManager::SetupDefaultHttpCorsHeader(
		const std::shared_ptr<const http::svr::HttpRequest> &request, const std::shared_ptr<http::svr::HttpResponse> &response,
		const std::vector<http::Method> &allowed_methods) const
	{
		return SetupHttpCorsHeader(info::VHostAppName::InvalidVHostAppName(), request, response, allowed_methods);
	}

	bool CorsManager::SetupHttpCorsHeader(
		const info::VHostAppName &vhost_app_name,
		const std::shared_ptr<const http::svr::HttpRequest> &request, const std::shared_ptr<http::svr::HttpResponse> &response,
		const std::vector<http::Method> &allowed_methods) const
	{
		// Set Access-Control-Allow-Methods even if vhost_app_name doesn't exist
		std::vector<ov::String> method_list;
		for (const auto &method : allowed_methods)
		{
			method_list.push_back(http::StringFromMethod(method));
		}

		if (method_list.empty() == false)
		{
			response->SetHeader("Access-Control-Allow-Methods", ov::String::Join(method_list, ", "));
		}

		ov::String origin_header = request->GetHeader("ORIGIN");
		ov::String cors_header = "";
		bool found_origin = false;
		std::unordered_map<info::VHostAppName, cfg::cmn::CrossDomains>::const_iterator cors_cfg_iterator;

		{
			std::lock_guard lock_guard(_cors_mutex);

			auto cors_policy_iterator = _cors_policy_map.find(vhost_app_name);
			auto cors_regex_list_iterator = _cors_item_list_map.find(vhost_app_name);
			cors_cfg_iterator = _cors_cfg_map.find(vhost_app_name);
			
			if (
				(cors_policy_iterator == _cors_policy_map.end()) ||
				(cors_regex_list_iterator == _cors_item_list_map.end()) ||
				(cors_cfg_iterator == _cors_cfg_map.end()))
			{
				// This happens in the following situations:
				//
				// 1) Request to an application that doesn't exist
				// 2) Request while the application is being created
				return false;
			}

			const auto &cors_policy = cors_policy_iterator->second;
			switch (cors_policy)
			{
				case CorsPolicy::Empty:
					// Nothing to do
					break;

				case CorsPolicy::All:
					cors_header = "*";
					found_origin = true;
					break;

				case CorsPolicy::Null:
					cors_header = "null";
					found_origin = true;
					break;

				case CorsPolicy::Origin: {
					const auto &cors_item_list = cors_regex_list_iterator->second;

					std::find_if(
						cors_item_list.cbegin(), cors_item_list.cend(),
						[&origin_header, &found_origin](const auto &cors_item) -> bool {
							const auto result = cors_item.IsMatches(origin_header);

							logtt("Checking CORS for origin header [%s] with config [%s] (%s): %s",
								  origin_header.CStr(),
								  cors_item.regex.GetPattern().CStr(), cors_item.url.CStr(),
								  result ? "MATCHED" : "not matched");
							
							if (result == true)
							{
								found_origin = true;
							}
							return result;
						});

					cors_header = origin_header;
				}
			}
		}

		const auto &cors_cfg = cors_cfg_iterator->second;
		if (found_origin == false)
		{
			response->UnsetHeader("Access-Control-Allow-Origin");
			response->UnsetHeader("Access-Control-Allow-Credentials");
			response->UnsetHeader("Access-Control-Allow-Methods");
			response->UnsetHeader("Access-Control-Allow-Headers");
		}
		else
		{
			response->SetHeader("Access-Control-Allow-Origin", cors_header);

			// Access-Control-Allow-Credentials
			auto custom_header_opt = cors_cfg.GetCustomHeader("Access-Control-Allow-Credentials");
			if (custom_header_opt.has_value())
			{
				response->SetHeader(std::get<0>(custom_header_opt.value()), std::get<1>(custom_header_opt.value()));
			}
			else
			{
				response->SetHeader("Access-Control-Allow-Credentials", "true");
			}

			// Access-Control-Allow-Methods
			custom_header_opt = cors_cfg.GetCustomHeader("Access-Control-Allow-Methods");
			if (custom_header_opt.has_value())
			{
				response->SetHeader(std::get<0>(custom_header_opt.value()), std::get<1>(custom_header_opt.value()));
			}
			else 
			{
				// Already set above
				// response->SetHeader("Access-Control-Allow-Methods", ov::String::Join(method_list, ", "));
			}

			// Access-Control-Allow-Headers
			custom_header_opt = cors_cfg.GetCustomHeader("Access-Control-Allow-Headers");
			if (custom_header_opt.has_value())
			{
				response->SetHeader(std::get<0>(custom_header_opt.value()), std::get<1>(custom_header_opt.value()));
			}
			else
			{
				auto request_allow_headers = request->GetHeader("Access-Control-Request-Headers");
				if (request_allow_headers.IsEmpty() == false)
				{
					response->SetHeader("Access-Control-Allow-Headers", request_allow_headers);
				}
			}
		}

		// Remaining custom headers
		auto custom_headers = cors_cfg.GetCustomHeaders();
		for (const auto &item : custom_headers)
		{
			auto key = item.GetKey();
			if (key.LowerCaseString() == "access-control-allow-origin" ||
				key.LowerCaseString() == "access-control-allow-credentials" ||
				key.LowerCaseString() == "access-control-allow-methods" ||
				key.LowerCaseString() == "access-control-allow-headers")
			{
				continue;
			}

			response->SetHeader(key, item.GetValue());
		}

		return true;
	}
}  // namespace http
