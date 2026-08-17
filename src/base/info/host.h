//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <config/config.h>

#include "application.h"
#include "certificate.h"

namespace info
{
	typedef uint32_t host_id_t;

	class Host : public cfg::vhost::VirtualHost
	{
	public:
		explicit Host(const ov::String &server_name, const ov::String &server_id, const cfg::vhost::VirtualHost &host_info);

		host_id_t GetId() const
		{
			return _host_id;
		}

		ov::String GetUUID() const;

		bool LoadCertificate();

		const std::shared_ptr<Certificate> &GetCertificate() const
		{
			return _certificate;
		}

	protected:
		ov::String _server_name;
		ov::String _server_id;
		host_id_t _host_id = 0;
		std::shared_ptr<Certificate> _certificate = nullptr;
	};

}  // namespace info