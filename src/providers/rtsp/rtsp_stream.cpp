#include "rtsp_stream.h"

std::shared_ptr<RtspStream> RtspStream::Create(const std::shared_ptr<pvd::Application> &application)
{
    auto stream = std::make_shared<RtspStream>(application);
    return stream;
}

RtspStream::RtspStream(const std::shared_ptr<pvd::Application> &application)
	: pvd::Stream(application, StreamSourceType::Rtsp)
{	
}


RtspStream::~RtspStream()
{
}
