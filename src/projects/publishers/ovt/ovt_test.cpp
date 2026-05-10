//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================

//
//  Covers: OVT playlist-scoped description assembly, playlist resolution, and runtime subscribe helpers
//
#include <base/info/application.h>
#include <base/info/host.h>
#include <base/info/media_track.h>
#include <base/info/playlist.h>
#include <base/info/stream.h>
#include <base/ovlibrary/byte_io.h>
#include <base/ovlibrary/json.h>
#include <base/ovlibrary/url.h>
#include <gtest/gtest.h>
#include <modules/ovt_packetizer/ovt_signaling.h>


#include "ovt_publisher_internal.h"
#include "ovt_session.h"
#include "ovt_stream.h"

// Integration test below crosses the publisher<->provider seam (RefreshSessionScope ->
// linked input -> provider's TryAccumulateActiveRequestScopeLocked) and needs to read
// the provider stream's private `_shared_request_state` to verify B1 follow-up
// regression is closed. Same trick the provider's own test file uses.
#define private public
#define protected public
#include <providers/ovt/ovt_stream.h>
#undef private
#undef protected

namespace
{
	class TestApplicationInfo final : public info::Application
	{
	public:
		TestApplicationInfo(const ov::String &vhost_name, const ov::String &app_name)
			: info::Application(
				  info::Host("test-server", "test-server-id", cfg::vhost::VirtualHost()),
				  1,
				  info::VHostAppName(vhost_name, app_name),
				  false)
		{
		}
	};

	std::shared_ptr<MediaTrack> MakeTrack(uint32_t track_id,
										  cmn::MediaType media_type,
										  const ov::String &variant_name,
										  const ov::String &public_name)
	{
		auto track = std::make_shared<MediaTrack>();
		track->SetId(track_id);
		track->SetMediaType(media_type);
		track->SetVariantName(variant_name);
		track->SetPublicName(public_name);
		track->SetLanguage("und");
		track->SetTimeBase(1, 1000);
		track->SetBitrateByConfig(1000000);
		track->SetStartFrameTime(0);
		track->SetLastFrameTime(1000);

		if (media_type == cmn::MediaType::Video)
		{
			track->SetCodecId(cmn::MediaCodecId::H264);
		}
		else
		{
			track->SetCodecId(cmn::MediaCodecId::Aac);
			track->SetSampleRate(48000);
		}

		return track;
	}

	std::shared_ptr<const info::Playlist> MakePlaylist(const ov::String &name,
													   const ov::String &file_name,
													   const ov::String &video_variant_name,
													   int video_index_hint,
													   const ov::String &audio_variant_name,
													   int audio_index_hint)
	{
		auto playlist  = std::make_shared<info::Playlist>(name, file_name, false);
		auto rendition = std::make_shared<info::Rendition>(name, video_variant_name, audio_variant_name);
		rendition->SetVideoIndexHint(video_index_hint);
		rendition->SetAudioIndexHint(audio_index_hint);
		playlist->AddRendition(rendition);
		return playlist;
	}

	std::shared_ptr<OvtStream> MakeStartedOvtStream(const std::shared_ptr<info::Application> &app_info,
													const ov::String &stream_name,
													const std::vector<std::shared_ptr<MediaTrack>> &tracks,
													const std::vector<std::shared_ptr<const info::Playlist>> &playlists)
	{
		info::Stream stream_info(*app_info, StreamSourceType::Ovt);
		stream_info.SetName(stream_name);

		for (const auto &track : tracks)
		{
			if (stream_info.AddTrack(track) == false)
			{
				ADD_FAILURE() << "Failed to add track to stream fixture";
				return nullptr;
			}
		}

		for (const auto &playlist : playlists)
		{
			if (stream_info.AddPlaylist(playlist) == false)
			{
				ADD_FAILURE() << "Failed to add playlist to stream fixture";
				return nullptr;
			}
		}

		auto stream = std::make_shared<OvtStream>(nullptr, stream_info, 0);
		stream->SetState(pub::Stream::State::STARTED);
		return stream;
	}

	std::set<int32_t> CollectTrackIdsFromDescription(const Json::Value &description)
	{
		std::set<int32_t> track_ids;
		for (const auto &json_track : description["stream"]["tracks"])
		{
			track_ids.emplace(json_track["id"].asInt());
		}

		return track_ids;
	}

	std::shared_ptr<const ov::Url> ParseUrl(const char *url)
	{
		return ov::Url::Parse(url);
	}

	std::shared_ptr<OvtPacket> MakeMediaOvtPacket(uint32_t track_id, bool marker, uint16_t sequence_number)
	{
		uint8_t payload[sizeof(uint32_t)] = {0};
		ByteWriter<uint32_t>::WriteBigEndian(payload, track_id);

		auto packet = std::make_shared<OvtPacket>();
		packet->SetPayloadType(OVT_PAYLOAD_TYPE_MEDIA_PACKET);
		packet->SetMarker(marker);
		packet->SetSequenceNumber(sequence_number);
		packet->SetTimestamp(sequence_number);
		packet->SetSessionId(0);
		EXPECT_TRUE(packet->SetPayload(payload, sizeof(payload)));
		return packet;
	}

	class RecordingOvtSession final : public OvtSession
	{
	public:
		RecordingOvtSession(const info::Session &session_info,
							const std::shared_ptr<pub::Application> &application,
							const std::shared_ptr<pub::Stream> &stream,
							const std::optional<std::set<int32_t>> &allowed_track_ids)
			: OvtSession(session_info, application, stream, nullptr, allowed_track_ids)
		{
		}

