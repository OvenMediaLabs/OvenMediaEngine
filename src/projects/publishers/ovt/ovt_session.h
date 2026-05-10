#pragma once

#include <base/info/media_track.h>
#include <base/ovsocket/socket.h>
#include <base/publisher/session.h>

class OvtPacket;

class OvtSession : public pub::Session
{
public:
	static std::shared_ptr<OvtSession> Create(const std::shared_ptr<pub::Application> &application,
											  const std::shared_ptr<pub::Stream> &stream,
											  uint32_t ovt_session_id,
											  const std::shared_ptr<ov::Socket> &connector,
											  const std::optional<std::set<int32_t>> &allowed_track_ids = std::nullopt);

	OvtSession(const info::Session &session_info,
			   const std::shared_ptr<pub::Application> &application,
			   const std::shared_ptr<pub::Stream> &stream,
			   const std::shared_ptr<ov::Socket> &connector,
			   const std::optional<std::set<int32_t>> &allowed_track_ids = std::nullopt);
	~OvtSession() override;

	bool Start() override;
	bool Stop() override;
	bool UpdateAllowedTrackIds(const std::optional<std::set<int32_t>> &allowed_track_ids);
	std::optional<std::set<int32_t>> GetAllowedTrackIds() const;

	// Expose the OVT session's filtered track set as the authoritative scope for the
	// linked input provider stream's shared-request-state accumulation. This is the
	// signal that lets a multi-playlist `subscribe` (issues.md B1) be tracked as a
	// union demand instead of being misclassified as a full-stream demand from the
	// decorative session URL alone.
	std::optional<std::set<int32_t>> GetAuthoritativeAllowedTrackIds() const override
	{
		return GetAllowedTrackIds();
	}

	void SendOutgoingData(const std::any &packet) override;
	void OnMessageReceived(const std::any &message) override;

	const std::shared_ptr<ov::Socket> GetConnector();

private:
	bool ShouldForwardPacketLocked(const std::shared_ptr<OvtPacket> &packet);
	bool BeginMediaPacketFilterDecisionLocked(const std::shared_ptr<OvtPacket> &packet);
	bool IsTrackAllowedLocked(uint32_t track_id) const;

protected:
	virtual bool EmitPacket(const std::shared_ptr<OvtPacket> &packet);

	std::shared_ptr<ov::Socket>		_connector;
	bool 							_sent_ready;
	std::optional<std::set<int32_t>> _allowed_track_ids;
	bool _current_media_packet_allowed;
	bool _has_current_media_packet_filter;
	// Cached track id of the current media-packet fragment (set on the first packet of the
	// fragment in BeginMediaPacketFilterDecisionLocked). Used to re-evaluate the filter
	// decision when UpdateAllowedTrackIds widens the scope mid-fragment, so a fragment that
	// was being dropped under the old narrower filter can start flowing immediately if the
	// new wider filter now allows that track.
	std::optional<uint32_t> _current_media_packet_track_id;
	mutable std::mutex _filter_lock;
};
