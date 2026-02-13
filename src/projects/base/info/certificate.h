//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovcrypto/ovcrypto.h>
#include <config/config.h>

namespace info
{
	class Certificate
	{
	public:
		///
		/// @param certificate_name A name/identifier of certificate, OME uses virtual host name as this value.
		/// @param host_name_list A host name list (wildcard can be used)
		/// @param tls_config A information of certificate
		///
		static std::shared_ptr<Certificate> CreateCertificate(const ov::String &certificate_name, const std::vector<ov::String> &host_name_list, const cfg::cmn::Tls &tls_config);

		ov::String GetName() const;

		bool IsCertificateForHost(const ov::String &host_name) const;

		std::shared_ptr<::Certificate> GetCertificate() const
		{
			return _certificate;
		}

		ov::String ToString() const;

	protected:
		struct HostNameEntry
		{
			HostNameEntry(ov::String host_name, ov::Regex regex)
				: host_name(std::move(host_name)),
				  regex(std::move(regex))
			{
			}

			ov::String host_name;
			ov::Regex regex;
		};

	protected:
		std::shared_ptr<ov::Error> PrepareCertificate(const ov::String &certificate_name, const std::vector<ov::String> &host_name_list, const cfg::cmn::Tls &tls_config);

	protected:
		ov::String _certificate_name;
		std::vector<HostNameEntry> _host_name_entry_list;

		std::shared_ptr<::Certificate> _certificate;
	};
}  // namespace info