		std::vector<uint32_t> ForwardedTrackIds() const
		{
			std::vector<uint32_t> track_ids;
			for (const auto &packet : _forwarded_packets)
			{
				if ((packet == nullptr) || (packet->PayloadLength() < sizeof(uint32_t)))
				{
					continue;
				}

				track_ids.emplace_back(ByteReader<uint32_t>::ReadBigEndian(packet->Payload()));
			}

			return track_ids;
		}

	protected:
		bool EmitPacket(const std::shared_ptr<OvtPacket> &packet) override
		{
			_forwarded_packets.emplace_back(std::make_shared<OvtPacket>(*packet));
			return true;
		}

	private:
		std::vector<std::shared_ptr<OvtPacket>> _forwarded_packets;
	};

	std::shared_ptr<RecordingOvtSession> MakeStartedRecordingSession(const std::shared_ptr<OvtStream> &stream,
																	 uint32_t session_id,
																	 const std::optional<std::set<int32_t>> &allowed_track_ids)
	{
		auto stream_info = std::static_pointer_cast<info::Stream>(stream);
		info::Session session_info(*stream_info, session_id);

		auto session = std::make_shared<RecordingOvtSession>(session_info, nullptr, stream, allowed_track_ids);
		if (session->Start() == false)
		{
			ADD_FAILURE() << "Failed to start recording OVT session fixture";
			return nullptr;
		}

		return session;
	}
}  // namespace

TEST(OvtStreamPlaylist, ResolveTrackIdsUsesVariantNameAndIndexHint)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeStartedOvtStream(
		app_info,
		"stream",
		{
			MakeTrack(101, cmn::MediaType::Video, "video", "video-main"),
			MakeTrack(102, cmn::MediaType::Video, "video", "video-backup"),
			MakeTrack(201, cmn::MediaType::Audio, "audio", "audio-main"),
		},
		{
			MakePlaylist("master", "master", "video", 1, "audio", 0),
		});
	ASSERT_NE(stream, nullptr);

	std::set<int32_t> track_ids;
	ASSERT_TRUE(stream->ResolveTrackIdsForPlaylist("master", track_ids));
	EXPECT_EQ(track_ids, (std::set<int32_t>{102, 201}));
}

TEST(OvtStreamDescription, PlaylistScopedDescriptionContainsOnlyRequestedPlaylistTracks)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeStartedOvtStream(
		app_info,
		"stream",
		{
			MakeTrack(101, cmn::MediaType::Video, "video-main", "video-main"),
			MakeTrack(102, cmn::MediaType::Audio, "audio-main", "audio-main"),
			MakeTrack(201, cmn::MediaType::Video, "video-mobile", "video-mobile"),
		},
		{
			MakePlaylist("master", "master", "video-main", 0, "audio-main", 0),
			MakePlaylist("mobile", "mobile", "video-mobile", 0, "audio-main", 0),
		});
	ASSERT_NE(stream, nullptr);

	Json::Value description;
	ASSERT_TRUE(stream->GetDescription(description, "mobile"));
	ASSERT_TRUE(description["stream"].isObject());
	ASSERT_EQ(description["stream"]["playlists"].size(), 1U);
	EXPECT_EQ(description["stream"]["playlists"][0]["fileName"].asString(), "mobile");
	EXPECT_EQ(CollectTrackIdsFromDescription(description), (std::set<int32_t>{102, 201}));
}

TEST(OvtStreamDescription, FullDescriptionAdvertisesRuntimeWideningAndAllTracks)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeStartedOvtStream(
		app_info,
		"stream",
		{
			MakeTrack(101, cmn::MediaType::Video, "video-main", "video-main"),
			MakeTrack(102, cmn::MediaType::Audio, "audio-main", "audio-main"),
			MakeTrack(201, cmn::MediaType::Video, "video-mobile", "video-mobile"),
		},
		{
			MakePlaylist("master", "master", "video-main", 0, "audio-main", 0),
			MakePlaylist("mobile", "mobile", "video-mobile", 0, "audio-main", 0),
		});
	ASSERT_NE(stream, nullptr);

	Json::Value description;
	ASSERT_TRUE(stream->GetDescription(description));
	EXPECT_EQ(description["version"].asUInt(), OVT_SIGNALING_VERSION);
	ASSERT_TRUE(description["capabilities"].isObject());
	EXPECT_TRUE(description["capabilities"]["runtimeWidening"].asBool());
	EXPECT_EQ(description["stream"]["playlists"].size(), 2U);
	EXPECT_EQ(CollectTrackIdsFromDescription(description), (std::set<int32_t>{101, 102, 201}));
}

TEST(OvtSubscribeHelpers, ParseSubscribeSelectionDefaultsNullToFullStream)
{
	bool full_stream = false;
	std::optional<std::set<int32_t>> track_ids;

	ASSERT_TRUE(ovt_pub::internal::ParseSubscribeSelection(Json::Value(), full_stream, track_ids));
	EXPECT_TRUE(full_stream);
	EXPECT_FALSE(track_ids.has_value());
}

TEST(OvtSubscribeHelpers, ParseSubscribeSelectionReadsTrackIds)
{
	Json::Value contents;
	contents["fullStream"] = false;
	contents["trackIds"].append(102);
	contents["trackIds"].append(201);

	bool full_stream = true;
	std::optional<std::set<int32_t>> track_ids;

	ASSERT_TRUE(ovt_pub::internal::ParseSubscribeSelection(contents, full_stream, track_ids));
	EXPECT_FALSE(full_stream);
	ASSERT_TRUE(track_ids.has_value());
	EXPECT_EQ(*track_ids, (std::set<int32_t>{102, 201}));
}

