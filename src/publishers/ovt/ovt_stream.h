#pragma once

#include <base/common_types.h>
#include <base/publisher/stream.h>
#include <modules/ovt_packetizer/ovt_packetizer.h>
#include <modules/ovt_packetizer/ovt_signaling.h>

#include "monitoring/monitoring.h"
#include "ovt_required_set.h"
#include "ovt_track_filter.h"

class OvtStream final : public pub::Stream, public OvtPacketizerInterface
{
public:
	// One edit `RebuildRenditions()` made to a playlist
	struct RenditionChange
	{
		enum class Type
		{
			// The rendition stays with its other side
			SideEmptied,
			RenditionRemoved,
			// The tracks are the same as `kept_rendition`'s
			DuplicateRemoved,
			PlaylistRemoved,
		};

		// A side that lost tracks to the filter
		struct LostSide
		{
			cmn::MediaType media_type = cmn::MediaType::Unknown;
			ov::String variant;
			// The track the side was pinned to, if any
			std::optional<uint32_t> track_id;
			// False when a side without a pinned track kept some of its variant's tracks
			bool emptied = true;
		};

		Type type = Type::SideEmptied;
		ov::String playlist;
		ov::String rendition;
		std::vector<LostSide> lost_sides;
		ov::String kept_rendition;
	};

public:
	static std::shared_ptr<OvtStream> Create(const std::shared_ptr<pub::Application> application,
											 const info::Stream &info,
											 uint32_t worker_count);
	explicit OvtStream(const std::shared_ptr<pub::Application> application,
					   const info::Stream &info,
					   uint32_t worker_count);
	~OvtStream() final;

	void SendVideoFrame(const std::shared_ptr<MediaPacket> &media_packet) override;
	void SendAudioFrame(const std::shared_ptr<MediaPacket> &media_packet) override;
	void SendDataFrame(const std::shared_ptr<MediaPacket> &media_packet) override {} // Not supported yet
	

	bool OnOvtPacketized(std::shared_ptr<OvtPacket> &packet) override;

	bool RemoveSessionByConnectorId(int connector_id);

	// Resolves the set of allowed track ids for the named TrackSet.
	// Returns false if the TrackSet does not exist on this stream.
	bool ResolveTrackSetTrackIds(const ov::String &track_set_name, std::set<uint32_t> &out_track_ids);

	// The DESCRIBE `contents` for one edge, in the OVT2 form.
	// With an active filter the tracks are reduced and the renditions rebuilt (`RebuildRenditions()`);
	// the OVT1 codec judgment is made on the `codec` strings of this very JSON,
	// so that it cannot diverge from the tracks it describes.
	// `filtered` reports whether any track was removed.
	bool GetDescription(const OvtTrackFilter &filter, Json::Value &description, bool *filtered = nullptr);

	// Track ids a PLAY may deliver under `filter`, from the current tracks. nullopt means no filtering.
	std::optional<std::set<uint32_t>> ResolveAllowedTrackIds(const OvtTrackFilter &filter);

	// The `playlists` array a PLAY response carries, rebuilt for the confirmed track set.
	// It runs the same `RebuildRenditions()` rule the describe uses, so the edge's renditions end up
	// consistent with the tracks it will actually receive.
	// `described_track_ids` is what this connection's describe of the stream sent, nullopt when it described none.
	// The changes are logged unless it is the same set, since that describe logged its own.
	Json::Value BuildPlaylistsForTracks(const OvtTrackFilter &filter, const std::set<uint32_t> &allowed,
										const std::optional<std::set<uint32_t>> &described_track_ids);

	// `ovt.required` of a describe:
	// the codec token of every track whose media type is in `required_media_types`, sorted and unique.
	// Built from the JSON that is sent, so it names exactly the tracks the edge sees.
	static std::vector<ov::String> CollectRequiredTokens(const Json::Value &contents, const std::set<cmn::MediaType> &required_media_types);

	// The required tokens learned at packetization time (PT 40), with their version
	OvtRequiredSet::Snapshot GetRequiredSnapshot() const
	{
		return _required_set.Get();
	}

	uint32_t GetRequiredVersion() const
	{
		return _required_set.GetVersion();
	}

