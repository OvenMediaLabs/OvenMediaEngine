#pragma once

#include "base/common_types.h"

#include "base/provider/application.h"
#include "base/provider/stream.h"

using namespace pvd;

class RtspApplication : public Application
{

public:
	static std::shared_ptr<RtspApplication> Create(const std::shared_ptr<Provider> &provider, const info::Application &application_info);

	explicit RtspApplication(const info::Application &info);
	
	~RtspApplication() override = default;

	std::shared_ptr<pvd::Stream> CreateStream();

private:
};