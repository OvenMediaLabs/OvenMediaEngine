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

#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <thread>
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
