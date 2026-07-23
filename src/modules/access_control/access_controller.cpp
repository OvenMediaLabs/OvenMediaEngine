#include "access_controller.h"

#include <orchestrator/orchestrator.h>

#define OV_LOG_TAG "AccessController"

AccessController::AccessController(ProviderType provider_type, const cfg::Server &server_config)
	: _provider_type(provider_type), _publisher_type(PublisherType::Unknown), _server_config(server_config)
{
}

AccessController::AccessController(PublisherType publisher_type, const cfg::Server &server_config)
	: _provider_type(ProviderType::Unknown), _publisher_type(publisher_type), _server_config(server_config)
{
}

std::optional<info::Host> AccessController::GetHostInfo(const std::shared_ptr<const ac::RequestInfo> &request_info)
{
	auto orchestrator = ocst::Orchestrator::GetInstance();
	auto request_url  = request_info->GetRequestedUrl();
	auto vhost_name	  = orchestrator->GetVhostNameFromDomain(request_url->Host());

	if (vhost_name.IsEmpty())
	{
		logte("Could not resolve the domain: %s", request_url->Host().CStr());
		return std::nullopt;
	}

	auto host_info_item = orchestrator->GetHostInfo(vhost_name);
	if (host_info_item.has_value() == false)
	{
		logte("Could not find VirtualHost (%s)", vhost_name.CStr());
	}

	return host_info_item;
}

std::tuple<AccessController::VerificationResult, std::shared_ptr<const SignedPolicy>> AccessController::VerifyBySignedPolicy(const info::Host &host_info, const std::shared_ptr<const ac::RequestInfo> &request_info)
{
	// Handle SignedPolicy if needed
	auto &signed_policy_config = host_info.GetSignedPolicy();

	if (signed_policy_config.IsParsed() == false)
	{
		// The vhost doesn't use the SignedPolicy feature.
		return {AccessController::VerificationResult::Off, nullptr};
	}

	if (_provider_type != ProviderType::Unknown)
	{
		if (signed_policy_config.IsEnabledProvider(_provider_type) == false)
		{
			// This provider turned off the SignedPolicy function
			return {AccessController::VerificationResult::Off, nullptr};
		}
	}
	else if (_publisher_type != PublisherType::Unknown)
	{
		if (signed_policy_config.IsEnabledPublisher(_publisher_type) == false)
		{
			// This publisher turned off the SignedPolicy function
			return {AccessController::VerificationResult::Off, nullptr};
		}
	}
	else
	{
		logte("Could not resolve provider/publisher type: %s", request_info->GetRequestedUrl()->Host().CStr());
		return {AccessController::VerificationResult::Error, nullptr};
	}

	auto policy_query_key_name	  = signed_policy_config.GetPolicyQueryKeyName();
	auto signature_query_key_name = signed_policy_config.GetSignatureQueryKeyName();
	auto secret_key				  = signed_policy_config.GetSecretKey();

	auto signed_policy			  = SignedPolicy::Load(request_info, policy_query_key_name, signature_query_key_name, secret_key);
	if (signed_policy == nullptr)
	{
		// Probably this doesn't happen
		logte("Could not load SignedPolicy");
		return {AccessController::VerificationResult::Error, nullptr};
	}

	if (signed_policy->GetErrCode() != SignedPolicy::ErrCode::PASSED)
	{
		return {AccessController::VerificationResult::Fail, signed_policy};
	}

	return {AccessController::VerificationResult::Pass, signed_policy};
}

std::tuple<AccessController::VerificationResult, std::shared_ptr<const SignedPolicy>> AccessController::VerifyBySignedPolicy(const std::shared_ptr<const ac::RequestInfo> &request_info)
{
	auto host_info_item = GetHostInfo(request_info);

	if (host_info_item.has_value())
	{
		return VerifyBySignedPolicy(host_info_item.value(), request_info);
	}

	return {AccessController::VerificationResult::Error, nullptr};
}

std::tuple<AccessController::VerificationResult, std::shared_ptr<const AdmissionWebhooks>> AccessController::InvokeAdmissionWebhook(
	const info::Host &host_info,
	const std::shared_ptr<const ac::RequestInfo> &request_info,
	std::optional<int> timeout_in_msec,
	AdmissionWebhooks::Status::Code status,
	AccessController::AdmissionWebhookInvokeResult result_callback)
{
	auto &webhooks_config = host_info.GetAdmissionWebhooks();
	if (!webhooks_config.IsParsed())
	{
		// The vhost doesn't use the AdmissionWebhooks feature.
		return {AccessController::VerificationResult::Off, nullptr};
	}

	if (_provider_type != ProviderType::Unknown)
	{
		if (webhooks_config.IsEnabledProvider(_provider_type) == false)
		{
			// This provider turned off the AdmissionWebhooks function
			return {AccessController::VerificationResult::Off, nullptr};
		}
	}
	else if (_publisher_type != PublisherType::Unknown)
	{
		if (webhooks_config.IsEnabledPublisher(_publisher_type) == false)
		{
			// This publisher turned off the AdmissionWebhooks function
			return {AccessController::VerificationResult::Off, nullptr};
		}
	}
	else
	{
		logte("Could not resolve provider/publisher type: %s", request_info->GetRequestedUrl()->Host().CStr());
		return {AccessController::VerificationResult::Error, nullptr};
	}

	auto control_server_url_address = webhooks_config.GetControlServerUrl();
	auto control_server_url			= ov::Url::Parse(control_server_url_address);
	auto secret_key					= webhooks_config.GetSecretKey();
	auto timeout_msec				= timeout_in_msec.value_or(webhooks_config.GetTimeoutMsec());

	if (control_server_url == nullptr)
	{
		logte("Could not parse control server url: %s", control_server_url_address.CStr());
		return {AccessController::VerificationResult::Error, nullptr};
	}

	std::shared_ptr<AdmissionWebhooks> admission_webhooks;
	if (_provider_type != ProviderType::Unknown)
	{
		admission_webhooks = AdmissionWebhooks::Query(_provider_type, control_server_url, timeout_msec, secret_key, request_info, status);
	}
	else if (_publisher_type != PublisherType::Unknown)
	{
		admission_webhooks = AdmissionWebhooks::Query(_publisher_type, control_server_url, timeout_msec, secret_key, request_info, status);
	}
	else
	{
		logte("Provider type or publisher type must be set");
		return {AccessController::VerificationResult::Error, nullptr};
	}

	if (admission_webhooks == nullptr)
	{
		// Probably this doesn't happen
		logte("Could not load admission webhooks");
		return {AccessController::VerificationResult::Error, nullptr};
	}

	if (result_callback != nullptr)
	{
		auto client_address = request_info->GetClientAddress();
		result_callback(
			control_server_url_address,
			client_address,
			admission_webhooks);
	}

	if (admission_webhooks->GetErrCode() != AdmissionWebhooks::ErrCode::ALLOWED)
	{
		return {AccessController::VerificationResult::Fail, admission_webhooks};
	}

	return {AccessController::VerificationResult::Pass, admission_webhooks};
}

