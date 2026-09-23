//
// Created by getroot on 19. 12. 9.
//

#pragma once

#include <base/common_types.h>
#include <base/ovlibrary/semaphore.h>
#include <base/ovlibrary/url.h>
#include <base/provider/pull_provider/application.h>
#include <base/provider/pull_provider/stream.h>
#include <modules/ovt_packetizer/ovt_depacketizer.h>
#include <modules/ovt_packetizer/ovt_packet.h>
#include <modules/ovt_packetizer/ovt_packetizer.h>
#include <monitoring/monitoring.h>

#include <atomic>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <unordered_set>
#include <vector>

#define OVT_TIMEOUT_MSEC 3000

// A response is the first message a request gets. This many messages of another kind may arrive
// while waiting for one before the request is given up on.
#define MAX_IGNORED_MESSAGES_WHILE_WAITING 32
// Distinct track ids the connection will name in a log line. The origin picks the ids,
// so the set that suppresses repeats needs an end.
#define MAX_LOGGED_TRACK_IDS 32

namespace pvd
{
	class OvtProvider;

	class OvtStream final : public pvd::PullStream, public OvtPacketizerInterface
	{
	public:
		static std::shared_ptr<OvtStream> Create(const std::shared_ptr<pvd::PullApplication> &application, const uint32_t stream_id, const ov::String &stream_name,	const std::vector<ov::String> &url_list, const std::shared_ptr<pvd::PullStreamProperties> &properties);

		OvtStream(const std::shared_ptr<pvd::PullApplication> &application, const info::Stream &stream_info, const std::vector<ov::String> &url_list, const std::shared_ptr<pvd::PullStreamProperties> &properties);
		~OvtStream() final;

		bool OnOvtPacketized(std::shared_ptr<OvtPacket> &packet) override;

		ProcessMediaEventTrigger GetProcessMediaEventTriggerMode() override {
			return ProcessMediaEventTrigger::TRIGGER_EPOLL;
		}

		int GetFileDescriptorForDetectingEvent() override;
		// If this stream belongs to the Pull provider, 
		// this function is called periodically by the StreamMotor of application. 
		// Media data has to be processed here.
		PullStream::ProcessMediaResult ProcessMediaPacket() override;

		// Why an OVT2 describe track is left unregistered, or `nullopt` to register it.
		// Only for an OVT2 origin; an OVT1 origin sends no names,
		// so every track is registered as before.
		static std::optional<ov::String> GetTrackSkipReason(const Json::Value &json_track, const std::shared_ptr<const MediaTrack> &track);

		// Whether the origin's RFC 6381 `codecs` string disagrees with this build's own derivation.
		// False when either side has none. Static and public so a test can drive both sides of it.
		static bool CodecsDisagree(const ov::String &derived, const Json::Value &json_codecs);

		// Field names of one describe or notify track whose value type the protocol does not allow.
		// Empty when the track is readable. A field a later release added may be absent,
		// so each such field names the release it appeared in where it is checked.
		// Static and public so a test can feed it the shape an older release actually sends.
		static std::vector<ov::String> InvalidTrackFields(const Json::Value &json_track);

	private:
		// One origin connection: its socket and depacketizer, what it told us about itself,
		// and what has already been logged for it.
		// A connection replaces this whole object instead of clearing it, so one load hands the media
		// path a socket and a depacketizer that belong to the same connection.
		//
		// `_state` orders the handshake ahead of the media path in the ordinary case, but a stop
		// followed at once by a resume can leave the motor inside `ProcessMediaPacket()` holding this
		// object while the next handshake writes it. The two members both paths touch carry their own
		// ordering; the rest belong to one thread each and say which.
		// The socket and the depacketizer have none of their own, so in that window the two threads can
		// still reach them at the same time (known issue).
		struct Connection
		{
			// Filled in before the object is published, and not written again
			std::shared_ptr<ov::Socket> socket;
			std::shared_ptr<OvtDepacketizer> depacketizer = std::make_shared<OvtDepacketizer>();

			// The origin sent an `ovt` object. Its media header layout is checked right after and a bad
			// one refuses the response, so this is true only for a peer whose layout was readable.
			// The handshake writes it, and the media path reads it.
			std::atomic<bool> origin_is_ovt2			  = false;
			// The origin's `ovt.version`, its generation. 0 when it declared none.
			// Reported once per connection and never judged on; it is here so a later release can hold
			// back what this generation mishandles without refusing the stream.
			uint32_t origin_version						  = 0;
			// At least one PT 40 arrived from this origin. Media path only.
			bool required_announced						  = false;
			// Track ids already named in a log line, so a lasting fault is reported once instead of
			// once per packet. `describe_mismatch_logged` is written by the handshake and by the NOTIFY
			// path, which is why it has a lock; the other two are the media path's alone.
			std::unordered_set<uint32_t> unregistered_track_logged;
			std::unordered_set<uint32_t> wire_format_logged;

			std::mutex describe_mismatch_lock;
			std::unordered_set<uint32_t> describe_mismatch_logged;
		};

		enum class ReceivePacketResult : uint8_t
		{
			COMPLETE,
			INCOMPLETE,
			DISCONNECTED,
			ERROR, 
			TIMEOUT,
			ALREADY_COMPLETED,
		};

