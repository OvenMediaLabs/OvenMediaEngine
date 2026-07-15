#include "rtsp_application.h"
#include "rtsp_stream.h"

#include "base/provider/application.h"
#include "base/info/stream.h"

#define OV_LOG_TAG "RtspApplication"

std::shared_ptr<RtspApplication> RtspApplication::Create(const std::shared_ptr<Provider> &provider, const info::Application &application_info)
{
	auto application = std::make_shared<RtspApplication>(application_info);
	application->Start();
	return application;
}

RtspApplication::RtspApplication(const info::Application &application_info)
	: Application(application_info)
{
}

std::shared_ptr<pvd::Stream> RtspApplication::CreateStream()
{
	logtt("OnStreamCreated");

	auto stream = RtspStream::Create(GetSharedPtrAs<pvd::Application>());

	return stream;
}
