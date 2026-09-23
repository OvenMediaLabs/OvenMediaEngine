#pragma once

#include <base/info/media_track.h>
#include <base/ovsocket/socket.h>
#include <base/publisher/session.h>

#include <optional>

#include "ovt_required_set.h"

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

	// Writes one signaling message straight to the connector,
	// bypassing the `_sent_ready` gate and the TrackSet filter.
	// A payload longer than one OVT packet is fragmented and the fragments go out in a single `Send()`,
	// so this message is never split around another writer's message.
	bool SendMessageDirect(OvtPayloadType payload_type, const ov::String &payload);

	const std::shared_ptr<ov::Socket> GetConnector();

	// Whether the edge speaks OVT2, and the track epoch its describe of this stream reflected
	// (`nullopt` when this connection described no stream, or described another one).
	// Set once before the session is added to the stream.
	void SetPeer(bool is_ovt2, const std::optional<uint32_t> &track_epoch)
	{
		_is_ovt2 = is_ovt2;

		if (track_epoch.has_value())
		{
			_track_epoch_cursor.SetDescribeEpoch(*track_epoch);
		}
		else
		{
			_track_epoch_cursor.SetDescribeUnknown();
		}
	}

	bool IsOvt2() const
	{
		return _is_ovt2;
	}

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

	// Sends the stream's current required set when this session has not delivered that version yet.
	// Called on the stream worker right before the first fragment of a media packet.
	void SendRequiredIfChanged();
	// Catches this session up on a NOTIFY that was broadcast while it was still behind the send gate
	void SendTrackSnapshotIfStale();

	std::shared_ptr<ov::Socket>		_connector;
	bool 							_sent_ready;
	bool							_is_ovt2 = false;
	// True while the next media fragment starts a new serialized `MediaPacket`
	bool							_at_group_start = true;

	// Fragments of the unit being assembled, flushed in one `Send()` at its marker.
	// Stream worker only, like the two flags above.
	std::shared_ptr<ov::Data>		_unit_buffer;
	OvtRequiredCursor				_required_cursor;
	OvtTrackEpochCursor				_track_epoch_cursor;

	// `_track_set_filter_enabled` and `_allowed_track_ids` are written by
	// the OVT publisher request thread (via `SetAllowedTrackIds()` during `HandlePlayRequest()`)
	// and read by the stream worker thread (via `SendOutgoingData()` and `SendTrackSnapshotIfStale()`).
	// Both sides acquire `_track_set_filter_mutex` to establish the synchronizes-with edge.
	// An empty `_allowed_track_ids` while enabled drops every media packet,
	// which is not the same as being disabled.
	mutable ov::Mutex _track_set_filter_mutex;
	bool _track_set_filter_enabled OV_GUARDED_BY(_track_set_filter_mutex) = false;
	std::set<uint32_t> _allowed_track_ids OV_GUARDED_BY(_track_set_filter_mutex);
	// Per fragment-group filter state. A "group" is the run of OvtPackets that
	// together carry one serialized MediaPacket; only the first packet of a group
	// contains the 4-byte track id at the start of its payload, so we cache the
	// accept/drop decision until the group's marker packet is seen.
	GroupDecision _current_group_decision OV_GUARDED_BY(_track_set_filter_mutex) = GroupDecision::Pending;
	// One-shot guard to avoid log spam when the first fragment is malformed.
	bool _warned_malformed_first_fragment OV_GUARDED_BY(_track_set_filter_mutex) = false;
};
