//==============================================================================
//
//  Transcode Webhook
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <base/info/application.h>
#include <base/info/stream.h>
#include <config/items/virtual_hosts/applications/transcode_webhook/transcode_webhook.h>

class TranscodeWebhook
{
public:
    enum class Policy
    {
        DeleteStream = 0,
        CreateStream = 1,
        UseLocalProfiles = 2
    };

    TranscodeWebhook(const info::Application &application_info);

    Policy RequestOutputProfiles(const info::Stream &input_stream_info, cfg::vhost::app::oprf::OutputProfiles &output_profiles);

private:
    bool MakeRequestBody(const info::Stream &input_stream_info, ov::String &body) const;
    Policy ParseResponse(const info::Stream &input_stream_info, const std::shared_ptr<ov::Data> &data, cfg::vhost::app::oprf::OutputProfiles &output_profiles) const;

    const info::Application _application_info;
    cfg::vhost::app::trwh::TranscodeWebhook _config;
};