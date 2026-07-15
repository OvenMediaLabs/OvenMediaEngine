#pragma once

#include <base/common_types.h>
#include <base/info/session.h>
#include <base/publisher/application.h>

#include "ovt_stream.h"

class OvtApplication final : public pub::Application
{
public:
	static std::shared_ptr<OvtApplication> Create(const std::shared_ptr<pub::Publisher> &publisher, const info::Application &application_info);
	OvtApplication(const std::shared_ptr<pub::Publisher> &publisher, const info::Application &application_info);
	~OvtApplication() final;

private:
	bool Start() override;
	bool Stop() override;

	// Application Implementation
	std::shared_ptr<pub::Stream> CreateStream(const std::shared_ptr<info::Stream> &info, uint32_t worker_count) override;
	bool DeleteStream(const std::shared_ptr<info::Stream> &info) override;
};
