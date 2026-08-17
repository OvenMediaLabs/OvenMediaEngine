//==============================================================================
//
//  Transcode Webhook
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================

#include "transcode_webhook.h"

#include <monitoring/monitoring.h>
#include <modules/json_serdes/application.h>
#include <modules/json_serdes/stream.h>
#include <modules/http/client/http_client.h>

#define OV_LOG_TAG "TranscodeWebhook"

TranscodeWebhook::TranscodeWebhook(const info::Application &application_info)
    : _application_info(application_info)
{
    _config = _application_info.GetConfig().GetTranscodeWebhook();
}

TranscodeWebhook::Policy TranscodeWebhook::RequestOutputProfiles(const info::Stream &input_stream_info, cfg::vhost::app::oprf::OutputProfiles &output_profiles)
{
    if (_config.IsEnabled() == false)
    {
        return Policy::UseLocalProfiles;
    }

    ov::String body;
   
    if (MakeRequestBody(input_stream_info, body) == false)
    {
        logte("Internal Error: Failed to make request body for transcode webhook for stream: %s/%s", _application_info.GetVHostAppName().CStr(), input_stream_info.GetName().CStr());
        return Policy::DeleteStream;
    }

    auto secret_key = _config.GetSecretKey();
    auto md_sha1 = ov::MessageDigest::ComputeHmac(ov::CryptoAlgorithm::Sha1, secret_key.ToData(false), body.ToData(false));
	if(md_sha1 == nullptr)
	{
		// Error
        logte("Internal Error: Signature creation failed.(Method : HMAC(SHA1), Key : %s, Body length : %zu", secret_key.CStr(), body.GetLength());
		return Policy::DeleteStream;
	}

	auto signature_sha1_base64 = ov::Base64::Encode(md_sha1, true);
    auto timeout_msec = _config.GetTimeout();
    auto client = std::make_shared<http::clnt::HttpClient>();
    client->SetMethod(http::Method::Post);
	client->SetBlockingMode(ov::BlockingMode::Blocking);
	client->SetConnectionTimeout(timeout_msec);
    client->SetRecvTimeout(timeout_msec);
	client->SetRequestHeader("X-OME-Signature", signature_sha1_base64);
	client->SetRequestHeader("Content-Type", "application/json");
	client->SetRequestHeader("Accept", "application/json");
	client->SetRequestBody(body);

    ov::StopWatch watch;
	watch.Start();

    auto control_server_url = ov::Url::Parse(_config.GetControlServerUrl());
    if (control_server_url == nullptr)
    {
        logte("Internal Error: Invalid control server url: %s", _config.GetControlServerUrl().CStr());
        return Policy::DeleteStream;
    }

    int elapsed_ms = 0;
    TranscodeWebhook::Policy policy;

    client->Request(control_server_url->ToUrlString(true), [&](http::StatusCode status_code, const std::shared_ptr<ov::Data> &data, const std::shared_ptr<const ov::Error> &error) 
	{
        elapsed_ms = watch.Elapsed();

		// A response was received from the server.
		if(error == nullptr) 
		{	
			if(status_code == http::StatusCode::OK) 
			{
				// Parsing response
				policy = ParseResponse(input_stream_info, data, output_profiles);
				return;
			} 
			else 
			{
                policy = _config.GetUseLocalProfilesOnErrorResponse() ? Policy::UseLocalProfiles : Policy::DeleteStream;
                
                logti("Control Server responded error status. HTTP code(%d) use local profiles(%s)", static_cast<uint16_t>(status_code), policy == Policy::UseLocalProfiles ? "true" : "false");
                return;
			}
		}
		else
		{
            policy = _config.GetUseLocalProfilesOnConnectionFailure() ? Policy::UseLocalProfiles : Policy::DeleteStream;
			// A connection error or an error that does not conform to the HTTP spec has occurred.
			logte("The HTTP client's request failed. error code(%d) error message(%s) use local profile(%s)", error->GetCode(), error->GetMessage().CStr(), policy == Policy::UseLocalProfiles ? "true" : "false");
			return;
		}
	});

    return policy;
}

