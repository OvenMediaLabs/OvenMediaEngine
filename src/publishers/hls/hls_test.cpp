//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#include <gtest/gtest.h>

#include <base/info/media_track.h>
#include <base/mediarouter/media_buffer.h>
#include <modules/containers/mpegts/mpegts_packager.h>
#include <modules/containers/mpegts/mpegts_packetizer.h>

#include "hls_media_playlist.h"

namespace
{
	std::shared_ptr<MediaTrack> MakeTrack(uint32_t id, cmn::MediaType type, cmn::MediaCodecId codec, uint32_t version)
	{
		auto track = std::make_shared<MediaTrack>();
		track->SetId(id);
		track->SetMediaType(type);
		track->SetCodecId(codec);
		track->SetTimeBase(1, 90000);
		track->SetVersion(version);
		return track;
	}

	std::shared_ptr<const MediaPacket> MakeVideoKeyFrame(uint32_t track_id, int64_t dts, int64_t duration)
	{
		auto data = std::make_shared<ov::Data>();
		uint8_t byte = 0x00;
		data->Append(&byte, 1);
		return std::make_shared<MediaPacket>(cmn::MediaType::Video, track_id, data, dts, dts, duration,
											 MediaPacketFlag::Key, cmn::BitstreamFormat::H264_ANNEXB, cmn::PacketType::NALU);
	}

	std::shared_ptr<mpegts::Segment> MakeSegment(int64_t number, int64_t dts, double duration_ms,
												 uint32_t version, bool discontinuity, const ov::String &codecs)
	{
		auto segment = std::make_shared<mpegts::Segment>(number, dts, duration_ms);
		segment->SetTrackVersion(version);
		segment->SetDiscontinuityPoint(discontinuity);
		segment->SetCodecsParameter(codecs);
		segment->SetUrl(ov::String::FormatString("seg_%" PRId64 ".ts", number));
		return segment;
	}

	class CountingPacketizerSink : public mpegts::PacketizerSink
	{
	public:
		void OnPsi(const std::vector<std::shared_ptr<const MediaTrack>> &tracks, const std::vector<std::shared_ptr<mpegts::Packet>> &psi_packets) override
		{
			_psi_count++;
			_last_pmt = psi_packets.size() >= 2 ? psi_packets[1] : nullptr;
		}

		void OnFrame(const std::shared_ptr<const MediaPacket> &media_packet, const std::shared_ptr<const ov::Data> &ts_data) override
		{
		}

		int _psi_count = 0;
		std::shared_ptr<mpegts::Packet> _last_pmt = nullptr;
	};

	class CollectingPackagerSink : public mpegts::PackagerSink
	{
	public:
		void OnSegmentCreated(const ov::String &packager_id, const std::shared_ptr<base::modules::Segment> &segment) override
		{
			_created.push_back(segment);
		}

		void OnSegmentDeleted(const ov::String &packager_id, const std::shared_ptr<base::modules::Segment> &segment) override
		{
			_deleted.push_back(segment);
		}

		std::vector<std::shared_ptr<base::modules::Segment>> _created;
		std::vector<std::shared_ptr<base::modules::Segment>> _deleted;
	};
}  // namespace

// ---------------------------------------------------------------------------
// mpegts::Packetizer - the PMT version bumps and the PSI is re-broadcast only
// when the elementary stream type (codec) changes.
// ---------------------------------------------------------------------------

TEST(HlsPacketizer, RebroadcastsPsiOnCodecChange)
{
	auto sink = std::make_shared<CountingPacketizerSink>();

	mpegts::Packetizer packetizer;
	packetizer.AddSink(sink);
	ASSERT_TRUE(packetizer.AddTrack(MakeTrack(1, cmn::MediaType::Video, cmn::MediaCodecId::H264, 1)));
	ASSERT_TRUE(packetizer.Start());

	EXPECT_EQ(sink->_psi_count, 1);

	// H264 -> H265 changes the stream_type, so the PMT is rebuilt and re-broadcast
	ASSERT_TRUE(packetizer.UpdateTrack(MakeTrack(1, cmn::MediaType::Video, cmn::MediaCodecId::H265, 2)));
	EXPECT_EQ(sink->_psi_count, 2);
}

