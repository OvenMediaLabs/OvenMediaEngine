//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  modules/ffmpeg/writer_test.cpp
//  Covers: ffmpeg::Writer
//    - Public API contract (url/timeouts/timestamp mode/tracks/state)
//    - Muxer lifecycle (Start/Stop/finalize) and the trailer-flush guarantee
//    - Thread-safety of SendPacket vs. Stop (the concurrency the crash fix targets)
//
//==============================================================================
// cmake -S src -B build -DOME_BUILD_TESTS=ON
// cmake --build build --target ome_test_modules
// ctest --test-dir build -L modules -R FfmpegWriter --output-on-failure

#include <gtest/gtest.h>

#include <modules/ffmpeg/writer.h>
#include <modules/bitstream/aac/audio_specific_config.h>
#include <modules/bitstream/h264/h264_decoder_configuration_record.h>

#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace
{
	namespace fs = std::filesystem;

	// Unique temp path for one test. Name only varies by suffix so the same test
	// is reproducible; the file is removed by TempFile's destructor.
	class TempFile
	{
	public:
		explicit TempFile(const char *suffix)
		{
			static std::atomic<uint64_t> counter{0};
			ov::String name = ov::String::FormatString(
				"ome_writer_test_%d_%llu.%s",
				static_cast<int>(::getpid()),
				static_cast<unsigned long long>(counter.fetch_add(1)),
				suffix);
			_path = (fs::temp_directory_path() / name.CStr()).string().c_str();
		}

		~TempFile()
		{
			std::error_code ec;
			fs::remove(fs::path(_path.CStr()), ec);
		}

		const ov::String &Path() const { return _path; }

		bool Exists() const { return fs::exists(fs::path(_path.CStr())); }

		int64_t Size() const
		{
			std::error_code ec;
			auto size = fs::file_size(fs::path(_path.CStr()), ec);
			return ec ? -1 : static_cast<int64_t>(size);
		}

	private:
		ov::String _path;
	};

	std::shared_ptr<MediaTrack> MakeVideoTrack(uint32_t id)
	{
		auto track = std::make_shared<MediaTrack>();
		track->SetId(id);
		track->SetMediaType(cmn::MediaType::Video);
		track->SetCodecId(cmn::MediaCodecId::H264);
		track->SetOriginBitstream(cmn::BitstreamFormat::H264_ANNEXB);
		track->SetTimeBase(1, 90000);
		track->SetResolution(320, 240);
		track->SetBitrateByConfig(500000);
		return track;
	}

	std::shared_ptr<MediaTrack> MakeAudioTrack(uint32_t id)
	{
		auto track = std::make_shared<MediaTrack>();
		track->SetId(id);
		track->SetMediaType(cmn::MediaType::Audio);
		track->SetCodecId(cmn::MediaCodecId::Aac);
		track->SetOriginBitstream(cmn::BitstreamFormat::AAC_ADTS);
		track->SetTimeBase(1, 48000);
		track->SetChannelLayout(cmn::AudioChannel::Layout::LayoutStereo);
		track->SetSampleRate(48000);
		track->SetBitrateByConfig(128000);
		return track;
	}

	std::shared_ptr<MediaPacket> MakeVideoPacket(uint32_t track_id, int64_t pts, int64_t dts, bool key)
	{
		// Minimal AnnexB-ish payload. Content correctness is not asserted; this only
		// drives SendPacket()'s code path (bitstream branch selection + write/drop).
		static const uint8_t kNal[] = {0x00, 0x00, 0x00, 0x01, 0x65, 0x11, 0x22, 0x33};
		return std::make_shared<MediaPacket>(
			cmn::MediaType::Video, track_id,
			kNal, static_cast<int32_t>(sizeof(kNal)),
			pts, dts, 3000,
			key ? MediaPacketFlag::Key : MediaPacketFlag::NoFlag,
			cmn::BitstreamFormat::H264_ANNEXB, cmn::PacketType::NALU);
	}
}  // namespace

// ---------------------------------------------------------------------------
// Construction & basic contract
// ---------------------------------------------------------------------------