TEST(OvtSubscribeHelpers, ParseSubscribeSelectionRejectsMissingTrackIdsForScopedSubscribe)
{
	Json::Value contents;
	contents["fullStream"] = false;

	bool full_stream	   = true;
	std::optional<std::set<int32_t>> track_ids;

	EXPECT_FALSE(ovt_pub::internal::ParseSubscribeSelection(contents, full_stream, track_ids));
}

TEST(OvtSubscribeHelpers, ParseSubscribeSelectionRejectsEmptyTrackIdsForScopedSubscribe)
{
	Json::Value contents;
	contents["fullStream"] = false;
	contents["trackIds"]   = Json::Value(Json::arrayValue);

	bool full_stream	   = true;
	std::optional<std::set<int32_t>> track_ids;

	EXPECT_FALSE(ovt_pub::internal::ParseSubscribeSelection(contents, full_stream, track_ids));
}

TEST(OvtSubscribeHelpers, ParseSubscribeSelectionRejectsInvalidTrackIds)
{
	Json::Value contents;
	contents["trackIds"].append("bad");

	bool full_stream = false;
	std::optional<std::set<int32_t>> track_ids;

	EXPECT_FALSE(ovt_pub::internal::ParseSubscribeSelection(contents, full_stream, track_ids));
}

TEST(OvtSubscribeHelpers, ResolveCanonicalSubscribeScopeUsesPlaylistScopeForExactPlaylistTracks)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeStartedOvtStream(
		app_info,
		"stream",
		{
			MakeTrack(101, cmn::MediaType::Video, "video-main", "video-main"),
			MakeTrack(102, cmn::MediaType::Audio, "audio-main", "audio-main"),
			MakeTrack(201, cmn::MediaType::Video, "video-mobile", "video-mobile"),
		},
		{
			MakePlaylist("master", "master", "video-main", 0, "audio-main", 0),
			MakePlaylist("mobile", "mobile", "video-mobile", 0, "audio-main", 0),
		});
	ASSERT_NE(stream, nullptr);

	auto scoped_url = ovt_pub::internal::ResolveCanonicalSubscribeScopeUrl(
		ParseUrl("ovt://origin.example.com/app/stream/mobile?token=abc"),
		stream,
		std::set<int32_t>{102, 201},
		false);

	ASSERT_NE(scoped_url, nullptr);
	EXPECT_EQ(scoped_url->Path(), "/app/stream/mobile");
	EXPECT_EQ(scoped_url->GetQueryValue("token"), "abc");
}

TEST(OvtSubscribeHelpers, ResolveCanonicalSubscribeScopeFallsBackToFullScope)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeStartedOvtStream(
		app_info,
		"stream",
		{
			MakeTrack(101, cmn::MediaType::Video, "video-main", "video-main"),
			MakeTrack(102, cmn::MediaType::Audio, "audio-main", "audio-main"),
			MakeTrack(201, cmn::MediaType::Video, "video-mobile", "video-mobile"),
		},
		{
			MakePlaylist("master", "master", "video-main", 0, "audio-main", 0),
			MakePlaylist("mobile", "mobile", "video-mobile", 0, "audio-main", 0),
		});
	ASSERT_NE(stream, nullptr);

	auto full_scope_url = ovt_pub::internal::ResolveCanonicalSubscribeScopeUrl(
		ParseUrl("ovt://origin.example.com/app/stream/mobile?token=abc"),
		stream,
		std::set<int32_t>{101, 102, 201},
		false);
	ASSERT_NE(full_scope_url, nullptr);
	EXPECT_EQ(full_scope_url->Path(), "/app/stream");

	auto unmatched_scope_url = ovt_pub::internal::ResolveCanonicalSubscribeScopeUrl(
		ParseUrl("ovt://origin.example.com/app/stream/mobile?token=abc"),
		stream,
		std::set<int32_t>{102},
		false);
	EXPECT_EQ(unmatched_scope_url, nullptr);
}

// Regression for B1 (issues.md #1): the requested set may legitimately equal the union
// of multiple registered playlists. The previous "exact single-playlist match" policy
// rejected such requests with 400, forcing supported peers into compatibility-mode
// fallback. Per requirements.md section 4.4 widening MUST be possible from a supported
// peer; the publisher therefore accepts any subset that exactly equals a union of
// registered playlists.
TEST(OvtSubscribeHelpers, ResolveCanonicalSubscribeScopeAcceptsPlaylistUnion)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeStartedOvtStream(
		app_info,
		"stream",
		{
			MakeTrack(101, cmn::MediaType::Video, "video-a", "video-a"),
			MakeTrack(102, cmn::MediaType::Audio, "audio-a", "audio-a"),
			MakeTrack(201, cmn::MediaType::Video, "video-b", "video-b"),
			MakeTrack(202, cmn::MediaType::Audio, "audio-b", "audio-b"),
			MakeTrack(301, cmn::MediaType::Data, "data-extra", "data-extra"),
		},
		{
			// Two disjoint playlists. Their union covers neither the full track set
			// (track 301 is excluded) nor any single playlist alone.
			MakePlaylist("p1", "p1", "video-a", 0, "audio-a", 0),
			MakePlaylist("p2", "p2", "video-b", 0, "audio-b", 0),
		});
	ASSERT_NE(stream, nullptr);

	// Union of p1 union p2 must be accepted. URL falls back to the stream-level (full) URL
	// because no single playlist file fits the union.
	auto union_scope_url = ovt_pub::internal::ResolveCanonicalSubscribeScopeUrl(
		ParseUrl("ovt://origin.example.com/app/stream"),
		stream,
		std::set<int32_t>{101, 102, 201, 202},
		false);
	ASSERT_NE(union_scope_url, nullptr);
	EXPECT_EQ(union_scope_url->Path(), "/app/stream");

	// An arbitrary subset that does NOT equal any union of registered playlists must
	// still be rejected - accepting that would let a malicious or buggy peer subscribe
	// to track sets that no exposed playlist describes.
	auto arbitrary_subset_url = ovt_pub::internal::ResolveCanonicalSubscribeScopeUrl(
		ParseUrl("ovt://origin.example.com/app/stream"),
		stream,
		std::set<int32_t>{101, 201},
		false);
	EXPECT_EQ(arbitrary_subset_url, nullptr);
}

