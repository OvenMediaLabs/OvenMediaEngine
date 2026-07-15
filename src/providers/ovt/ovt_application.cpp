//
// Created by getroot on 19. 12. 9.
//

#include "ovt_application.h"
#include "ovt_stream.h"

#define OV_LOG_TAG "OvtApplication"

namespace pvd
{
	std::shared_ptr<OvtApplication> OvtApplication::Create(const std::shared_ptr<PullProvider> &provider, const info::Application &application_info)
	{
		auto application = std::make_shared<OvtApplication>(provider, application_info);

		application->Start();

		return application;
	}

	OvtApplication::OvtApplication(const std::shared_ptr<PullProvider> &provider, const info::Application &info)
			: PullApplication(provider, info)
	{

	}

	OvtApplication::~OvtApplication()
	{

	}

	std::shared_ptr<pvd::PullStream> OvtApplication::CreateStream(const uint32_t stream_id, const ov::String &stream_name, const std::vector<ov::String> &url_list, const std::shared_ptr<pvd::PullStreamProperties> &properties)
	{
		return OvtStream::Create(GetSharedPtrAs<pvd::PullApplication>(), stream_id, stream_name, url_list, properties);
	}

	bool OvtApplication::Start()
	{
		return pvd::PullApplication::Start();
	}

	bool OvtApplication::Stop()
	{
		return pvd::PullApplication::Stop();
	}
}