TEST(FfmpegWriter, CreateReturnsInstance)
{
	auto writer = ffmpeg::Writer::Create();
	ASSERT_NE(writer, nullptr);
	EXPECT_EQ(writer->GetState(), ffmpeg::Writer::WriterStateNone);
}

TEST(FfmpegWriter, SetUrlEmptyFails)
{
	auto writer = ffmpeg::Writer::Create();
	EXPECT_FALSE(writer->SetUrl(""));
	EXPECT_FALSE(writer->GetErrorMessage().IsEmpty());
}

TEST(FfmpegWriter, SetUrlStoresUrl)
{
	TempFile tmp("ts");
	auto writer = ffmpeg::Writer::Create();
	ASSERT_TRUE(writer->SetUrl(tmp.Path(), "mpegts"));
	EXPECT_STREQ(writer->GetUrl().CStr(), tmp.Path().CStr());
}

// A Writer owns one context for its lifetime. A second SetUrl() on the same
// instance must be rejected (not replace the live context), so the original
// URL is preserved and reuse is forced through a new Writer.
TEST(FfmpegWriter, SetUrlOnLiveContextIsRejected)
{
	TempFile first("ts");
	TempFile second("ts");
	auto writer = ffmpeg::Writer::Create();
	ASSERT_TRUE(writer->SetUrl(first.Path(), "mpegts"));

	EXPECT_FALSE(writer->SetUrl(second.Path(), "mpegts"));
	EXPECT_FALSE(writer->GetErrorMessage().IsEmpty());
	// The original URL must be untouched.
	EXPECT_STREQ(writer->GetUrl().CStr(), first.Path().CStr());
}

// ---------------------------------------------------------------------------
// Timeouts & timestamp mode round-trips
// ---------------------------------------------------------------------------

TEST(FfmpegWriter, ConnectionTimeoutRoundTrip)
{
	auto writer = ffmpeg::Writer::Create();
	writer->SetConnectionTimeout(1234);
	EXPECT_EQ(writer->GetConnectionTimeout(), 1234);
}

TEST(FfmpegWriter, SendTimeoutRoundTrip)
{
	auto writer = ffmpeg::Writer::Create();
	writer->SetSendTimeout(4321);
	EXPECT_EQ(writer->GetSendTimeout(), 4321);
}

TEST(FfmpegWriter, TimestampModeRoundTrip)
{
	auto writer = ffmpeg::Writer::Create();
	writer->SetTimestampMode(ffmpeg::Writer::TIMESTAMP_PASSTHROUGH_MODE);
	EXPECT_EQ(writer->GetTimestampMode(), ffmpeg::Writer::TIMESTAMP_PASSTHROUGH_MODE);

	writer->SetTimestampMode(ffmpeg::Writer::TIMESTAMP_STARTZERO_MODE);
	EXPECT_EQ(writer->GetTimestampMode(), ffmpeg::Writer::TIMESTAMP_STARTZERO_MODE);
}

// ---------------------------------------------------------------------------
// Track registration & lookup
// ---------------------------------------------------------------------------

TEST(FfmpegWriter, AddTrackRegistersTrack)
{
	TempFile tmp("ts");
	auto writer = ffmpeg::Writer::Create();
	ASSERT_TRUE(writer->SetUrl(tmp.Path(), "mpegts"));

	ASSERT_TRUE(writer->AddTrack(MakeVideoTrack(1)));
	ASSERT_TRUE(writer->AddTrack(MakeAudioTrack(2)));

	EXPECT_NE(writer->GetTrackByTrackId(1), nullptr);
	EXPECT_NE(writer->GetTrackByTrackId(2), nullptr);
	EXPECT_EQ(writer->GetTrackByTrackId(999), nullptr);

	EXPECT_EQ(writer->GetTrackCountByType(cmn::MediaType::Video), 1);
	EXPECT_EQ(writer->GetTrackCountByType(cmn::MediaType::Audio), 1);
}

