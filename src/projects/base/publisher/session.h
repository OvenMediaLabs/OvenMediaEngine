//
// Created by getroot on 18. 3. 19.
//

#pragma once

#include <base/ovlibrary/ovlibrary.h>

#include "base/common_types.h"
#include "base/info/session.h"

namespace pub
{
	class Application;
	class Stream;

	class Session : public info::Session
	{
	public:
		Session(const std::shared_ptr<Application> &application, const std::shared_ptr<Stream> &stream);
		Session(const info::Session &info, const std::shared_ptr<Application> &app, const std::shared_ptr<Stream> &stream);
		virtual ~Session();

		const std::shared_ptr<Application> &GetApplication();
		std::shared_ptr<const Application> GetApplication() const;
		const std::shared_ptr<Stream> &GetStream();
		std::shared_ptr<const Stream> GetStream() const;

		std::shared_ptr<ov::Url> GetRequestedUrl() const;
		void SetRequestedUrl(const std::shared_ptr<ov::Url> &requested_url);

		std::shared_ptr<ov::Url> GetFinalUrl() const;
		void SetFinalUrl(const std::shared_ptr<ov::Url> &final_url);

		/// Optional hook overridden by publishers whose protocol can express a
		/// more authoritative track-level scope than what the session URL
		/// encodes (OVT, after a runtime `subscribe` covering a union of
		/// playlists). When this returns a value, the linked input provider
		/// stream uses it as the authoritative input for shared-request-state
		/// accumulation instead of inferring demand from `_requested_url` /
		/// `_final_url` alone (which can only express full-stream or a single
		/// playlist file).
		///
		/// @return Set of track ids the session has been granted by the upstream
		///         protocol, or `std::nullopt` to fall back to URL inference.
		virtual std::optional<std::set<int32_t>> GetAuthoritativeAllowedTrackIds() const
		{
			return std::nullopt;
		}

		virtual bool Start();
		virtual bool Stop();

		virtual void SendOutgoingData(const std::any &packet) {};
		virtual void OnMessageReceived(const std::any &message) {};

		enum class SessionState : int8_t
		{
			Ready,
			Started,
			Stopping,
			Stopped,
			Error
		};

		SessionState GetState();
		void SetState(SessionState state);
		virtual void Terminate(ov::String reason);

	protected:
		std::shared_ptr<ov::Url> _requested_url;
		std::shared_ptr<ov::Url> _final_url;

	private:
		std::shared_ptr<Application> _application;
		std::shared_ptr<Stream> _stream;
		SessionState _state;
		ov::String _error_reason;
	};

}  // namespace pub