TEST(OvtSessionFiltering, SharedFullAndPlaylistSessionsReceiveOnlyAllowedTracks)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeStartedOvtStream(
		app_info,
		"stream",
		{
			MakeTrack(101, cmn::MediaType::Video, "video-main", "video-main"),
			MakeTrack(102, cmn::MediaType::Audio, "audio-main", "audio-main"),
			MakeTrack(201, cmn::MediaType::Video, "video-mobile", "video-mobile"),
		},
		{
			MakePlaylist("master", "master", "video-main", 0, "audio-main", 0),
			MakePlaylist("mobile", "mobile", "video-mobile", 0, "audio-main", 0),
		});
	ASSERT_NE(stream, nullptr);

	std::set<int32_t> mobile_track_ids;
	ASSERT_TRUE(stream->ResolveTrackIdsForPlaylist("mobile", mobile_track_ids));
	EXPECT_EQ(mobile_track_ids, (std::set<int32_t>{102, 201}));

	auto full_session	= MakeStartedRecordingSession(stream, 1, std::nullopt);
	auto mobile_session = MakeStartedRecordingSession(stream, 2, mobile_track_ids);
	ASSERT_NE(full_session, nullptr);
	ASSERT_NE(mobile_session, nullptr);
	ASSERT_TRUE(stream->AddSession(full_session));
	ASSERT_TRUE(stream->AddSession(mobile_session));

	Json::Value full_description;
	Json::Value mobile_description;
	ASSERT_TRUE(stream->GetDescription(full_description));
	ASSERT_TRUE(stream->GetDescription(mobile_description, "mobile"));
	EXPECT_EQ(CollectTrackIdsFromDescription(full_description), (std::set<int32_t>{101, 102, 201}));
	EXPECT_EQ(CollectTrackIdsFromDescription(mobile_description), (std::set<int32_t>{102, 201}));

	EXPECT_TRUE(stream->BroadcastPacket(MakeMediaOvtPacket(101, true, 1)));
	EXPECT_TRUE(stream->BroadcastPacket(MakeMediaOvtPacket(101, false, 2)));
	EXPECT_TRUE(stream->BroadcastPacket(MakeMediaOvtPacket(101, true, 3)));
	EXPECT_TRUE(stream->BroadcastPacket(MakeMediaOvtPacket(102, false, 4)));
	EXPECT_TRUE(stream->BroadcastPacket(MakeMediaOvtPacket(201, true, 5)));

	EXPECT_EQ(full_session->ForwardedTrackIds(), (std::vector<uint32_t>{101, 101, 102, 201}));
	EXPECT_EQ(mobile_session->ForwardedTrackIds(), (std::vector<uint32_t>{102, 201}));
}

namespace
{
	// Build a fully-started session pre-synced past the initial-marker gate so that
	// subsequent BroadcastPacket calls feed straight into the filter's per-fragment
	// decision logic.
	std::shared_ptr<RecordingOvtSession> MakeSyncedSession(const std::shared_ptr<OvtStream> &stream,
														   uint32_t session_id,
														   const std::optional<std::set<int32_t>> &allowed_track_ids)
	{
		auto session = MakeStartedRecordingSession(stream, session_id, allowed_track_ids);
		if (session == nullptr)
		{
			return nullptr;
		}

		if (stream->AddSession(session) == false)
		{
			ADD_FAILURE() << "Failed to attach recording OVT session";
			return nullptr;
		}

		// First marker packet: consumed by `_sent_ready` gate, dropped, never forwarded.
		EXPECT_TRUE(stream->BroadcastPacket(MakeMediaOvtPacket(102, true, 0)));
		return session;
	}

	std::shared_ptr<OvtStream> MakeFilterTestStream(const std::shared_ptr<info::Application> &app_info)
	{
		return MakeStartedOvtStream(
			app_info,
			"stream",
			{
				MakeTrack(101, cmn::MediaType::Video, "video-main", "video-main"),
				MakeTrack(102, cmn::MediaType::Audio, "audio-main", "audio-main"),
				MakeTrack(201, cmn::MediaType::Video, "video-mobile", "video-mobile"),
			},
			{
				MakePlaylist("master", "master", "video-main", 0, "audio-main", 0),
				MakePlaylist("mobile", "mobile", "video-mobile", 0, "audio-main", 0),
			});
	}
}  // namespace