		std::shared_ptr<pvd::OvtProvider> GetOvtProvider();

		bool StartStream(const std::shared_ptr<const ov::Url> &url) override; // Start
		bool RestartStream(const std::shared_ptr<const ov::Url> &url) override; // Failover
		bool StopStream() override; // Stop

		bool ConnectOrigin(const std::shared_ptr<Connection> &connection);
		bool RequestDescribe(const std::shared_ptr<Connection> &connection);
		bool ReceiveDescribe(const std::shared_ptr<Connection> &connection, uint32_t request_id);
		// The `playlists` array of a describe response, or the rebuilt one a play response carries.
		// `out` is replaced, so nothing an earlier attempt parsed survives.
		// `source` names the response in the log, because an OVT2 connection parses both.
		bool ParsePlaylists(const Json::Value &json_playlists, std::vector<std::shared_ptr<info::Playlist>> &out, const char *source);
		// Wraps `InvalidTrackFields()` and names each bad field in the log with this stream's identity.
		bool ValidateTrackJson(const Json::Value &json_track);
		// Builds a MediaTrack from one track entry of a DESCRIBE/notify payload.
		// Returns nullptr if the entry is invalid.
		std::shared_ptr<MediaTrack> ParseTrackFromJson(const std::shared_ptr<Connection> &connection, const Json::Value &json_track);

		// Monitoring only: compares MH[30] with the format derived from the track's codec
		void CheckWireFormat(const std::shared_ptr<Connection> &connection, const std::shared_ptr<const MediaPacket> &media_packet);

		// Refuses when the origin's required list (describe or PT 40) names a token this build does not know
		bool CheckRequiredTokens(const Json::Value &json_required, const char *source);

		// One message received while playing: a required set (PT 40), or a `stop` or `notify` control
		// message. Returns SUCCESS to keep going
		PullStream::ProcessMediaResult ProcessMessage(const std::shared_ptr<Connection> &connection, const OvtDepacketizer::Message &message);

		// Installs what the last describe parsed:
		// the playlists replace the previous set,
		// and the tracks in `allowed_track_ids` are added or changed.
		// `nullopt` means an OVT1 origin, which names none, so every described track is registered.
		// The track layout is settled by the first SUCCESSFUL PLAY RESPONSE, on the tracks the describe
		// carried and `allowedTrackIds` confirmed. After that, an id this stream does not already have is
		// named in a warning rather than added, and a track the new describe no longer carries stays
		// registered, because `info::Stream` can neither add nor remove one once shared.
		// Returns false when nothing is left to register, which includes a failover to an origin whose
		// track ids do not overlap the settled layout: it has nothing this stream can deliver.
		bool RegisterDescribedTracks(const std::shared_ptr<Connection> &connection, const std::optional<std::set<uint32_t>> &allowed_track_ids);
		// Applies an origin-pushed track configuration change (server "notify").
		// Only tracks this stream already has are replaced; others are ignored.
		// False when a track is malformed: the change cannot be applied
		// and its old configuration can no longer be trusted.
		bool ApplyTrackNotification(const std::shared_ptr<Connection> &connection, const Json::Value &contents);
		// Whether every field of one describe or notify track carries the type the protocol names.
		bool RequestPlay(const std::shared_ptr<Connection> &connection);
		bool ReceivePlay(const std::shared_ptr<Connection> &connection, uint32_t request_id);
		bool RequestStop();
		bool ReceiveStop(uint32_t request_id, const std::shared_ptr<OvtPacket> &packet);

		bool ReceivePacket(const std::shared_ptr<Connection> &connection, bool non_block = false);
		std::shared_ptr<ov::Data> ReceiveMessage(const std::shared_ptr<Connection> &connection);

		// Publishes what the handshake measured. Monitoring registers the stream only after the first
		// `StartStream()` returns, so that first call finds no metrics object and the media path retries.
		void ReportOriginTimes();

		void Release();

		std::shared_ptr<const ov::Url> _curr_url = nullptr;

		uint32_t _last_request_id;

		// Measured by the handshake, reported once the stream has a metrics object.
		// The media path reads them, so they carry their own ordering.
		std::atomic<int64_t> _origin_request_time_msec{0};
		std::atomic<int64_t> _origin_response_time_msec{0};
		std::atomic<bool> _origin_times_reported{false};

		std::shared_mutex	_packetizer_lock;
		std::shared_ptr<OvtPacketizer>	_packetizer;
		// Parsed by describe, registered after the play response names the tracks that will be sent
		std::vector<std::shared_ptr<MediaTrack>> _described_tracks;
		std::vector<std::shared_ptr<info::Playlist>> _described_playlists;
		// Swapped atomically when a connection is ready, never edited in place: the motor thread can be
		// reading media through the previous one while the collector thread starts the next.
		// A thread already inside the previous connection holds it alive and finishes on it.
		std::shared_ptr<Connection> _connection = std::make_shared<Connection>();

		// The track layout is settled by the first describe of this stream and never restructured
		// afterwards. Written and read on the start / stop path only, which one lock serializes.
		bool _track_layout_fixed				= false;
	};
}