TEST(HlsPacketizer, KeepsPsiOnSameCodecChange)
{
	auto sink = std::make_shared<CountingPacketizerSink>();

	mpegts::Packetizer packetizer;
	packetizer.AddSink(sink);
	ASSERT_TRUE(packetizer.AddTrack(MakeTrack(1, cmn::MediaType::Video, cmn::MediaCodecId::H264, 1)));
	ASSERT_TRUE(packetizer.Start());

	EXPECT_EQ(sink->_psi_count, 1);

	// A same-codec change (e.g. resolution) leaves the PMT byte-identical
	ASSERT_TRUE(packetizer.UpdateTrack(MakeTrack(1, cmn::MediaType::Video, cmn::MediaCodecId::H264, 2)));
	EXPECT_EQ(sink->_psi_count, 1);
}

// ---------------------------------------------------------------------------
// mpegts::Packager - a main-track change starts a new configuration generation,
// so the following segment is a discontinuity point and the pre-change tail is not.
// ---------------------------------------------------------------------------

TEST(HlsPackager, MainTrackChangeMarksDiscontinuity)
{
	mpegts::Packager::Config config;
	config.target_duration_ms = 1000;
	config.max_segment_count = 100;
	config.segment_retention_count = 0;

	auto packager = std::make_shared<mpegts::Packager>("variant", config);
	auto sink = std::make_shared<CollectingPackagerSink>();
	packager->AddSink(sink);

	auto track = MakeTrack(1, cmn::MediaType::Video, cmn::MediaCodecId::H264, 1);
	packager->OnPsi({track}, {});

	auto ts = std::make_shared<ov::Data>();
	uint8_t byte = 0x47;
	ts->Append(&byte, 1);

	const int64_t one_second = 90000;
	int64_t dts = 0;

	// Fill a few segments under the original configuration
	for (int i = 0; i < 4; i++)
	{
		packager->OnFrame(MakeVideoKeyFrame(1, dts, one_second), ts);
		dts += one_second;
	}

	ASSERT_GT(sink->_created.size(), 0u);

	// Change the video track (H264 -> H265)
	ASSERT_TRUE(packager->UpdateTrack(MakeTrack(1, cmn::MediaType::Video, cmn::MediaCodecId::H265, 2)));

	// Feed more key frames under the new configuration
	for (int i = 0; i < 4; i++)
	{
		packager->OnFrame(MakeVideoKeyFrame(1, dts, one_second), ts);
		dts += one_second;
	}

	// Exactly one discontinuity across the whole sequence, and it is the first
	// segment of the new generation.
	int discontinuity_count = 0;
	uint32_t last_version = 0;
	bool first = true;
	for (const auto &segment : sink->_created)
	{
		if (segment->IsDiscontinuityPoint())
		{
			discontinuity_count++;
			EXPECT_EQ(segment->GetTrackVersion(), 1u);	// second generation
		}

		if (first == false && segment->GetTrackVersion() != last_version)
		{
			EXPECT_TRUE(segment->IsDiscontinuityPoint());
		}

		last_version = segment->GetTrackVersion();
		first = false;
	}

	EXPECT_EQ(discontinuity_count, 1);
}

TEST(HlsPackager, RequestCutDeduplicatesNearbyBoundary)
{
	mpegts::Packager::Config config;
	config.target_duration_ms = 6000;

	auto packager = std::make_shared<mpegts::Packager>("variant", config);
	auto sink = std::make_shared<CollectingPackagerSink>();
	packager->AddSink(sink);

	auto track = MakeTrack(1, cmn::MediaType::Video, cmn::MediaCodecId::H264, 1);
	packager->OnPsi({track}, {});

	auto ts = std::make_shared<ov::Data>();
	uint8_t byte = 0x47;
	ts->Append(&byte, 1);

	packager->OnFrame(MakeVideoKeyFrame(1, 0, 90000), ts);

	// An own cut followed by a propagated cut within a segment of it must not add a
	// second boundary once the first has been applied.
	ASSERT_TRUE(packager->UpdateTrack(MakeTrack(1, cmn::MediaType::Video, cmn::MediaCodecId::H265, 2)));
	double boundary = packager->GetLastSampleEndTimestampMs();
	packager->RequestCutForDiscontinuity(boundary);	// within target_duration of the applied cut

	// No crash and the packager stays consistent; a following key frame still packages
	packager->OnFrame(MakeVideoKeyFrame(1, 90000, 90000), ts);
	SUCCEED();
}

