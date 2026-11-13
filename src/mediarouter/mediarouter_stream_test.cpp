//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  Covers: MediaRouteStream mirror buffer policies - RetainMirrorBuffer
//  (per-track keyframe-led retention) and BuildPastData (tap backfill assembly),
//  and the track versions a VP8 picture size change publishes
//
//==============================================================================
#include <base/info/application.h>
#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "mediarouter_stream.h"

namespace
{
	constexpr int64_t kRetentionUs = MEDIA_ROUTE_STREAM_MIRROR_RETENTION_MS * 1000;
	constexpr int64_t kBaseBackfillUs = MEDIA_ROUTE_STREAM_MIRROR_BASE_BACKFILL_MS * 1000;

	std::shared_ptr<MediaPacket> MakePacket(cmn::MediaType media_type, uint32_t track_id, bool keyframe)
	{
		return std::make_shared<MediaPacket>(media_type, track_id, nullptr, 0, 0, 0,
											 keyframe ? MediaPacketFlag::Key : MediaPacketFlag::NoFlag,
											 cmn::BitstreamFormat::Unknown, cmn::PacketType::Unknown);
	}

	std::shared_ptr<MediaRouteStream::MirrorBufferItem> MakeItem(cmn::MediaType media_type, uint32_t track_id, bool keyframe, int64_t dts_us = 0, int64_t age_ms = 0)
	{
		auto packet = MakePacket(media_type, track_id, keyframe);
		auto item = std::make_shared<MediaRouteStream::MirrorBufferItem>(packet, dts_us);
		item->created_time = std::chrono::steady_clock::now() - std::chrono::milliseconds(age_ms);
		return item;
	}
}  // namespace

TEST(MediaRouterRetainMirrorBuffer, KeyframeSupersedesTrackGop)
{
	MediaRouteStream::MirrorBufferMap buffers;
	buffers[1] = {MakeItem(cmn::MediaType::Video, 1, true), MakeItem(cmn::MediaType::Video, 1, false)};
	buffers[3] = {MakeItem(cmn::MediaType::Audio, 3, false)};
	auto audio = buffers[3][0];

	auto keyframe = MakePacket(cmn::MediaType::Video, 1, true);
	MediaRouteStream::RetainMirrorBuffer(buffers, keyframe, 0);

	// Track 1 holds only the new keyframe; the audio track is untouched
	ASSERT_EQ(buffers[1].size(), 1u);
	EXPECT_EQ(buffers[1][0]->packet, keyframe);
	ASSERT_EQ(buffers[3].size(), 1u);
	EXPECT_EQ(buffers[3][0], audio);
}

TEST(MediaRouterRetainMirrorBuffer, VideoStaysEmptyUntilKeyframe)
{
	MediaRouteStream::MirrorBufferMap buffers;

	auto non_key = MakePacket(cmn::MediaType::Video, 1, false);
	MediaRouteStream::RetainMirrorBuffer(buffers, non_key, 0);

	EXPECT_TRUE(buffers[1].empty());

	auto keyframe = MakePacket(cmn::MediaType::Video, 1, true);
	MediaRouteStream::RetainMirrorBuffer(buffers, keyframe, 0);

	ASSERT_EQ(buffers[1].size(), 1u);
	EXPECT_EQ(buffers[1][0]->packet, keyframe);
}

TEST(MediaRouterRetainMirrorBuffer, OverlongGopDiscardedWhole)
{
	// The GOP has grown past the retention window in media time, so the arriving
	// non-key packet discards the whole GOP and the track waits for a keyframe
	MediaRouteStream::MirrorBufferMap buffers;
	buffers[1] = {
		MakeItem(cmn::MediaType::Video, 1, true, 0),
		MakeItem(cmn::MediaType::Video, 1, false, 33000),
	};

	auto non_key = MakePacket(cmn::MediaType::Video, 1, false);
	MediaRouteStream::RetainMirrorBuffer(buffers, non_key, kRetentionUs + 1000);

	EXPECT_TRUE(buffers[1].empty());
}

TEST(MediaRouterRetainMirrorBuffer, NonVideoKeepsRetentionWindowOfContent)
{
	MediaRouteStream::MirrorBufferMap buffers;
	buffers[3] = {
		MakeItem(cmn::MediaType::Audio, 3, false, 0),	  // falls out of the window
		MakeItem(cmn::MediaType::Audio, 3, false, 2000000),
	};
	auto recent = buffers[3][1];

	auto packet = MakePacket(cmn::MediaType::Audio, 3, false);
	MediaRouteStream::RetainMirrorBuffer(buffers, packet, kRetentionUs + 1000000);

	ASSERT_EQ(buffers[3].size(), 2u);
	EXPECT_EQ(buffers[3][0], recent);
	EXPECT_EQ(buffers[3][1]->packet, packet);
}

