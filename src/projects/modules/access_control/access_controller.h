//==============================================================================
//
//  Authenticate Base Class
//
//  Created by Getroot
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/common_types.h>
#include <base/info/host.h>

#include "admission_webhooks/admission_webhooks.h"
#include "request_info.h"
#include "signed_policy/signed_policy.h"

class AccessController
{
public:
	enum class VerificationResult
	{
		Error = -2,	 // Unexpected error
		Fail  = -1,	 // Verified but fail
		Off	  = 0,	 // configuration is off
		Pass  = 1	 // Check signature and pass
	};

	// This is called only when the API of the server set in AdmissionWebhooks is called.
	// In other words, it is not called if AdmissionWebhooks is not set or if any other error occurs first.
	using AdmissionWebhookInvokeResult = std::function<void(
		const ov::String &control_server_url,
		const std::shared_ptr<const ov::SocketAddress> &client_address,
		const std::shared_ptr<const AdmissionWebhooks> &admission_webhooks)>;

public:
	AccessController(ProviderType provider_type, const cfg::Server &server_config);
	AccessController(PublisherType publisher_type, const cfg::Server &server_config);

	// Verify the SignedPolicy based on the `host_info` provided from outside, such as vhost or host.
	std::tuple<VerificationResult, std::shared_ptr<const SignedPolicy>> VerifyBySignedPolicy(const info::Host &host_info, const std::shared_ptr<const ac::RequestInfo> &request_info);
	// Verify the SignedPolicy based on the `host_info` obtained from the `request_info`'s host part, treating it as a domain.
	std::tuple<VerificationResult, std::shared_ptr<const SignedPolicy>> VerifyBySignedPolicy(const std::shared_ptr<const ac::RequestInfo> &request_info);

	// Verify the AdmissionWebhooks based on the `host_info` provided from outside, such as vhost or host.
	std::tuple<VerificationResult, std::shared_ptr<const AdmissionWebhooks>> VerifyByWebhooks(const info::Host &host_info, const std::shared_ptr<const ac::RequestInfo> &request_info);
	// Verify the AdmissionWebhooks based on the `request_info`'s host part, treating it as a domain.
	std::tuple<VerificationResult, std::shared_ptr<const AdmissionWebhooks>> VerifyByWebhooks(const std::shared_ptr<const ac::RequestInfo> &request_info);

	// Send close webhooks to the control server based on the `host_info` provided from outside, such as vhost or host.
	std::tuple<VerificationResult, std::shared_ptr<const AdmissionWebhooks>> SendCloseWebhooks(const info::Host &host_info, const std::shared_ptr<const ac::RequestInfo> &request_info);
	// Send close webhooks to the control server based on the `request_info`'s host part, treating it as a domain.
	std::tuple<VerificationResult, std::shared_ptr<const AdmissionWebhooks>> SendCloseWebhooks(const std::shared_ptr<const ac::RequestInfo> &request_info);

protected:
	std::optional<info::Host> GetHostInfo(const std::shared_ptr<const ac::RequestInfo> &request_info);

	std::tuple<VerificationResult, std::shared_ptr<const AdmissionWebhooks>> InvokeAdmissionWebhook(
		const info::Host &host_info,
		const std::shared_ptr<const ac::RequestInfo> &request_info,
		std::optional<int> timeout_msec,
		AdmissionWebhooks::Status::Code status,
		AdmissionWebhookInvokeResult result_callback);

private:
	const ProviderType _provider_type;
	const PublisherType _publisher_type;
	const cfg::Server _server_config;
};