std::tuple<AccessController::VerificationResult, std::shared_ptr<const AdmissionWebhooks>> AccessController::VerifyByWebhooks(const info::Host &host_info, const std::shared_ptr<const ac::RequestInfo> &request_info)
{
	return InvokeAdmissionWebhook(
		host_info,
		request_info,
		std::nullopt,
		AdmissionWebhooks::Status::Code::OPENING,
		[&request_info](const ov::String &control_server_url_address,
						const std::shared_ptr<const ov::SocketAddress> &client_address,
						const std::shared_ptr<const AdmissionWebhooks> &admission_webhooks) {
			if (admission_webhooks->GetErrCode() == AdmissionWebhooks::ErrCode::ALLOWED ||
				admission_webhooks->GetErrCode() == AdmissionWebhooks::ErrCode::DENIED)
			{
				logti("AdmissionWebhook queried %s for client %s accessing %s (Result: %s, Elapsed: %" PRIu64 " ms)",
					  control_server_url_address.CStr(),
					  client_address->ToString(false).CStr(),
					  request_info->GetRequestedUrl()->ToUrlString().CStr(),
					  admission_webhooks->GetErrCodeString().CStr(),
					  admission_webhooks->GetElapsedTime());
			}
			else 
			{
				logtw("Failed to query AdmissionWebhook %s for client %s accessing %s (Result: %s, Elapsed: %" PRIu64" ms)",
					  control_server_url_address.CStr(),
					  client_address->ToString(false).CStr(),
					  request_info->GetRequestedUrl()->ToUrlString().CStr(),
					  admission_webhooks->GetErrCodeString().CStr(),
					  admission_webhooks->GetElapsedTime());
			}
		});
}

std::tuple<AccessController::VerificationResult, std::shared_ptr<const AdmissionWebhooks>> AccessController::VerifyByWebhooks(const std::shared_ptr<const ac::RequestInfo> &request_info)
{
	auto host_info_item = GetHostInfo(request_info);

	if (host_info_item.has_value())
	{
		return VerifyByWebhooks(
			host_info_item.value(),
			request_info);
	}

	return {AccessController::VerificationResult::Error, nullptr};
}

std::tuple<AccessController::VerificationResult, std::shared_ptr<const AdmissionWebhooks>> AccessController::SendCloseWebhooks(const info::Host &host_info, const std::shared_ptr<const ac::RequestInfo> &request_info)
{
	return InvokeAdmissionWebhook(
		host_info,
		request_info,
		std::nullopt,
		AdmissionWebhooks::Status::Code::CLOSING,
		[&request_info](const ov::String &control_server_url_address,
						const std::shared_ptr<const ov::SocketAddress> &client_address,
						const std::shared_ptr<const AdmissionWebhooks> &admission_webhooks) {
			if (admission_webhooks->GetErrCode() == AdmissionWebhooks::ErrCode::ALLOWED ||
				admission_webhooks->GetErrCode() == AdmissionWebhooks::ErrCode::DENIED)
			{
				logti("AdmissionWebhook notified %s that client %s closed the connection to %s (Result: %s, Elapsed: %" PRIu64 " ms)",
					  control_server_url_address.CStr(),
					  client_address->ToString(false).CStr(),
					  request_info->GetRequestedUrl()->ToUrlString().CStr(),
					  admission_webhooks->GetErrCodeString().CStr(),
					  admission_webhooks->GetElapsedTime());
			}
			else 
			{
				logtw("Failed to notify AdmissionWebhook %s that client %s closed the connection to %s (Result: %s, Elapsed: %" PRIu64 " ms)",
					  control_server_url_address.CStr(),
					  client_address->ToString(false).CStr(),
					  request_info->GetRequestedUrl()->ToUrlString().CStr(),
					  admission_webhooks->GetErrCodeString().CStr(),
					  admission_webhooks->GetElapsedTime());
			}
		});
}

std::tuple<AccessController::VerificationResult, std::shared_ptr<const AdmissionWebhooks>> AccessController::SendCloseWebhooks(const std::shared_ptr<const ac::RequestInfo> &request_info)
{
	auto host_info_item = GetHostInfo(request_info);

	if (host_info_item.has_value())
	{
		return SendCloseWebhooks(
			host_info_item.value(),
			request_info);
	}

	return {AccessController::VerificationResult::Error, nullptr};
}
