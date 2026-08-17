#include "session.h"

#include "application.h"
#include "base/info/stream.h"
#include "publisher_private.h"

namespace pub
{
	Session::Session(const std::shared_ptr<Application> &application, const std::shared_ptr<Stream> &stream)
		: info::Session(*std::static_pointer_cast<info::Stream>(stream))
	{
		_application = application;
		_stream		 = stream;
		_state		 = SessionState::Ready;
	}

	Session::Session(const info::Session &info, const std::shared_ptr<Application> &application, const std::shared_ptr<Stream> &stream)
		: info::Session(*std::static_pointer_cast<info::Stream>(stream), info)
	{
		_application = application;
		_stream		 = stream;
		_state		 = SessionState::Ready;
	}

	Session::~Session()
	{
	}

	const std::shared_ptr<Application> &Session::GetApplication()
	{
		return _application;
	}

	std::shared_ptr<const Application> Session::GetApplication() const
	{
		return _application;
	}

	const std::shared_ptr<Stream> &Session::GetStream()
	{
		return _stream;
	}

	std::shared_ptr<const Stream> Session::GetStream() const
	{
		return _stream;
	}

	std::shared_ptr<ov::Url> Session::GetRequestedUrl() const
	{
		return _requested_url;
	}

	void Session::SetRequestedUrl(const std::shared_ptr<ov::Url> &requested_url)
	{
		_requested_url = requested_url;
	}

	std::shared_ptr<ov::Url> Session::GetFinalUrl() const
	{
		return _final_url;
	}

	void Session::SetFinalUrl(const std::shared_ptr<ov::Url> &final_url)
	{
		_final_url = final_url;
	}

	bool Session::Start()
	{
		_state = SessionState::Started;
		return true;
	}

	bool Session::Stop()
	{
		_state = SessionState::Stopped;

		return true;
	}

	Session::SessionState Session::GetState()
	{
		return _state;
	}

	void Session::SetState(SessionState state)
	{
		_state = state;
	}

	void Session::Terminate(ov::String reason)
	{
		_state		  = SessionState::Error;
		_error_reason = reason;

		auto stream	  = GetStream();
		if (stream != nullptr)
		{
			stream->RemoveSession(GetId());
		}
	}
}  // namespace pub