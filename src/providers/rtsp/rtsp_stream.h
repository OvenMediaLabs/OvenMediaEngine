#pragma once

#include "base/common_types.h"
#include "base/provider/stream.h"

using namespace pvd;

class RtspStream : public pvd::Stream
{
public:
	static std::shared_ptr<RtspStream> Create(const std::shared_ptr<pvd::Application> &application);

public:
	explicit RtspStream(const std::shared_ptr<pvd::Application> &application);
	~RtspStream() final;

private:
};