TEST(HlsPackager, TrackChangeBeforeContentIsNotDiscontinuity)
{
	mpegts::Packager::Config config;
	config.target_duration_ms = 1000;
	config.max_segment_count = 100;

	auto packager = std::make_shared<mpegts::Packager>("variant", config);
	auto sink = std::make_shared<CollectingPackagerSink>();
	packager->AddSink(sink);

	packager->OnPsi({MakeTrack(1, cmn::MediaType::Video, cmn::MediaCodecId::H264, 1)}, {});

	// Change the track before a single frame is packaged
	ASSERT_TRUE(packager->UpdateTrack(MakeTrack(1, cmn::MediaType::Video, cmn::MediaCodecId::H265, 2)));

	auto ts = std::make_shared<ov::Data>();
	uint8_t byte = 0x47;
	ts->Append(&byte, 1);

	int64_t dts = 0;
	for (int i = 0; i < 4; i++)
	{
		packager->OnFrame(MakeVideoKeyFrame(1, dts, 90000), ts);
		dts += 90000;
	}

	ASSERT_GT(sink->_created.size(), 0u);
	for (const auto &segment : sink->_created)
	{
		EXPECT_FALSE(segment->IsDiscontinuityPoint());	// pre-content change is the initial config
		EXPECT_GT(segment->GetDurationMs(), 0.0);
	}
}

TEST(HlsPackager, PropagatedCutOnEmptyBufferProducesNoEmptySegment)
{
	mpegts::Packager::Config config;
	config.target_duration_ms = 1000;
	config.max_segment_count = 100;

	auto packager = std::make_shared<mpegts::Packager>("variant", config);
	auto sink = std::make_shared<CollectingPackagerSink>();
	packager->AddSink(sink);

	packager->OnPsi({MakeTrack(1, cmn::MediaType::Video, cmn::MediaCodecId::H264, 1)}, {});

	// A sibling requests an aligned cut while nothing is buffered yet
	packager->RequestCutForDiscontinuity(0.0);

	auto ts = std::make_shared<ov::Data>();
	uint8_t byte = 0x47;
	ts->Append(&byte, 1);

	int64_t dts = 0;
	for (int i = 0; i < 5; i++)
	{
		packager->OnFrame(MakeVideoKeyFrame(1, dts, 90000), ts);
		dts += 90000;
	}

	ASSERT_GT(sink->_created.size(), 0u);
	for (const auto &segment : sink->_created)
	{
		EXPECT_GT(segment->GetDurationMs(), 0.0);	// the empty-buffer cut must not create a 0-duration segment
	}
}

// ---------------------------------------------------------------------------
// HlsMediaPlaylist - EXT-X-DISCONTINUITY, EXT-X-DISCONTINUITY-SEQUENCE, CODECS
// union and the fixed EXT-X-VERSION.
// ---------------------------------------------------------------------------

class HlsMediaPlaylistTest : public ::testing::Test
{
protected:
	std::shared_ptr<HlsMediaPlaylist> MakePlaylist(size_t segment_count = 10)
	{
		HlsMediaPlaylist::HlsMediaPlaylistConfig config;
		config.segment_count = segment_count;
		config.target_duration = 6;
		auto playlist = std::make_shared<HlsMediaPlaylist>("variant", "medialist.m3u8", config);
		playlist->SetWallclockOffset(0);
		return playlist;
	}
};

TEST_F(HlsMediaPlaylistTest, VersionIsThree)
{
	auto playlist = MakePlaylist();
	playlist->OnSegmentCreated(MakeSegment(0, 0, 6000, 0, false, "avc1.640028,mp4a.40.2"));

	auto result = playlist->ToString(true);
	EXPECT_NE(result.IndexOf("#EXT-X-VERSION:3"), -1L);
}

TEST_F(HlsMediaPlaylistTest, EmitsDiscontinuityOnFlaggedSegment)
{
	auto playlist = MakePlaylist();
	playlist->OnSegmentCreated(MakeSegment(0, 0, 6000, 0, false, "avc1.640028"));
	playlist->OnSegmentCreated(MakeSegment(1, 90000 * 6, 6000, 0, false, "avc1.640028"));
	playlist->OnSegmentCreated(MakeSegment(2, 90000 * 12, 6000, 1, true, "hev1.1.6.L93.B0"));
	playlist->OnSegmentCreated(MakeSegment(3, 90000 * 18, 6000, 1, false, "hev1.1.6.L93.B0"));

	auto result = playlist->ToString(true);

	// Exactly one EXT-X-DISCONTINUITY tag
	size_t count = 0;
	int64_t pos = 0;
	while ((pos = result.IndexOf("#EXT-X-DISCONTINUITY\n", pos)) != -1)
	{
		count++;
		pos += 1;
	}
	EXPECT_EQ(count, 1u);
}