TEST(MediaRouterBuildPastData, NonVideoCutAtEarliestKeyframeDts)
{
	MediaRouteStream::MirrorBufferMap buffers;
	buffers[1] = {
		MakeItem(cmn::MediaType::Video, 1, true, 5000000),
		MakeItem(cmn::MediaType::Video, 1, false, 5033000),
	};
	buffers[3] = {
		MakeItem(cmn::MediaType::Audio, 3, false, 4900000),	 // before the keyframe DTS: cut
		MakeItem(cmn::MediaType::Audio, 3, false, 5100000),
		MakeItem(cmn::MediaType::Audio, 3, false, 9000000),
	};

	auto past_data = MediaRouteStream::BuildPastData(buffers);

	ASSERT_EQ(past_data.size(), 4u);
	EXPECT_EQ(past_data[0], buffers[1][0]);
	EXPECT_EQ(past_data[1], buffers[1][1]);
	EXPECT_EQ(past_data[2], buffers[3][1]);
	EXPECT_EQ(past_data[3], buffers[3][2]);
}

TEST(MediaRouterBuildPastData, NonVideoBaseWindowWithoutVideo)
{
	// Without video the track keeps only the last base window of content,
	// anchored at its newest DTS
	MediaRouteStream::MirrorBufferMap buffers;
	buffers[3] = {
		MakeItem(cmn::MediaType::Audio, 3, false, 5000000 - kBaseBackfillUs - 1000),  // outside the window: cut
		MakeItem(cmn::MediaType::Audio, 3, false, 5000000 - kBaseBackfillUs + 1000),
		MakeItem(cmn::MediaType::Audio, 3, false, 5000000),
	};

	auto past_data = MediaRouteStream::BuildPastData(buffers);

	ASSERT_EQ(past_data.size(), 2u);
	EXPECT_EQ(past_data[0], buffers[3][1]);
	EXPECT_EQ(past_data[1], buffers[3][2]);
}

TEST(MediaRouterBuildPastData, StaleVideoTrackSkipped)
{
	// A stalled track still holding an old GOP contributes no past data; the
	// audio then falls back to its own base window
	MediaRouteStream::MirrorBufferMap buffers;
	buffers[1] = {
		MakeItem(cmn::MediaType::Video, 1, true, 0, MEDIA_ROUTE_STREAM_MIRROR_RETENTION_MS + 1000),
		MakeItem(cmn::MediaType::Video, 1, false, 33000, MEDIA_ROUTE_STREAM_MIRROR_RETENTION_MS + 500),
	};
	buffers[3] = {MakeItem(cmn::MediaType::Audio, 3, false, 1500000)};

	auto past_data = MediaRouteStream::BuildPastData(buffers);

	ASSERT_EQ(past_data.size(), 1u);
	EXPECT_EQ(past_data[0], buffers[3][0]);
}

TEST(MediaRouterBuildPastData, InterleavesTracksByDts)
{
	MediaRouteStream::MirrorBufferMap buffers;
	buffers[1] = {
		MakeItem(cmn::MediaType::Video, 1, true, 1000000),
		MakeItem(cmn::MediaType::Video, 1, false, 1066000),
	};
	buffers[3] = {
		MakeItem(cmn::MediaType::Audio, 3, false, 1030000),
		MakeItem(cmn::MediaType::Audio, 3, false, 1090000),
	};

	auto past_data = MediaRouteStream::BuildPastData(buffers);

	ASSERT_EQ(past_data.size(), 4u);
	EXPECT_EQ(past_data[0], buffers[1][0]);	 // video 1000000
	EXPECT_EQ(past_data[1], buffers[3][0]);	 // audio 1030000
	EXPECT_EQ(past_data[2], buffers[1][1]);	 // video 1066000
	EXPECT_EQ(past_data[3], buffers[3][1]);	 // audio 1090000
}

TEST(MediaRouterBuildPastData, EmptyBuffersReturnEmpty)
{
	MediaRouteStream::MirrorBufferMap buffers;

	auto past_data = MediaRouteStream::BuildPastData(buffers);

	EXPECT_TRUE(past_data.empty());
}

namespace
{
	class TestApplicationInfo : public info::Application
	{
	public:
		using info::Application::GetInvalidApplication;
	};

	constexpr uint32_t kVp8TrackId = 0;

	// Frame tag with version 0, show_frame 1 and an empty first partition (RFC 6386 section 9.1)
	std::vector<uint8_t> Vp8KeyFrame(uint16_t width, uint16_t height)
	{
		std::vector<uint8_t> frame = {
			0x10, 0x00, 0x00,
			0x9d, 0x01, 0x2a,
			static_cast<uint8_t>(width & 0xFF), static_cast<uint8_t>(width >> 8),
			static_cast<uint8_t>(height & 0xFF), static_cast<uint8_t>(height >> 8)};
		frame.resize(16, 0x00);
		return frame;
	}

	// Same frame tag with the frame type bit set, which marks an interframe
	std::vector<uint8_t> Vp8DeltaFrame()
	{
		std::vector<uint8_t> frame = {0x11, 0x00, 0x00};
		frame.resize(16, 0x00);
		return frame;
	}