	// Grows on every track configuration change this stream relayed as a NOTIFY.
	// A session compares the value its describe reflected against this one:
	// a NOTIFY broadcast while the session was still behind the `_sent_ready` gate never reached it,
	// so a difference means the tracks it learned at describe are stale.
	uint32_t GetTrackEpoch() const
	{
		return _track_epoch.load(std::memory_order_acquire);
	}

	// A NOTIFY carrying every track's current configuration, for a session that missed one.
	// Empty when there is no track to describe.
	// Static so a test can pin the wire form without an application, like `BuildDescription()`.
	static ov::String BuildTrackSnapshot(const std::map<int32_t, std::shared_ptr<const MediaTrack>> &tracks);

	static void GenerateTrackDescription(const std::shared_ptr<const MediaTrack> &track, Json::Value &out_json_track);
	// The DESCRIBE `contents` for the given stream parts. Static so a test can pin the wire form
	// without an application; `tracks` is walked in map order, which is the id order.
	static Json::Value BuildDescription(const ov::String &app_name, const ov::String &stream_name, const ov::String &origin_stream_uuid,
										const std::map<ov::String, std::shared_ptr<const info::Playlist>> &playlists,
										const std::map<int32_t, std::shared_ptr<const MediaTrack>> &tracks);
	// Rewrites each rendition's index hints to the group index an edge assigns
	// when it adds the tracks of `stream["tracks"]` in order.
	// Only an OVT1 response needs it, and only when tracks were filtered;
	// an OVT2 edge reads the track id fields, which are left alone either way.
	static void RenumberIndexHints(Json::Value &stream);
	// Whether an OVT1 edge can carry the track this describe JSON entry names.
	// The judgment reads the `codec` string of the entry that is about to be sent, not the live track,
	// so the set of tracks selected cannot diverge from the body describing them.
	// A `Data` track carries no codec and always passes.
	static bool IsCarriableByLegacyEdge(const Json::Value &track);
	// Keeps only `allowed` in `stream["tracks"]` and rebuilds every rendition around what remains.
	// Returns the number of renditions and playlists removed.
	// `changes` lists each removal and each kept rendition that lost a whole side;
	// a rendition that names no variant, or a playlist that had only such renditions, is left out.
	static size_t RebuildRenditions(Json::Value &stream, const std::set<uint32_t> &allowed, std::vector<RenditionChange> *changes = nullptr);
	// The origin log lines for `changes`: what happened to each rendition and playlist.
	// The causes are on the track lines `LogRenditionChanges()` writes next to them.
	static std::vector<ov::String> DescribeRenditionChanges(const std::vector<RenditionChange> &changes);
	// Why `filter` removes the describe track entry `track`, worded for the origin log; empty when it passes
	static ov::String RemovalCause(const OvtTrackFilter &filter, const Json::Value &track);

private:
	// Logs a warning for every change, after one for each track of a lost variant that `allowed` leaves out,
	// with its cause; `GetDescription()` logs the OVT1 codec filter's own. `path` is the request the playlists answer.
	void LogRenditionChanges(const char *path, const OvtTrackFilter &filter, const Json::Value &tracks, const std::set<uint32_t> &allowed,
							 const std::vector<RenditionChange> &changes);

	bool Start() override;
	bool Stop() override;
	// Tells every session that this stream is stopping and why
	void NotifyStop(ovt::StopReason reason);

	// Relays the origin's already-parsed track configuration to connected edges
	// so a runtime change (codec, resolution, decoder config) is applied there.
	void OnTrackChanged(int32_t track_id, const std::shared_ptr<const MediaTrack> &old_track, const std::shared_ptr<const MediaTrack> &new_track) override;

	void GenerateDescription(Json::Value &out_description);

	// A transport format or packet type an OVT1 peer has no value for becomes a required token here,
	// ahead of the packet that carries it
	void NoteRequiredValues(const std::shared_ptr<const MediaPacket> &media_packet);

	uint32_t _worker_count = 0;

	std::shared_mutex _packetizer_lock;
	std::shared_ptr<OvtPacketizer> _packetizer;
	OvtRequiredSet _required_set;
	std::atomic<uint32_t> _track_epoch = 1;
};
