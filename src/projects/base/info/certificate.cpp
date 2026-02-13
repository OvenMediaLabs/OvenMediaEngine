//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "certificate.h"

#define OV_LOG_TAG "Certificate"

namespace info
{
	std::shared_ptr<Certificate> Certificate::CreateCertificate(const ov::String &certificate_name, const std::vector<ov::String> &host_name_list, const cfg::cmn::Tls &tls_config)
	{
		if (tls_config.IsParsed() == false)
		{
			// TLS is disabled
			return nullptr;
		}

		auto certificate = std::make_shared<Certificate>();

		auto error = certificate->PrepareCertificate(certificate_name, host_name_list, tls_config);

		ov::String cert_path = tls_config.GetCertPath();
		ov::String key_path = tls_config.GetKeyPath();
		ov::String chain_cert_path = tls_config.GetChainCertPath();

		ov::String chain_description = chain_cert_path.IsEmpty() ? "N/A" : ov::String::FormatString("Chain cert file path: %s", chain_cert_path.CStr());

		if (error == nullptr)
		{
			logti(
				"A certificate has been loaded for VirtualHost [%s]:\n"
				"\tCert file path: %s\n"
				"\tChain cert file path: %s\n"
				"\tPrivate key file path: %s",
				certificate_name.CStr(),
				cert_path.CStr(),
				chain_cert_path.CStr(),
				key_path.CStr());
		}
		else
		{
			logte(
				"Failed to create a certificate for VirtualHost [%s]:\n"
				"\tReason: %s\n"
				"\tCert file path: %s\n"
				"\tChain cert file path: %s\n"
				"\tPrivate key file path: %s",
				certificate_name.CStr(),
				error->GetMessage().CStr(),
				cert_path.CStr(),
				chain_cert_path.CStr(),
				key_path.CStr());

			return nullptr;
		}

		return certificate;
	}

	std::shared_ptr<ov::Error> Certificate::PrepareCertificate(const ov::String &certificate_name, const std::vector<ov::String> &host_name_list, const cfg::cmn::Tls &tls_config)
	{
		if (host_name_list.empty())
		{
			logtw("Host name list is empty. Certificate may not work properly (name: %s)", certificate_name.CStr());
		}

		ov::String cert_path = tls_config.GetCertPath();
		ov::String key_path = tls_config.GetKeyPath();
		ov::String chain_cert_path = tls_config.GetChainCertPath();

		if (cert_path.IsEmpty())
		{
			return ov::Error::CreateError(OV_LOG_TAG, "[%s] Invalid TLS configuration: <CertPath> must not be empty", certificate_name.CStr());
		}

		if (key_path.IsEmpty())
		{
			return ov::Error::CreateError(OV_LOG_TAG, "[%s] Invalid TLS configuration: <KeyPath> must not be empty", certificate_name.CStr());
		}

		auto certificate = std::make_shared<::Certificate>();
		auto error = certificate->GenerateFromPem(key_path, cert_path, chain_cert_path);

		if (error != nullptr)
		{
			return ov::Error::CreateError(OV_LOG_TAG, "[%s] Could not create a certificate from file - %s", certificate_name.CStr(), error->What());
		}

		_certificate_name = certificate_name;
		_certificate = certificate;

		for (auto &host_name : host_name_list)
		{
			_host_name_entry_list.emplace_back(
				host_name,
				ov::Regex::CompiledRegex(ov::Regex::WildCardRegex(host_name)));
		}

		return nullptr;
	}

	bool Certificate::IsCertificateForHost(const ov::String &host_name) const
	{
		for (const auto &host_name_entry : _host_name_entry_list)
		{
			auto match_result = host_name_entry.regex.Matches(host_name);

			if (match_result.IsMatched())
			{
				logtt("Matched: host_name: %s, pattern: %s",
					  host_name.CStr(),
					  host_name_entry.regex.GetPattern().CStr());

				return true;
			}

			auto error = match_result.GetError();

			if (error != nullptr)
			{
				logtt("Not matched with error: host_name: %s, pattern: %s, error: %s",
					  host_name.CStr(),
					  host_name_entry.regex.GetPattern().CStr(),
					  error->What());
			}
		}

		return false;
	}

	ov::String Certificate::GetName() const
	{
		return _certificate_name;
	}

	ov::String Certificate::ToString() const
	{
		ov::String description;

		description.AppendFormat("<Certificate: %p, name: %s [", this, _certificate_name.CStr());

		int index = 0;

		for (auto &entry : _host_name_entry_list)
		{
			if (index > 0)
			{
				description.Append(", ");
			}

			description.AppendFormat("%s", entry.host_name.CStr());

			index++;
		}

		description.Append("]>");

		return description;
	}
}  // namespace info