	// Feeds VP8 frames into one inbound stream, as a WHIP publisher does
	class Vp8Ingest
	{
	public:
		// A declared size stands in for the Oven-Capabilities max_width and max_height
		explicit Vp8Ingest(int32_t declared_width = 0, int32_t declared_height = 0)
		{
			_stream = std::make_shared<info::Stream>(TestApplicationInfo::GetInvalidApplication(), StreamSourceType::WebRTC);

			auto track = std::make_shared<MediaTrack>();
			track->SetId(kVp8TrackId);
			track->SetMediaType(cmn::MediaType::Video);
			track->SetCodecId(cmn::MediaCodecId::Vp8);
			track->SetTimeBase(1, 90000);
			if (declared_width > 0 && declared_height > 0)
			{
				track->SetResolution(declared_width, declared_height);
			}
			_stream->AddTrack(track);

			_route = std::make_unique<MediaRouteStream>(_stream, cmn::MediaRouterStreamType::INBOUND, 0);
		}

		// Returns the frame after normalization, or nullptr when it was dropped
		std::shared_ptr<MediaPacket> Send(const std::vector<uint8_t> &frame)
		{
			auto packet = std::make_shared<MediaPacket>(cmn::MediaType::Video, kVp8TrackId,
														frame.data(), static_cast<int32_t>(frame.size()),
														_pts, _pts, 3000, MediaPacketFlag::Unknown,
														cmn::BitstreamFormat::VP8, cmn::PacketType::RAW);
			_pts += 3000;

			_route->Push(packet);
			return _route->PopAndNormalize();
		}

		// The input stream's own track, updated together with each published version
		std::shared_ptr<const MediaTrack> StreamTrack() const
		{
			return _stream->GetTrack(kVp8TrackId);
		}

	private:
		std::shared_ptr<info::Stream> _stream;
		std::unique_ptr<MediaRouteStream> _route;
		int64_t _pts = 0;
	};
}  // namespace

TEST(MediaRouterVp8PictureSize, KeyFrameWithNewSizePublishesNewVersion)
{
	Vp8Ingest ingest;

	auto first = ingest.Send(Vp8KeyFrame(640, 480));
	ASSERT_NE(first, nullptr);
	ASSERT_NE(first->GetTrack(), nullptr);
	EXPECT_EQ(first->GetTrack()->GetResolution().width, 640);
	EXPECT_EQ(first->GetTrack()->GetResolution().height, 480);

	auto delta = ingest.Send(Vp8DeltaFrame());
	ASSERT_NE(delta, nullptr);
	EXPECT_EQ(delta->GetTrack(), first->GetTrack());

	auto resized = ingest.Send(Vp8KeyFrame(320, 240));
	ASSERT_NE(resized, nullptr);
	ASSERT_NE(resized->GetTrack(), nullptr);
	EXPECT_EQ(resized->GetTrack()->GetVersion(), first->GetTrack()->GetVersion() + 1);
	EXPECT_EQ(resized->GetTrack()->GetResolution().width, 320);
	EXPECT_EQ(resized->GetTrack()->GetResolution().height, 240);
	EXPECT_EQ(ingest.StreamTrack()->GetResolution().width, 320);
	EXPECT_EQ(ingest.StreamTrack()->GetResolution().height, 240);
}

TEST(MediaRouterVp8PictureSize, KeyFrameWithSameSizeKeepsVersion)
{
	Vp8Ingest ingest;

	auto first = ingest.Send(Vp8KeyFrame(640, 480));
	ASSERT_NE(first, nullptr);

	auto again = ingest.Send(Vp8KeyFrame(640, 480));
	ASSERT_NE(again, nullptr);
	EXPECT_EQ(again->GetTrack(), first->GetTrack());
}

TEST(MediaRouterVp8PictureSize, DeclaredSizeIsReplacedButStaysTheMax)
{
	Vp8Ingest ingest(1280, 720);

	auto first = ingest.Send(Vp8KeyFrame(640, 480));
	ASSERT_NE(first, nullptr);
	ASSERT_NE(first->GetTrack(), nullptr);
	EXPECT_EQ(first->GetTrack()->GetResolution().width, 640);
	EXPECT_EQ(first->GetTrack()->GetResolution().height, 480);
	EXPECT_EQ(first->GetTrack()->GetMaxResolution().width, 1280);
	EXPECT_EQ(first->GetTrack()->GetMaxResolution().height, 720);
}

TEST(MediaRouterVp8PictureSize, UnreadableKeyFrameKeepsLastSize)
{
	Vp8Ingest ingest;

	auto broken = Vp8KeyFrame(320, 240);
	broken[3] = 0x00;  // breaks the start code

	// Before any size is known the frame cannot describe the track
	EXPECT_EQ(ingest.Send(broken), nullptr);

	auto first = ingest.Send(Vp8KeyFrame(640, 480));
	ASSERT_NE(first, nullptr);

	auto kept = ingest.Send(broken);
	ASSERT_NE(kept, nullptr);
	EXPECT_EQ(kept->GetTrack(), first->GetTrack());
}