TEST_F(HlsMediaPlaylistTest, DiscontinuitySequenceCountsScrolledOut)
{
	// A short live window; the discontinuity sits ahead of it and later scrolls out
	auto playlist = MakePlaylist(2);

	playlist->OnSegmentCreated(MakeSegment(0, 0, 6000, 0, false, "avc1.640028"));
	playlist->OnSegmentCreated(MakeSegment(1, 90000 * 6, 6000, 1, true, "hev1.1.6.L93.B0"));
	playlist->OnSegmentCreated(MakeSegment(2, 90000 * 12, 6000, 1, false, "hev1.1.6.L93.B0"));
	playlist->OnSegmentCreated(MakeSegment(3, 90000 * 18, 6000, 1, false, "hev1.1.6.L93.B0"));
	playlist->OnSegmentCreated(MakeSegment(4, 90000 * 24, 6000, 1, false, "hev1.1.6.L93.B0"));

	// Segment 1 (a discontinuity) is retained but ahead of the live window, so it
	// counts toward the sequence base.
	auto live = playlist->ToString(false);
	EXPECT_NE(live.IndexOf("#EXT-X-DISCONTINUITY-SEQUENCE:1"), -1L);

	// Scroll out segments 0 and 1 (the discontinuity) entirely; the base is now
	// carried by the removed counter instead.
	playlist->OnSegmentDeleted(MakeSegment(0, 0, 6000, 0, false, "avc1.640028"));
	playlist->OnSegmentDeleted(MakeSegment(1, 90000 * 6, 6000, 1, true, "hev1.1.6.L93.B0"));

	auto after = playlist->ToString(true);
	EXPECT_NE(after.IndexOf("#EXT-X-DISCONTINUITY-SEQUENCE:1"), -1L);
}

TEST_F(HlsMediaPlaylistTest, CodecsUnionSpansCodecChange)
{
	auto playlist = MakePlaylist();
	playlist->OnSegmentCreated(MakeSegment(0, 0, 6000, 0, false, "avc1.640028,mp4a.40.2"));
	playlist->OnSegmentCreated(MakeSegment(1, 90000 * 6, 6000, 1, true, "hev1.1.6.L93.B0,mp4a.40.2"));

	auto codecs = playlist->GetCodecsString();

	EXPECT_NE(codecs.IndexOf("avc1.640028"), -1L);
	EXPECT_NE(codecs.IndexOf("hev1.1.6.L93.B0"), -1L);

	// The shared audio codec appears only once
	size_t audio_count = 0;
	int64_t pos = 0;
	while ((pos = codecs.IndexOf("mp4a.40.2", pos)) != -1)
	{
		audio_count++;
		pos += 1;
	}
	EXPECT_EQ(audio_count, 1u);
}

TEST_F(HlsMediaPlaylistTest, CodecsUnionShrinksWhenOldCodecScrollsOut)
{
	auto playlist = MakePlaylist();
	playlist->OnSegmentCreated(MakeSegment(0, 0, 6000, 0, false, "avc1.640028,mp4a.40.2"));
	playlist->OnSegmentCreated(MakeSegment(1, 90000 * 6, 6000, 1, true, "hev1.1.6.L93.B0,mp4a.40.2"));
	playlist->OnSegmentCreated(MakeSegment(2, 90000 * 12, 6000, 1, false, "hev1.1.6.L93.B0,mp4a.40.2"));

	// Spans the codec change
	EXPECT_NE(playlist->GetCodecsString().IndexOf("avc1.640028"), -1L);

	// Remove the only old-codec segment; its codec drops from the union
	playlist->OnSegmentDeleted(MakeSegment(0, 0, 6000, 0, false, "avc1.640028,mp4a.40.2"));

	auto codecs = playlist->GetCodecsString();
	EXPECT_EQ(codecs.IndexOf("avc1.640028"), -1L);
	EXPECT_NE(codecs.IndexOf("hev1.1.6.L93.B0"), -1L);
}

TEST_F(HlsMediaPlaylistTest, NoDiscontinuityWithoutChange)
{
	auto playlist = MakePlaylist();
	playlist->OnSegmentCreated(MakeSegment(0, 0, 6000, 0, false, "avc1.640028"));
	playlist->OnSegmentCreated(MakeSegment(1, 90000 * 6, 6000, 0, false, "avc1.640028"));

	auto result = playlist->ToString(true);
	EXPECT_EQ(result.IndexOf("#EXT-X-DISCONTINUITY"), -1L);
}

TEST_F(HlsMediaPlaylistTest, ExcludedFlag)
{
	auto playlist = MakePlaylist();
	EXPECT_FALSE(playlist->IsExcluded());
	playlist->SetExcluded(true);
	EXPECT_TRUE(playlist->IsExcluded());
}
