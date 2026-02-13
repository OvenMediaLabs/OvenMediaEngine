//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "rtc_application.h"

#include "modules/ice/ice_port_manager.h"
#include "rtc_private.h"

std::shared_ptr<RtcApplication> RtcApplication::Create(const std::shared_ptr<pub::Publisher> &publisher,
													   const info::Application &application_info,
													   const std::shared_ptr<IcePort> &ice_port,
													   const std::shared_ptr<RtcSignallingServer> &rtc_signalling)
{
	auto application = std::make_shared<RtcApplication>(publisher, application_info, ice_port, rtc_signalling);
	application->Start();
	return application;
}

RtcApplication::RtcApplication(const std::shared_ptr<pub::Publisher> &publisher,
							   const info::Application &application_info,
							   const std::shared_ptr<IcePort> &ice_port,
							   const std::shared_ptr<RtcSignallingServer> &rtc_signalling)
	: Application(publisher, application_info)
{
	_ice_port		= ice_port;
	_rtc_signalling = rtc_signalling;
}

RtcApplication::~RtcApplication()
{
	Stop();
	logtt("RtcApplication(%d) has been terminated finally", GetId());
}

std::shared_ptr<Certificate> RtcApplication::GetCertificate()
{
	return _certificate;
}

std::shared_ptr<pub::Stream> RtcApplication::CreateStream(const std::shared_ptr<info::Stream> &info, uint32_t worker_count)
{
	logtt("RtcApplication::CreateStream : %s/%u", info->GetName().CStr(), info->GetId());
	return RtcStream::Create(GetSharedPtrAs<pub::Application>(), *info, worker_count);
}

bool RtcApplication::DeleteStream(const std::shared_ptr<info::Stream> &info)
{
	logtt("DeleteStream : %s/%u", info->GetName().CStr(), info->GetId());

	auto stream = std::static_pointer_cast<RtcStream>(GetStream(info->GetId()));
	if (stream == nullptr)
	{
		logte("RtcApplication::Delete stream failed. Cannot find stream (%s)", info->GetName().CStr());
		return false;
	}

	const auto sessions = stream->GetAllSessions();
	for (auto it = sessions.begin(); it != sessions.end();)
	{
		auto session = std::static_pointer_cast<RtcSession>(it->second);
		it++;

		_ice_port->RemoveSession(session->GetIceSessionId());

		_rtc_signalling->Disconnect(GetVHostAppName(), stream->GetName(), session->GetPeerSDP());
	}

	logtt("RtcApplication %s/%s stream has been deleted", GetVHostAppName().CStr(), stream->GetName().CStr());

	return true;
}

bool RtcApplication::Start()
{
	if (_certificate == nullptr)
	{
		_certificate = std::make_shared<Certificate>();

		auto error	 = _certificate->Generate();
		if (error != nullptr)
		{
			logte("Cannot create certificate: %s", error->What());
			return false;
		}
	}

	return Application::Start();
}

bool RtcApplication::Stop()
{
	return Application::Stop();
}