bool TranscodeWebhook::MakeRequestBody(const info::Stream &input_stream_info, ov::String &body) const
{
    /*
    {
        "source": "tcp://211.123.11.20:2223",
        "stream": {
            "name": "stream",
            "virtualHost": "default",
            "application": "app",
            "createdTime": "2023-10-11T03:45:21.879+09:00",
            "sourceType": "Rtmp",
            "tracks": [
            {
                "id": 0,
                "type": "Video",
                "video": {
                "bitrate": "2500000",
                "bypass": false,
                "codec": "H264",
                "framerate": 30.0,
                "hasBframes": false,
                "keyFrameInterval": 30,
                "height": 720,
                "width": 1280
                }
            },
            {
                "id": 1,
                "audio": {
                "bitrate": "128000",
                "bypass": false,
                "channel": 2,
                "codec": "AAC",
                "samplerate": 48000
                },
                "type": "Audio"
            }
            ]
        }
    }
    */

    Json::Value jv_root;
    Json::Value jv_stream;

    jv_root["source"] = input_stream_info.GetMediaSource().CStr();

    // Get Stream Metric
    auto stream_metric = mon::Monitoring::GetInstance()->GetStreamMetrics(input_stream_info);
    if (stream_metric == nullptr)
    {
        return false;
    }

    jv_stream = serdes::JsonFromStream(stream_metric);

    // Additioanl Info
    jv_stream["virtualHost"] = input_stream_info.GetApplicationInfo().GetVHostAppName().GetVHostName().CStr();
    jv_stream["application"] = input_stream_info.GetApplicationInfo().GetVHostAppName().GetAppName().CStr();

    jv_root["stream"] = jv_stream;

    body = jv_root.toStyledString().c_str();

    return true;
}

TranscodeWebhook::Policy TranscodeWebhook::ParseResponse(const info::Stream &input_stream_info, const std::shared_ptr<ov::Data> &data, cfg::vhost::app::oprf::OutputProfiles &output_profiles) const
{
    ov::JsonObject object = ov::Json::Parse(data->ToString());
    if (object.IsNull() || object.IsArray())
    {
        logte("Json parsing error : a response in the wrong format was received. (expected object, but array)");
        return _config.GetUseLocalProfilesOnErrorResponse() ? Policy::UseLocalProfiles : Policy::DeleteStream;
    }

    Json::Value &jv_allowed = object.GetJsonValue()["allowed"];
    if (jv_allowed.isNull() || jv_allowed.isBool() == false)
    {
        logte("Json parsing error : In response, \"allowed\" is required data and must be of type bool.");
        return _config.GetUseLocalProfilesOnErrorResponse() ? Policy::UseLocalProfiles : Policy::DeleteStream;
    }

    auto allowed = jv_allowed.asBool();
    if (allowed == false)
    {
        logti("ControlServer disallowed creation of output profiles for stream: %s/%s", _application_info.GetVHostAppName().CStr(), input_stream_info.GetName().CStr());
        return _config.GetUseLocalProfilesOnServerDisallow() ? Policy::UseLocalProfiles : Policy::DeleteStream;
    }

    Json::Value &jv_output_profiles = object.GetJsonValue()["outputProfiles"];
    if (jv_output_profiles.isNull() || jv_output_profiles.isObject() == false)
    {
        logte("Json parsing error : In response, \"outputProfiles\" is required data and must be of type object.");
        return _config.GetUseLocalProfilesOnErrorResponse() ? Policy::UseLocalProfiles : Policy::DeleteStream;
    }

    try
    {
        serdes::OutputProfilesFromJson(jv_output_profiles, &output_profiles);
    }
    catch (const cfg::ConfigError &error)
    {
        logte("Json parsing error : %s", error.What());
        return _config.GetUseLocalProfilesOnErrorResponse() ? Policy::UseLocalProfiles : Policy::DeleteStream;
    }

    return Policy::CreateStream;
}