TEST(OvtSessionFiltering, WideningKeepsInFlightAllowedFragmentFlowing)
{
	// Old=mobile {102,201}. A track-102 fragment is in flight (forwarded). Widen to full
	// {101,102,201}; the in-flight fragment must continue without a marker freeze.
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeFilterTestStream(app_info);
	ASSERT_NE(stream, nullptr);

	auto session = MakeSyncedSession(stream, 1, std::set<int32_t>{102, 201});
	ASSERT_NE(session, nullptr);

	// Allowed track 102 fragment starts.
	EXPECT_TRUE(stream->BroadcastPacket(MakeMediaOvtPacket(102, false, 1)));
	EXPECT_TRUE(stream->BroadcastPacket(MakeMediaOvtPacket(102, false, 2)));

	// Widen scope.
	EXPECT_TRUE(session->UpdateAllowedTrackIds(std::set<int32_t>{101, 102, 201}));

	// Remaining packets of the same fragment continue to flow (no marker freeze).
	EXPECT_TRUE(stream->BroadcastPacket(MakeMediaOvtPacket(102, false, 3)));
	EXPECT_TRUE(stream->BroadcastPacket(MakeMediaOvtPacket(102, true, 4)));

	EXPECT_EQ(session->ForwardedTrackIds(), (std::vector<uint32_t>{102, 102, 102, 102}));
}

TEST(OvtSessionFiltering, WideningReleasesPreviouslyDroppedFragmentForNewlyAllowedTrack)
{
	// Old={102,201}. A track-101 fragment is in flight (was being dropped). Widen to
	// {101,102,201}; the *cached* track id from the fragment's first packet is re-evaluated
	// and the rest of the fragment must start flowing immediately (not wait for marker).
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeFilterTestStream(app_info);
	ASSERT_NE(stream, nullptr);

	auto session = MakeSyncedSession(stream, 1, std::set<int32_t>{102, 201});
	ASSERT_NE(session, nullptr);

	// First packet of track-101 fragment lands while filter forbids 101 -- drop.
	EXPECT_TRUE(stream->BroadcastPacket(MakeMediaOvtPacket(101, false, 1)));

	// Widen.
	EXPECT_TRUE(session->UpdateAllowedTrackIds(std::set<int32_t>{101, 102, 201}));

	// Same fragment continues; the cached-track-id re-evaluation flips the decision to
	// "allowed" so 2..4 flow.
	EXPECT_TRUE(stream->BroadcastPacket(MakeMediaOvtPacket(101, false, 2)));
	EXPECT_TRUE(stream->BroadcastPacket(MakeMediaOvtPacket(101, false, 3)));
	EXPECT_TRUE(stream->BroadcastPacket(MakeMediaOvtPacket(101, true, 4)));

	EXPECT_EQ(session->ForwardedTrackIds(), (std::vector<uint32_t>{101, 101, 101}));
}

TEST(OvtSessionFiltering, WideningToFullStreamSkipsResync)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeFilterTestStream(app_info);
	ASSERT_NE(stream, nullptr);

	auto session = MakeSyncedSession(stream, 1, std::set<int32_t>{102});
	ASSERT_NE(session, nullptr);

	// Track 102 fragment in flight (forwarded).
	EXPECT_TRUE(stream->BroadcastPacket(MakeMediaOvtPacket(102, false, 1)));
	// Widen to full stream (nullopt).
	EXPECT_TRUE(session->UpdateAllowedTrackIds(std::nullopt));
	// In-flight fragment continues, and a previously-disallowed track 101 fragment that
	// kicks in *after* a marker boundary is also accepted by the new (full) filter.
	EXPECT_TRUE(stream->BroadcastPacket(MakeMediaOvtPacket(102, true, 2)));
	EXPECT_TRUE(stream->BroadcastPacket(MakeMediaOvtPacket(101, false, 3)));
	EXPECT_TRUE(stream->BroadcastPacket(MakeMediaOvtPacket(101, true, 4)));

	EXPECT_EQ(session->ForwardedTrackIds(), (std::vector<uint32_t>{102, 102, 101, 101}));
}

TEST(OvtSessionFiltering, NarrowingResyncsToNextMarkerAndDropsInFlightFragment)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeFilterTestStream(app_info);
	ASSERT_NE(stream, nullptr);

	// Start with full stream.
	auto session = MakeSyncedSession(stream, 1, std::nullopt);
	ASSERT_NE(session, nullptr);

	// Track 101 fragment starts and flows under the full filter.
	EXPECT_TRUE(stream->BroadcastPacket(MakeMediaOvtPacket(101, false, 1)));

	// Narrow scope: previously-allowed mid-fragment 101 must now be dropped, and the
	// session must wait for the next marker before resuming forwarding.
	EXPECT_TRUE(session->UpdateAllowedTrackIds(std::set<int32_t>{102}));

	// Remaining 101 fragment is dropped (inside resync window).
	EXPECT_TRUE(stream->BroadcastPacket(MakeMediaOvtPacket(101, false, 2)));
	EXPECT_TRUE(stream->BroadcastPacket(MakeMediaOvtPacket(101, true, 3)));	 // marker -> resync done

	// New fragment after marker: 101 dropped (filter excludes it), 102 allowed.
	EXPECT_TRUE(stream->BroadcastPacket(MakeMediaOvtPacket(101, true, 4)));
	EXPECT_TRUE(stream->BroadcastPacket(MakeMediaOvtPacket(102, true, 5)));

	// Only the *first* 101 packet flowed (before narrowing) and the 102 marker after
	// resync. The rest are dropped.
	EXPECT_EQ(session->ForwardedTrackIds(), (std::vector<uint32_t>{101, 102}));
}