TEST(FfmpegWriter, GetTrackCountEmptyWithoutTracks)
{
	TempFile tmp("ts");
	auto writer = ffmpeg::Writer::Create();
	ASSERT_TRUE(writer->SetUrl(tmp.Path(), "mpegts"));

	EXPECT_EQ(writer->GetTrackCountByType(cmn::MediaType::Video), 0);
	EXPECT_EQ(writer->GetTrackCountByType(cmn::MediaType::Audio), 0);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST(FfmpegWriter, StopBeforeStartIsSafe)
{
	auto writer = ffmpeg::Writer::Create();
	// No SetUrl / Start: Stop must be a safe no-op (no crash, no double-free).
	EXPECT_TRUE(writer->Stop());
	EXPECT_EQ(writer->GetState(), ffmpeg::Writer::WriterStateClosed);
}

TEST(FfmpegWriter, StartWithoutContextFails)
{
	auto writer = ffmpeg::Writer::Create();
	// Start without SetUrl (no AVFormatContext) must fail cleanly.
	EXPECT_FALSE(writer->Start());
}

TEST(FfmpegWriter, StartWithNoTracksFails)
{
	TempFile tmp("ts");
	auto writer = ffmpeg::Writer::Create();
	ASSERT_TRUE(writer->SetUrl(tmp.Path(), "mpegts"));

	// avformat_write_header() fails with "No streams to mux were specified".
	EXPECT_FALSE(writer->Start());
	EXPECT_EQ(writer->GetState(), ffmpeg::Writer::WriterStateError);
}

// Full lifecycle: a successful Start followed by Stop must finalize the file
// (write the trailer and flush it). This is the behavior the crash fix must not
// regress: the final avio flush must complete, so the output is non-empty.
TEST(FfmpegWriter, FullLifecycleFinalizesFile)
{
	TempFile tmp("ts");
	auto writer = ffmpeg::Writer::Create();
	ASSERT_TRUE(writer->SetUrl(tmp.Path(), "mpegts"));
	ASSERT_TRUE(writer->AddTrack(MakeVideoTrack(1)));

	if (writer->Start() == false)
	{
		// Depends on the ffmpeg build (muxer/codec availability). Don't fail the suite.
		GTEST_SKIP() << "Writer::Start() failed in this ffmpeg build: "
					 << writer->GetErrorMessage().CStr();
	}

	EXPECT_EQ(writer->GetState(), ffmpeg::Writer::WriterStateConnected);

	for (int i = 0; i < 10; i++)
	{
		writer->SendPacket(MakeVideoPacket(1, i * 3000, i * 3000, i == 0));
	}

	EXPECT_TRUE(writer->Stop());
	EXPECT_EQ(writer->GetState(), ffmpeg::Writer::WriterStateClosed);

	// The trailer/flush must have produced a non-empty file.
	EXPECT_TRUE(tmp.Exists());
	EXPECT_GT(tmp.Size(), 0);
}

TEST(FfmpegWriter, SendPacketBeforeStartIsDropped)
{
	TempFile tmp("ts");
	auto writer = ffmpeg::Writer::Create();
	ASSERT_TRUE(writer->SetUrl(tmp.Path(), "mpegts"));
	ASSERT_TRUE(writer->AddTrack(MakeVideoTrack(1)));

	// Not started yet: SendPacket must not be treated as an error (returns true, drops).
	EXPECT_TRUE(writer->SendPacket(MakeVideoPacket(1, 0, 0, true)));
}

// Exercise the actual muxer: SendPacket returns true even when it drops a packet.
// MP4 recording and FLV push share the same timestamp normalization.
class FfmpegWriterTimestamps
	: public testing::TestWithParam<std::tuple<std::string, ffmpeg::Writer::TimestampMode, int64_t>> {};

TEST_P(FfmpegWriterTimestamps, PreservesOpeningKeyframeAndAudioOffsets)
{
	// A decodable 16x16 black H.264 IDR and AAC stereo silence. Generated with:
	// ffmpeg -f lavfi -i color=black:size=16x16:rate=30 -frames:v 1 -c:v libx264 -bf 2 -f h264 frame.h264
	// ffmpeg -f lavfi -i anullsrc=r=48000:cl=stereo -frames:a 1 -c:a aac -f adts silence.aac
	// Keep SPS/PPS and IDR (without SEI), and the final AAC frame without ADTS.
	static const uint8_t sps[] = {0x67, 0x64, 0x00, 0x0a, 0xac, 0xd9, 0x5e, 0xc0,
		0x44, 0x00, 0x00, 0x03, 0x00, 0x04, 0x00, 0x00, 0x03, 0x00, 0xf0, 0x3c, 0x48, 0x96, 0x58};
	static const uint8_t pps[] = {0x68, 0xeb, 0xe3, 0xcb, 0x22, 0xc0};
	static const uint8_t idr[] = {0x00, 0x00, 0x00, 0x0f, 0x65, 0x88, 0x84, 0x00,
		0x27, 0xff, 0xfe, 0xf5, 0xb1, 0x7c, 0x0a, 0x6a, 0xe9, 0xeb, 0x81};
	static const uint8_t asc[] = {0x11, 0x90};
	static const uint8_t silence[] = {0x21, 0x10, 0x04, 0x60, 0x8c, 0x1c};

	const auto &[format, mode, composition_offset] = GetParam();
	TempFile tmp(format.c_str());
	auto writer = ffmpeg::Writer::Create();
	ASSERT_TRUE(writer->SetUrl(tmp.Path(), format.c_str()));
	writer->SetTimestampMode(mode);

	auto video = MakeVideoTrack(1);
	video->SetResolution(16, 16);
	video->SetOriginBitstream(cmn::BitstreamFormat::H264_AVCC);
	auto avcc = std::make_shared<AVCDecoderConfigurationRecord>();
	ASSERT_TRUE(avcc->AddSPS(std::make_shared<ov::Data>(sps, sizeof(sps))));
	ASSERT_TRUE(avcc->AddPPS(std::make_shared<ov::Data>(pps, sizeof(pps))));
	video->SetDecoderConfigurationRecord(avcc);
	ASSERT_TRUE(writer->AddTrack(video));

	auto audio = MakeAudioTrack(2);
	audio->SetOriginBitstream(cmn::BitstreamFormat::AAC_RAW);
	auto aac = std::make_shared<AudioSpecificConfig>();
	ASSERT_TRUE(aac->Parse(std::make_shared<ov::Data>(asc, sizeof(asc))));
	audio->SetDecoderConfigurationRecord(aac);
	ASSERT_TRUE(writer->AddTrack(audio));
	ASSERT_TRUE(writer->Start()) << writer->GetErrorMessage().CStr();

	// A later recording segment starts with a keyframe whose PTS exceeds its DTS.
	// Video and audio use different timebases, but must share the same offset.
	auto keyframe = std::make_shared<MediaPacket>(cmn::MediaType::Video, 1,
		idr, sizeof(idr), 90000 + composition_offset, 90000, 3000,
		MediaPacketFlag::Key, cmn::BitstreamFormat::H264_AVCC, cmn::PacketType::NALU);
	uint64_t sent_bytes = 0;
	ASSERT_TRUE(writer->SendPacket(keyframe, &sent_bytes));
	EXPECT_EQ(sent_bytes, sizeof(idr));
	for (int64_t timestamp : {46976, 48000, 49024})
	{
		// Passthrough does not clip earlier audio; FLV requires it to arrive
		// before the video, so only test the late-packet clipping in start-zero mode.
		if (mode == ffmpeg::Writer::TIMESTAMP_PASSTHROUGH_MODE && timestamp < 48000)
		{
			continue;
		}
		auto packet = std::make_shared<MediaPacket>(cmn::MediaType::Audio, 2,
			silence, sizeof(silence), timestamp, timestamp, 1024,
			MediaPacketFlag::Key, cmn::BitstreamFormat::AAC_RAW, cmn::PacketType::RAW);
		sent_bytes = 0;
		ASSERT_TRUE(writer->SendPacket(packet, &sent_bytes));
		bool clipped = mode == ffmpeg::Writer::TIMESTAMP_STARTZERO_MODE && timestamp < 48000;
		EXPECT_EQ(sent_bytes, clipped ? 0 : sizeof(silence));
	}
	ASSERT_TRUE(writer->Stop());

	AVFormatContext *raw_input = nullptr;
	ASSERT_GE(avformat_open_input(&raw_input, tmp.Path().CStr(), nullptr, nullptr), 0);
	auto input = std::shared_ptr<AVFormatContext>(raw_input, [](AVFormatContext *p) { avformat_close_input(&p); });
	ASSERT_GE(avformat_find_stream_info(input.get(), nullptr), 0);
	auto packet = std::shared_ptr<AVPacket>(av_packet_alloc(), [](AVPacket *p) { av_packet_free(&p); });
	ASSERT_NE(packet, nullptr);
	int video_packets = 0;
	int audio_packets = 0;
	while (av_read_frame(input.get(), packet.get()) >= 0)
	{
		auto stream = input->streams[packet->stream_index];
		if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			++video_packets;
			EXPECT_TRUE(packet->flags & AV_PKT_FLAG_KEY);
			EXPECT_NEAR((packet->pts - packet->dts) * av_q2d(stream->time_base), composition_offset / 90000.0, 0.001);
			if (mode == ffmpeg::Writer::TIMESTAMP_STARTZERO_MODE)
			{
				EXPECT_EQ(packet->dts, 0);
			}
			else if (format == "flv")
			{
				EXPECT_DOUBLE_EQ(packet->dts * av_q2d(stream->time_base), 1.0);
			}
		}
		else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			if (audio_packets++ == 0 && mode == ffmpeg::Writer::TIMESTAMP_STARTZERO_MODE)
			{
				EXPECT_EQ(packet->dts, 0);
				EXPECT_EQ(packet->pts, 0);
			}
		}
		av_packet_unref(packet.get());
	}
	EXPECT_EQ(video_packets, 1);
	EXPECT_EQ(audio_packets, 2);
}

