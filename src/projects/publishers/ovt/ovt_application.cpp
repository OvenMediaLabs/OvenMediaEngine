#include "ovt_private.h"
#include "ovt_application.h"
#include "ovt_stream.h"
#include "ovt_session.h"

std::shared_ptr<OvtApplication> OvtApplication::Create(const std::shared_ptr<pub::Publisher> &publisher, const info::Application &application_info)
{
	auto application = std::make_shared<OvtApplication>(publisher, application_info);
	application->Start();
	return application;
}

OvtApplication::OvtApplication(const std::shared_ptr<pub::Publisher> &publisher, const info::Application &application_info)
		: Application(publisher, application_info)
{

}

OvtApplication::~OvtApplication()
{
	Stop();
	logtt("OvtApplication(%d) has been terminated finally", GetId());
}

bool OvtApplication::Start()
{
	return Application::Start();
}

bool OvtApplication::Stop()
{
	return Application::Stop();
}

std::shared_ptr<pub::Stream> OvtApplication::CreateStream(const std::shared_ptr<info::Stream> &info, uint32_t worker_count)
{
	logtt("OvtApplication::CreateStream : %s/%u", info->GetName().CStr(), info->GetId());
	return OvtStream::Create(GetSharedPtrAs<pub::Application>(), *info, worker_count);
}

bool OvtApplication::DeleteStream(const std::shared_ptr<info::Stream> &info)
{
	logtt("OvtApplication::DeleteStream : %s/%u", info->GetName().CStr(), info->GetId());

	auto stream = std::static_pointer_cast<OvtStream>(GetStream(info->GetId()));
	if(stream == nullptr)
	{
		logte("OvtApplication::Delete stream failed. Cannot find stream (%s)", info->GetName().CStr());
		return false;
	}
	
	logtt("OvtApplication %s/%s stream has been deleted", GetVHostAppName().CStr(), stream->GetName().CStr());

	return true;
}