TEST(OvtSessionFiltering, NarrowingFromFullStreamResyncsEvenWhenNewScopeOverlaps)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeFilterTestStream(app_info);
	ASSERT_NE(stream, nullptr);

	auto session = MakeSyncedSession(stream, 1, std::nullopt);
	ASSERT_NE(session, nullptr);

	// Track 102 (will remain allowed) fragment starts.
	EXPECT_TRUE(stream->BroadcastPacket(MakeMediaOvtPacket(102, false, 1)));
	// Narrow from full to {102, 201}. Even though 102 is still allowed, full -> set is a
	// non-superset transition (full is "wider than any set"), so the filter must resync.
	EXPECT_TRUE(session->UpdateAllowedTrackIds(std::set<int32_t>{102, 201}));

	// Mid-fragment 102 is dropped during resync window.
	EXPECT_TRUE(stream->BroadcastPacket(MakeMediaOvtPacket(102, true, 2)));	  // marker
	EXPECT_TRUE(stream->BroadcastPacket(MakeMediaOvtPacket(102, false, 3)));  // new fragment, allowed

	EXPECT_EQ(session->ForwardedTrackIds(), (std::vector<uint32_t>{102, 102}));
}

TEST(OvtSessionFiltering, UnchangedScopeDoesNotResync)
{
	auto app_info = std::make_shared<TestApplicationInfo>("default", "app");
	auto stream	  = MakeFilterTestStream(app_info);
	ASSERT_NE(stream, nullptr);

	auto session = MakeSyncedSession(stream, 1, std::set<int32_t>{102, 201});
	ASSERT_NE(session, nullptr);

	EXPECT_TRUE(stream->BroadcastPacket(MakeMediaOvtPacket(102, false, 1)));
	EXPECT_TRUE(session->UpdateAllowedTrackIds(std::set<int32_t>{102, 201}));  // identical
	EXPECT_TRUE(stream->BroadcastPacket(MakeMediaOvtPacket(102, true, 2)));

	EXPECT_EQ(session->ForwardedTrackIds(), (std::vector<uint32_t>{102, 102}));
}

// =============================================================================
// Publisher <-> Provider integration tests
//
// Until this fixture, every test in this file exercised either the publisher or
// the provider in isolation. The B1 follow-up regression (issues.md re-review B1)
// slipped through precisely because no test crossed the seam where a publisher
// session's scope is propagated to the linked input provider stream's shared
// request state. These tests fill that gap by:
//   1. Setting up a publisher OvtStream + RecordingOvtSession with a filtered
//      track set.
//   2. Setting up a provider OvtStream as the linked input.
//   3. Calling pub::Stream::RefreshSessionScope() to drive the production code
//      path (RegisterSessionScopeOnLinkedInputStream -> provider's
//      RegisterDownstreamSession -> TryAccumulateActiveRequestScopeLocked).
//   4. Asserting on the provider's _shared_request_state.
// =============================================================================

namespace
{
	std::shared_ptr<pvd::OvtStream> MakeIntegrationProviderStream(const std::shared_ptr<info::Application> &app_info,
																  const ov::String &stream_name,
																  const std::vector<std::shared_ptr<MediaTrack>> &tracks,
																  const std::vector<std::shared_ptr<const info::Playlist>> &playlists)
	{
		info::Stream provider_stream_info(*app_info, StreamSourceType::Ovt);
		provider_stream_info.SetName(stream_name);
		for (const auto &track : tracks)
		{
			provider_stream_info.AddTrack(track);
		}
		for (const auto &playlist : playlists)
		{
			provider_stream_info.AddPlaylist(playlist);
		}

		auto provider_stream = std::make_shared<pvd::OvtStream>(
			nullptr,
			provider_stream_info,
			std::vector<ov::String>{"ovt://origin/app/stream"},
			nullptr);
		provider_stream->_inventory_snapshot_state = pvd::OvtStream::InventorySnapshotState::FULL_SNAPSHOT;
		return provider_stream;
	}
}  // namespace