INSTANTIATE_TEST_SUITE_P(Muxers, FfmpegWriterTimestamps,
	testing::Combine(testing::Values("mp4", "flv"),
		testing::Values(ffmpeg::Writer::TIMESTAMP_STARTZERO_MODE, ffmpeg::Writer::TIMESTAMP_PASSTHROUGH_MODE),
		testing::Values(0, 6000)));

// ---------------------------------------------------------------------------
// Thread-safety: SendPacket racing with Stop must not crash or corrupt state.
// This exercises the lock that serializes frame-write against muxer teardown.
// Most valuable under ThreadSanitizer, but must at minimum not crash.
// ---------------------------------------------------------------------------

TEST(FfmpegWriter, ConcurrentSendPacketAndStopDoesNotCrash)
{
	TempFile tmp("ts");
	auto writer = ffmpeg::Writer::Create();
	ASSERT_TRUE(writer->SetUrl(tmp.Path(), "mpegts"));
	ASSERT_TRUE(writer->AddTrack(MakeVideoTrack(1)));

	if (writer->Start() == false)
	{
		GTEST_SKIP() << "Writer::Start() failed in this ffmpeg build: "
					 << writer->GetErrorMessage().CStr();
	}

	std::atomic<bool> stop_flag{false};
	std::atomic<int64_t> pts{0};

	std::vector<std::thread> senders;
	for (int t = 0; t < 4; t++)
	{
		senders.emplace_back([&]() {
			while (stop_flag.load() == false)
			{
				auto ts = pts.fetch_add(3000);
				// After Stop() finalizes the context, SendPacket must bail out
				// safely (re-check under the lock), never write into a freed context.
				writer->SendPacket(MakeVideoPacket(1, ts, ts, false));
			}
		});
	}

	// Let some frames flow, then tear down while senders are still active.
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	EXPECT_TRUE(writer->Stop());
	stop_flag.store(true);

	for (auto &s : senders)
	{
		s.join();
	}

	// Reaching here without a crash/hang is the assertion.
	EXPECT_EQ(writer->GetState(), ffmpeg::Writer::WriterStateClosed);
}
