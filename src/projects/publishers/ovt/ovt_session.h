#pragma once

#include <base/info/media_track.h>
#include <base/ovsocket/socket.h>
#include <base/publisher/session.h>

class OvtSession : public pub::Session
{
public:
	static std::shared_ptr<OvtSession> Create(const std::shared_ptr<pub::Application> &application,
											  const std::shared_ptr<pub::Stream> &stream,
											  uint32_t ovt_session_id,
											  const std::shared_ptr<ov::Socket> &connector);

	OvtSession(const info::Session &session_info,
			const std::shared_ptr<pub::Application> &application,
			const std::shared_ptr<pub::Stream> &stream,
			const std::shared_ptr<ov::Socket> &connector);
	~OvtSession() override;

	bool Start() override;
	bool Stop() override;

	void SendOutgoingData(const std::any &packet) override;
	void OnMessageReceived(const std::any &message) override;

	const std::shared_ptr<ov::Socket> GetConnector();

	// Restrict packet forwarding to the given track ids. Calling this enables the
	// TrackSet filter even if the id set is empty (in which case all media packets
	// are dropped). If this is never called, no filtering is applied.
	void SetAllowedTrackIds(const std::set<uint32_t> &allowed_track_ids);

private:
	// Per-fragment-group filtering decision used by SendOutgoingData.
	//
	// An OvtPacket header has no track id; the id only appears in the first
	// fragment of a serialized MediaPacket. SendOutgoingData reads the id once
	// per fragment-group, caches the decision below, and applies it to every
	// fragment in that group until the marker packet resets state.
	enum class GroupDecision
	{
		// First fragment of the group not yet seen; do not forward.
		Pending,

		// Track id matched the allowed set; forward every fragment of the group.
		Accept,

		// Track id not in the allowed set or unreadable; drop every fragment.
		Drop,
	};

	std::shared_ptr<ov::Socket>		_connector;
	bool 							_sent_ready;

	// `_track_set_filter_enabled` and `_allowed_track_ids` are written by
	// the OVT publisher request thread (via `SetAllowedTrackIds()` during `HandlePlayRequest()`)
	// and read by the stream worker thread (via `SendOutgoingData()`).
	// Both sides acquire `_track_set_filter_mutex` to establish the synchronizes-with edge.
	//
	// When `_track_set_filter_enabled` is `false`, no filtering is performed (full stream is forwarded).
	// When `true`, only media packets whose track id is in `_allowed_track_ids` are forwarded;
	// an empty `_allowed_track_ids` in this state drops every media packet.
	mutable std::mutex _track_set_filter_mutex;
	bool _track_set_filter_enabled = false;
	std::set<uint32_t> _allowed_track_ids;
	// Per fragment-group filter state. A "group" is the run of OvtPackets that
	// together carry one serialized MediaPacket; only the first packet of a group
	// contains the 4-byte track id at the start of its payload, so we cache the
	// accept/drop decision until the group's marker packet is seen.
	GroupDecision _current_group_decision = GroupDecision::Pending;
	// One-shot guard to avoid log spam when the first fragment is malformed.
	bool _warned_malformed_first_fragment = false;
};
