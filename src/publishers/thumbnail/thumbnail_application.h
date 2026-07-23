#pragma once

#include <base/common_types.h>
#include <base/info/session.h>
#include <base/publisher/application.h>
#include <modules/http/http.h>

#include "thumbnail_stream.h"

class ThumbnailApplication final : public pub::Application
{
public:
	static std::shared_ptr<ThumbnailApplication> Create(const std::shared_ptr<pub::Publisher> &publisher, const info::Application &application_info);
	ThumbnailApplication(const std::shared_ptr<pub::Publisher> &publisher, const info::Application &application_info);
	~ThumbnailApplication() final;

	const http::CorsManager &GetCorsManager() const
	{
		return _cors_manager;
	}

private:
	bool Start() override;
	bool Stop() override;

	// Application Implementation
	std::shared_ptr<pub::Stream> CreateStream(const std::shared_ptr<info::Stream> &info, uint32_t worker_count) override;
	bool DeleteStream(const std::shared_ptr<info::Stream> &info) override;

	void SetCrossDomain(const std::vector<ov::String> &url_list);

	http::CorsManager _cors_manager;
};