TEST(PublisherProviderIntegration, UnionSubscribeDoesNotInflateFullRequestCount)
{
	// Regression for B1 follow-up (issues.md re-review B1): when an OvtPublisher session
	// has accepted a runtime-`subscribe` covering a union of multiple playlists, the
	// shared request state on the linked input provider must record a track-level
	// (union) demand, NOT a full-stream demand. Before the fix, the publisher returned
	// a stream-level URL for the union, which the provider's URL-based inference
	// silently misclassified as full -> `full_request_count++`.
	auto app_info									= std::make_shared<TestApplicationInfo>("default", "app");

	std::vector<std::shared_ptr<MediaTrack>> tracks = {
		MakeTrack(101, cmn::MediaType::Video, "video-a", "video-a"),
		MakeTrack(102, cmn::MediaType::Audio, "audio-a", "audio-a"),
		MakeTrack(201, cmn::MediaType::Video, "video-b", "video-b"),
		MakeTrack(202, cmn::MediaType::Audio, "audio-b", "audio-b"),
	};
	std::vector<std::shared_ptr<const info::Playlist>> playlists = {
		MakePlaylist("p1", "p1", "video-a", 0, "audio-a", 0),
		MakePlaylist("p2", "p2", "video-b", 0, "audio-b", 0),
	};

	auto publisher_stream = MakeStartedOvtStream(app_info, "stream", tracks, playlists);
	ASSERT_NE(publisher_stream, nullptr);

	auto provider_stream = MakeIntegrationProviderStream(app_info, "stream", tracks, playlists);
	ASSERT_NE(provider_stream, nullptr);

	publisher_stream->LinkInputStream(provider_stream);

	// Session has the *union* of p1 and p2 as its allowed track set, but its decorative
	// URL is the stream-level URL - exactly the shape produced by
	// `ResolveCanonicalSubscribeScopeUrl` for a multi-playlist union.
	auto session = MakeStartedRecordingSession(publisher_stream, 1, std::set<int32_t>{101, 102, 201, 202});
	ASSERT_NE(session, nullptr);
	session->SetRequestedUrl(ov::Url::Parse("ovt://origin/app/stream"));
	session->SetFinalUrl(ov::Url::Parse("ovt://origin/app/stream"));

	// Add the session to the publisher stream's `_sessions` map so RefreshSessionScope
	// (which now requires session membership per B3-1 fix) does not bail out.
	ASSERT_TRUE(publisher_stream->AddSession(session));

	// Pre-arm the sticky compat flags that a real Branch 1 entry would have set; the
	// follow-up regression check below verifies they are NOT silently cleared by
	// Branch 2's "no demand" path.
	provider_stream->_compatibility_full_stream_mode_requested = true;
	provider_stream->_compatibility_fallback_restart_pending   = true;

	// Drive the production seam (a real subscribe handler would call RefreshSessionScope
	// after UpdateAllowedTrackIds + StoreRequestScopeOnSession; here AddSession already
	// registered, so this is the re-register that exercises the same seam).
	publisher_stream->RefreshSessionScope(session);

	// (a) Provider must NOT have counted this as a full-stream demand.
	EXPECT_FALSE(provider_stream->_shared_request_state.target_requires_full_stream);
	EXPECT_EQ(provider_stream->_shared_request_state.full_request_count, 0U);
	// (b) Track-level demand reflects the union exactly.
	EXPECT_EQ(provider_stream->_shared_request_state.resolved_target_track_ids,
			  (std::set<int32_t>{101, 102, 201, 202}));
	// (c) No misleading `playlist_request_counts` entry - the union spans two playlists,
	// neither of which alone represents the demand, and the URL has no file segment.
	EXPECT_TRUE(provider_stream->_shared_request_state.playlist_request_counts.empty());
	// (d) **Critical**: a Path-A-only session must still be recognised as positive
	// demand. If `has_target_demand` was computed from `target_requires_full_stream`
	// and `playlist_request_counts` alone, RecalculateActiveRequestStateLocked would
	// take the Branch 2 ("no demand") path and silently CLEAR `_compatibility_full_stream_mode_requested`
	// (because no remembered playlist reprobe scope is set) - that is the regression
	// the bug-hunter caught.
	//
	// With the fix, `has_target_demand=true` (from non-empty track_ref_counts) -> Branch
	// 2 is NOT entered. Sticky compat mode survives recalc. Branch 5 (sticky + active
	// demand + non-playlist-scoped upstream) then runs and legitimately clears the
	// pending-restart flag because the restart has effectively been absorbed by the
	// new demand state - that's the documented Branch 5 contract, so we don't assert
	// on `_compatibility_fallback_restart_pending` here.
	EXPECT_TRUE(provider_stream->_compatibility_full_stream_mode_requested)
		<< "Branch 2 (no-demand cleanup) ran even though the union session is active demand";
}

TEST(PublisherProviderIntegration, FullStreamSessionStillCountsAsFullDemand)
{
	// Negative control: a session with `_allowed_track_ids = nullopt` (truly full
	// stream - no filter) and a stream-level URL must still be classified as a
	// full-stream demand. The B1 follow-up fix narrows the misclassification path to
	// only sessions that expose an *explicit* track set hint via
	// `GetAuthoritativeAllowedTrackIds()`. Without that hint, URL-based inference is
	// the only signal, and a stream-level URL legitimately means "I want the full
	// stream".
	auto app_info									= std::make_shared<TestApplicationInfo>("default", "app");

	std::vector<std::shared_ptr<MediaTrack>> tracks = {
		MakeTrack(101, cmn::MediaType::Video, "video", "video"),
		MakeTrack(102, cmn::MediaType::Audio, "audio", "audio"),
	};
	std::vector<std::shared_ptr<const info::Playlist>> playlists = {
		MakePlaylist("master", "master", "video", 0, "audio", 0),
	};

	auto publisher_stream = MakeStartedOvtStream(app_info, "stream", tracks, playlists);
	ASSERT_NE(publisher_stream, nullptr);

	auto provider_stream = MakeIntegrationProviderStream(app_info, "stream", tracks, playlists);
	ASSERT_NE(provider_stream, nullptr);
	publisher_stream->LinkInputStream(provider_stream);

	// nullopt = no filter = true full-stream session.
	auto session = MakeStartedRecordingSession(publisher_stream, 1, std::nullopt);
	ASSERT_NE(session, nullptr);
	session->SetRequestedUrl(ov::Url::Parse("ovt://origin/app/stream"));
	session->SetFinalUrl(ov::Url::Parse("ovt://origin/app/stream"));

	ASSERT_TRUE(publisher_stream->AddSession(session));
	publisher_stream->RefreshSessionScope(session);

	EXPECT_TRUE(provider_stream->_shared_request_state.target_requires_full_stream);
	EXPECT_EQ(provider_stream->_shared_request_state.full_request_count, 1U);
}

