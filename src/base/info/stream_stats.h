//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <atomic>
#include <chrono>
#include <optional>

#include "base/ovlibrary/ovlibrary.h"
#include "base/ovlibrary/tsa/mutex.h"
#include "base/ovsocket/ovsocket.h"

namespace info
{
	// Transport-level details of the connection the stream's media travels on.
	// Immutable once published: writers build a new instance and swap the pointer.
	struct ConnectionInfo
	{
		// Final path type as the client experiences it: "UDP"/"TCP"/"SRT"/"TURN"
		ov::String transport;
		// Socket protocol ("UDP"/"TCP"/"SRT"); differs from `transport` only for TURN
		ov::String protocol;
		// Filled only when the corresponding address was valid at snapshot time
		ov::String local_address;
		std::optional<uint16_t> local_port;
		ov::String remote_address;
		std::optional<uint16_t> remote_port;

		// Selection order of this snapshot; `0` means "unversioned" (always accepted)
		uint64_t version = 0;

		// Build an immutable snapshot.
		// A null (or invalid) address side is left unfilled.
		// `transport` defaults to the socket protocol; `version` orders racing publications.
		static std::shared_ptr<const ConnectionInfo> From(const ov::SocketAddress *local_address, const ov::SocketAddress *remote_address, ov::SocketType socket_type, const char *transport = nullptr, uint64_t version = 0);
		static std::shared_ptr<const ConnectionInfo> From(const ov::SocketAddressPair &address_pair, ov::SocketType socket_type, const char *transport = nullptr, uint64_t version = 0);
		// Fills whichever addresses the socket knows; null only for a null socket
		static std::shared_ptr<const ConnectionInfo> From(const std::shared_ptr<ov::Socket> &socket, const char *transport = nullptr, uint64_t version = 0);

		ov::String ToString() const;
	};

	// Runtime state of one stream instance.
	// This is the deliberately shared mutable state between modules: every copy of a
	// stream shares the same StreamStats instance, while the stream description travels
	// with the copies. State that changes after the stream was created belongs here,
	// otherwise a copy taken before the change never sees it.
	class StreamStats
	{
	public:
		StreamStats();

		std::chrono::system_clock::time_point GetCreatedTime() const;

		// Stamped when the first media packet of the stream is sent to the media router.
		// Epoch until the stream goes on air
		void SetPublishedTime(const std::chrono::system_clock::time_point &time);
		std::chrono::system_clock::time_point GetPublishedTime() const;

		bool IsOnAir() const;
		void SetOnAir(bool on_air);

		// Latched when the media router first sees a packet of this stream. A provider can
		// declare itself published before media flows, so this is the only reliable anchor
		// for how long media has actually been running
		void SetFirstMediaTime();
		bool HasFirstMediaTime() const;
		std::chrono::system_clock::time_point GetFirstMediaTime() const;
		std::chrono::steady_clock::time_point GetFirstMediaTimeSteady() const;

		// Stamped when every track has been described and the stream is notified as
		// prepared. Epoch while the stream is not prepared
		void SetPrepared(bool prepared);
		std::chrono::system_clock::time_point GetPreparedTime() const;

		// Where the media is coming from. Rewritten on every pull-stream failover
		void SetMediaSource(const ov::String &url);
		ov::String GetMediaSource() const;

		// The currently selected transport path; `nullptr` until one is selected.
		// A versioned snapshot is stored only when newer; `false` means dropped as stale.
		bool SetConnectionInfo(const std::shared_ptr<const ConnectionInfo> &connection_info);
		std::shared_ptr<const ConnectionInfo> GetConnectionInfo() const;

	private:
		const std::chrono::system_clock::time_point _created_time;

		std::atomic<int64_t> _published_time = 0;
		std::atomic<bool> _on_air = false;

		std::atomic<int64_t> _first_media_time = 0;
		std::atomic<int64_t> _first_media_time_steady = 0;

		std::atomic<int64_t> _prepared_time = 0;

		mutable ov::Mutex _media_source_mutex;
		ov::String _media_source OV_GUARDED_BY(_media_source_mutex);

		mutable ov::Mutex _connection_info_mutex;
		std::shared_ptr<const ConnectionInfo> _connection_info OV_GUARDED_BY(_connection_info_mutex);
	};
}  // namespace info