TEST(PublisherProviderIntegration, SinglePlaylistSessionPropagatesPlaylistRequestCount)
{
	// A session whose track set matches exactly one registered playlist must produce
	// a `playlist_request_counts[<file>]++` entry on the provider so reconnect
	// heuristics (`TrySelectRepresentativePlaylistForReconnectLocked`) can pick that
	// playlist URL on subsequent failover. The session URL carries the playlist file
	// (publisher's `BuildPlaylistScopeUrl` path), and the explicit track set hint
	// agrees with it.
	auto app_info									= std::make_shared<TestApplicationInfo>("default", "app");

	std::vector<std::shared_ptr<MediaTrack>> tracks = {
		MakeTrack(101, cmn::MediaType::Video, "video-main", "video-main"),
		MakeTrack(102, cmn::MediaType::Audio, "audio-main", "audio-main"),
		MakeTrack(201, cmn::MediaType::Video, "video-mobile", "video-mobile"),
	};
	std::vector<std::shared_ptr<const info::Playlist>> playlists = {
		MakePlaylist("master", "master", "video-main", 0, "audio-main", 0),
		MakePlaylist("mobile", "mobile", "video-mobile", 0, "audio-main", 0),
	};

	auto publisher_stream = MakeStartedOvtStream(app_info, "stream", tracks, playlists);
	ASSERT_NE(publisher_stream, nullptr);
	auto provider_stream = MakeIntegrationProviderStream(app_info, "stream", tracks, playlists);
	ASSERT_NE(provider_stream, nullptr);
	publisher_stream->LinkInputStream(provider_stream);

	auto session = MakeStartedRecordingSession(publisher_stream, 1, std::set<int32_t>{102, 201});
	ASSERT_NE(session, nullptr);
	session->SetRequestedUrl(ov::Url::Parse("ovt://origin/app/stream/mobile"));
	session->SetFinalUrl(ov::Url::Parse("ovt://origin/app/stream/mobile"));

	ASSERT_TRUE(publisher_stream->AddSession(session));
	publisher_stream->RefreshSessionScope(session);

	EXPECT_FALSE(provider_stream->_shared_request_state.target_requires_full_stream);
	EXPECT_EQ(provider_stream->_shared_request_state.full_request_count, 0U);
	EXPECT_EQ(provider_stream->_shared_request_state.resolved_target_track_ids,
			  (std::set<int32_t>{102, 201}));
	ASSERT_EQ(provider_stream->_shared_request_state.playlist_request_counts.count("mobile"), 1U);
	EXPECT_EQ(provider_stream->_shared_request_state.playlist_request_counts.at("mobile"), 1U);
}

// Regression for B3-1 (issues.md re-review B3-1): RefreshSessionScope must not
// resurrect demand for a session that has already been removed from the
// publisher's `_sessions` map. The earlier "lock-free callback" pattern in
// AddSession/RemoveSession was extended in round 3, but RefreshSessionScope was
// left calling the cross-module Register hook directly without any membership
// check, leaving a window where a concurrent RemoveSession + Unregister could be
// followed by RefreshSessionScope's stray Register, leaking a zombie demand
// entry on the provider.
TEST(PublisherProviderIntegration, RefreshSessionScopeSkipsRemovedSession)
{
	auto app_info									= std::make_shared<TestApplicationInfo>("default", "app");

	std::vector<std::shared_ptr<MediaTrack>> tracks = {
		MakeTrack(101, cmn::MediaType::Video, "video", "video"),
		MakeTrack(102, cmn::MediaType::Audio, "audio", "audio"),
	};
	std::vector<std::shared_ptr<const info::Playlist>> playlists = {
		MakePlaylist("master", "master", "video", 0, "audio", 0),
	};

	auto publisher_stream = MakeStartedOvtStream(app_info, "stream", tracks, playlists);
	ASSERT_NE(publisher_stream, nullptr);
	auto provider_stream = MakeIntegrationProviderStream(app_info, "stream", tracks, playlists);
	ASSERT_NE(provider_stream, nullptr);
	publisher_stream->LinkInputStream(provider_stream);

	auto session = MakeStartedRecordingSession(publisher_stream, 1, std::set<int32_t>{101, 102});
	ASSERT_NE(session, nullptr);
	session->SetRequestedUrl(ov::Url::Parse("ovt://origin/app/stream/master"));
	session->SetFinalUrl(ov::Url::Parse("ovt://origin/app/stream/master"));

	// (a) Normal flow: AddSession registers the demand on the provider.
	ASSERT_TRUE(publisher_stream->AddSession(session));
	EXPECT_EQ(provider_stream->_active_request_sessions.size(), 1U);

	// (b) Simulate a concurrent disconnect: RemoveSession runs to completion,
	// erasing the session from `_sessions` and unregistering the provider entry.
	ASSERT_TRUE(publisher_stream->RemoveSession(session->GetId()));
	EXPECT_EQ(provider_stream->_active_request_sessions.size(), 0U);

	// (c) The subscribe handler that was racing with RemoveSession still calls
	// RefreshSessionScope on its (now-removed) session. With the fix the call
	// must be a no-op - no zombie entry is created on the provider.
	publisher_stream->RefreshSessionScope(session);
	EXPECT_EQ(provider_stream->_active_request_sessions.size(), 0U)
		<< "RefreshSessionScope resurrected demand for an already-removed session";
}
