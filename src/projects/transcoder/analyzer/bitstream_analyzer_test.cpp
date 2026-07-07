//==============================================================================
//
//  OvenMediaEngine - Unit Tests : BitstreamAnalyzer
//
//  Tests the read-only packet analyzer (transcoder/analyzer).
//
//  Groups:
//    A) Common logic: init / guards / close.
//    B) Per-codec: resolution for VP8, AV1, H.264, H.265.
//    C) Multi-frame detection: audio (ADTS-AAC) and video (H.264).
//
//  VP8 / AV1 test data is fully built here, so exact width/height are checked.
//  H.264 / H.265 use real SPS blobs, so resolution is only checked as > 0.
//
//==============================================================================

// Command
// cmake --build build/Debug --target ome_test_transcoder
// ctest --test-dir build/Debug -R BitstreamAnalyzer --output-on-failure

#include "bitstream_analyzer.h"

#include <gtest/gtest.h>

#include <base/ovlibrary/bit_writer.h>
#include <modules/bitstream/av1/av1_types.h>

#include <cstdint>
#include <vector>

namespace
{
	// ---- Dummy audio bytes (content doesn't matter) ----
	const std::vector<uint8_t> kDummyAudio = {0xFF, 0xF1, 0x50, 0x80, 0x01, 0x02, 0x03};

	// Wraps a byte buffer in a MediaPacket so it can be analyzed.
	std::shared_ptr<MediaPacket> MakePacket(cmn::MediaType type,
											const std::vector<uint8_t> &data,
											int64_t pts, int64_t dts)
	{
		return std::make_shared<MediaPacket>(
			0, type, 0,
			data.data(), static_cast<int32_t>(data.size()),
			pts, dts, 0,
			MediaPacketFlag::NoFlag,
			cmn::BitstreamFormat::Unknown,
			cmn::PacketType::Unknown);
	}

	// Builds one ADTS-AAC frame of `total_len` bytes (7-byte header, no CRC,
	// payload filled with `marker`) with an explicit sampling_frequency_index and
	// channel_configuration. The frame_length field is set to total_len.
	// (freq_index 3 -> 48000 Hz, 4 -> 44100 Hz.)
	std::vector<uint8_t> MakeAdtsFrameCfg(uint16_t total_len, uint8_t marker,
										  uint8_t freq_index, uint8_t channel_config)
	{
		std::vector<uint8_t> f(total_len, marker);
		f[0] = 0xFF;
		f[1] = 0xF1;  // sync, MPEG-4, Layer 0, protection_absent=1
		// profile=AAC-LC(01) | sampling_freq_index(4) | private(0) | channel_config MSB(1)
		f[2] = static_cast<uint8_t>((0x01 << 6) | ((freq_index & 0x0F) << 2) | ((channel_config >> 2) & 0x01));
		// channel_config LSB(2) | orig/home/copyright(0) | frame_length[12:11]
		f[3] = static_cast<uint8_t>(((channel_config & 0x03) << 6) | ((total_len >> 11) & 0x03));
		f[4] = static_cast<uint8_t>((total_len >> 3) & 0xFF);			// frame_length[10:3]
		f[5] = static_cast<uint8_t>(((total_len & 0x07) << 5) | 0x1F);	// frame_length[2:0] + buffer_fullness
		f[6] = 0xFC;													// buffer_fullness + num_raw_data_blocks=0
		return f;
	}

	// Convenience: an ADTS-AAC frame at 44100 Hz, 2 channels.
	std::vector<uint8_t> MakeAdtsFrame(uint16_t total_len, uint8_t marker)
	{
		return MakeAdtsFrameCfg(total_len, marker, /*freq_index=*/4, /*channel_config=*/2);
	}

	// ---- VP8 key frame (RFC 6386) ----
	std::vector<uint8_t> MakeVp8KeyFrame(uint16_t width, uint16_t height)
	{
		return {
			0x10, 0x00, 0x00,			   // frame_tag: key_frame=0, show_frame=1, first_part_size=0
			0x9d, 0x01, 0x2a,			   // start_code
			static_cast<uint8_t>(width & 0xFF), static_cast<uint8_t>((width >> 8) & 0x3F),
			static_cast<uint8_t>(height & 0xFF), static_cast<uint8_t>((height >> 8) & 0x3F),
			0x00, 0x00					   // padding to make the buffer long enough
		};
	}

	std::vector<uint8_t> MakeVp8InterFrame()
	{
		// bit0 = 1 -> not a key frame. No resolution carried.
		return {0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	}

	// ---- AV1 OBU helpers ----
	std::vector<uint8_t> EncodeLeb128(uint64_t value)
	{
		std::vector<uint8_t> bytes;
		do
		{
			uint8_t byte = value & 0x7F;
			value >>= 7;
			if (value != 0)
			{
				byte |= 0x80;
			}
			bytes.push_back(byte);
		} while (value != 0);
		return bytes;
	}

	uint8_t MakeObuHeaderByte(Av1ObuType type, bool has_size_field)
	{
		uint8_t b = 0;
		// forbidden(1) | type(4) | ext_flag(1) | has_size(1) | reserved(1)
		b |= (static_cast<uint8_t>(type) & 0x0F) << 3;
		b |= (has_size_field ? 1 : 0) << 1;
		return b;
	}

	std::vector<uint8_t> MakeObu(Av1ObuType type, const std::vector<uint8_t> &payload)
	{
		std::vector<uint8_t> bytes;
		bytes.push_back(MakeObuHeaderByte(type, true));
		auto leb = EncodeLeb128(payload.size());
		bytes.insert(bytes.end(), leb.begin(), leb.end());
		bytes.insert(bytes.end(), payload.begin(), payload.end());
		return bytes;
	}

	// A reduced sequence header -> width 80, height 48.
	std::vector<uint8_t> BuildReducedSeqHeaderPayload()
	{
		ov::BitWriter bw(64);
		bw.WriteBits(3, 0);	  // seq_profile
		bw.WriteBits(1, 1);	  // still_picture
		bw.WriteBits(1, 1);	  // reduced_still_picture_header
		bw.WriteBits(5, 5);	  // seq_level_idx_0
		bw.WriteBits(4, 7);	  // frame_width_bits_minus_1  -> 8 bits
		bw.WriteBits(4, 7);	  // frame_height_bits_minus_1 -> 8 bits
		bw.WriteBits(8, 79);  // max_frame_width_minus_1  -> width 80
		bw.WriteBits(8, 47);  // max_frame_height_minus_1 -> height 48
		bw.WriteBits(1, 0);	  // use_128x128_superblock
		bw.WriteBits(1, 0);	  // enable_filter_intra
		bw.WriteBits(1, 0);	  // enable_intra_edge_filter
		bw.WriteBits(1, 0);	  // enable_superres
		bw.WriteBits(1, 0);	  // enable_cdef
		bw.WriteBits(1, 0);	  // enable_restoration
		bw.WriteBits(1, 0);	  // high_bitdepth
		bw.WriteBits(1, 0);	  // monochrome
		bw.WriteBits(1, 0);	  // color_description_present_flag
		bw.WriteBits(1, 0);	  // color_range
		bw.WriteBits(2, 0);	  // chroma_sample_position
		return std::vector<uint8_t>(bw.GetData(), bw.GetData() + bw.GetDataSize());
	}

	// One AV1 unit = TemporalDelimiter + SequenceHeader + Frame (key frame).
	std::vector<uint8_t> MakeAv1KeyTemporalUnit()
	{
		std::vector<uint8_t> tu;
		auto td	 = MakeObu(Av1ObuType::TemporalDelimiter, {});
		auto seq = MakeObu(Av1ObuType::SequenceHeader, BuildReducedSeqHeaderPayload());
		auto frm = MakeObu(Av1ObuType::Frame, {0x00});	// reduced -> always KEY_FRAME
		tu.insert(tu.end(), td.begin(), td.end());
		tu.insert(tu.end(), seq.begin(), seq.end());
		tu.insert(tu.end(), frm.begin(), frm.end());
		return tu;
	}

	// ---- H.264 / H.265 Annex-B helpers ----
	const std::vector<uint8_t> kStartCode = {0x00, 0x00, 0x00, 0x01};

	std::vector<uint8_t> WithStartCode(std::vector<uint8_t> nal)
	{
		std::vector<uint8_t> out(kStartCode);
		out.insert(out.end(), nal.begin(), nal.end());
		return out;
	}

	// H.264 NAL header byte: IDR slice = 0x65, non-IDR slice = 0x41.
	// A real High-profile SPS (about 1920x1080); resolution is checked as > 0.
	const std::vector<uint8_t> kH264Sps = {
		0x67, 0x64, 0x00, 0x28, 0xac, 0xd9, 0x40, 0x78, 0x02, 0x27, 0xe5, 0x84,
		0x00, 0x00, 0x03, 0x00, 0x04, 0x00, 0x00, 0x03, 0x00, 0xf0, 0x3c, 0x60, 0xc9, 0x20};

	// H.265 NAL header (2 bytes): IDR = 0x26, TRAIL_R = 0x02.
	// A real SPS (type 33 -> 0x42); resolution is checked as > 0.
	const std::vector<uint8_t> kH265Sps = {
		0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, 0x90, 0x00, 0x00, 0x03,
		0x00, 0x00, 0x03, 0x00, 0x78, 0xa0, 0x03, 0xc0, 0x80, 0x10, 0xe5, 0x96, 0x56,
		0x69, 0x24, 0xca, 0xe0, 0x10, 0x00, 0x00, 0x03, 0x00, 0x10, 0x00, 0x00, 0x03,
		0x01, 0xe0, 0x80};
}  // namespace

// ============================================================================
// Layer A — codec-agnostic logic
// ============================================================================

TEST(BitstreamAnalyzer, InitRejectsUnsupportedCodec)
{
	BitstreamAnalyzer a;
	EXPECT_FALSE(a.Init(cmn::MediaCodecId::None));
	EXPECT_FALSE(a.IsValid());
}

TEST(BitstreamAnalyzer, InitAcceptsSupportedCodecs)
{
	for (auto codec : {cmn::MediaCodecId::H264, cmn::MediaCodecId::H265,
					   cmn::MediaCodecId::Av1, cmn::MediaCodecId::Vp8,
					   cmn::MediaCodecId::Aac, cmn::MediaCodecId::Mp3,
					   cmn::MediaCodecId::Mp2, cmn::MediaCodecId::Opus})
	{
		BitstreamAnalyzer a;
		EXPECT_TRUE(a.Init(codec));
		EXPECT_TRUE(a.IsValid());
	}
}

TEST(BitstreamAnalyzer, AnalyzeGuards)
{
	BitstreamAnalyzer a;

	// Not initialized yet.
	EXPECT_FALSE(a.Analyze(MakePacket(cmn::MediaType::Audio, kDummyAudio, 0, 0)));

	ASSERT_TRUE(a.Init(cmn::MediaCodecId::Aac));

	// Null packet.
	EXPECT_FALSE(a.Analyze(nullptr));
}

TEST(BitstreamAnalyzer, AudioCarriesNoResolution)
{
	BitstreamAnalyzer a;
	ASSERT_TRUE(a.Init(cmn::MediaCodecId::Aac));

	auto frame = MakeAdtsFrame(20, 0xC0);
	ASSERT_TRUE(a.Analyze(MakePacket(cmn::MediaType::Audio, frame, 1000, 1000)));

	EXPECT_EQ(a.GetPts(), 1000);
	EXPECT_EQ(a.GetDts(), 1000);
	EXPECT_EQ(a.GetWidth(), 0);
	EXPECT_EQ(a.GetHeight(), 0);
}

TEST(BitstreamAnalyzer, AacSampleRateAndChannels)
{
	BitstreamAnalyzer a;
	ASSERT_TRUE(a.Init(cmn::MediaCodecId::Aac));

	// MakeAdtsFrame encodes 44100 Hz and 2 channels.
	auto frame = MakeAdtsFrame(20, 0xC0);
	ASSERT_TRUE(a.Analyze(MakePacket(cmn::MediaType::Audio, frame, 1000, 1000)));

	EXPECT_EQ(a.GetSampleRate(), 44100);
	EXPECT_EQ(a.GetChannels(), 2);
	// Audio carries no resolution.
	EXPECT_EQ(a.GetWidth(), 0);
	EXPECT_EQ(a.GetHeight(), 0);
}

TEST(BitstreamAnalyzer, CloseResetsState)
{
	BitstreamAnalyzer a;
	ASSERT_TRUE(a.Init(cmn::MediaCodecId::Vp8));

	auto key = MakeVp8KeyFrame(128, 96);
	ASSERT_TRUE(a.Analyze(MakePacket(cmn::MediaType::Video, key, 5, 5)));
	ASSERT_GT(a.GetWidth(), 0);

	a.Close();
	EXPECT_FALSE(a.IsValid());
	EXPECT_EQ(a.GetWidth(), 0);
	EXPECT_EQ(a.GetHeight(), 0);
	EXPECT_EQ(a.GetFrameCount(), 0);
}

// ============================================================================
// Layer B — per-codec analysis
// ============================================================================

TEST(BitstreamAnalyzer, Vp8Resolution)
{
	BitstreamAnalyzer a;
	ASSERT_TRUE(a.Init(cmn::MediaCodecId::Vp8));

	auto key = MakeVp8KeyFrame(128, 96);
	ASSERT_TRUE(a.Analyze(MakePacket(cmn::MediaType::Video, key, 0, 0)));

	EXPECT_EQ(a.GetWidth(), 128);
	EXPECT_EQ(a.GetHeight(), 96);
	EXPECT_FALSE(a.IsMultiFrame());
}

TEST(BitstreamAnalyzer, Vp8InterFrameKeepsResolution)
{
	BitstreamAnalyzer a;
	ASSERT_TRUE(a.Init(cmn::MediaCodecId::Vp8));

	auto key = MakeVp8KeyFrame(128, 96);
	a.Analyze(MakePacket(cmn::MediaType::Video, key, 0, 0));

	auto inter = MakeVp8InterFrame();
	ASSERT_TRUE(a.Analyze(MakePacket(cmn::MediaType::Video, inter, 1, 1)));

	// Resolution from the previous key frame is retained.
	EXPECT_EQ(a.GetWidth(), 128);
	EXPECT_EQ(a.GetHeight(), 96);
}

TEST(BitstreamAnalyzer, Av1Resolution)
{
	BitstreamAnalyzer a;
	ASSERT_TRUE(a.Init(cmn::MediaCodecId::Av1));

	auto tu = MakeAv1KeyTemporalUnit();
	ASSERT_TRUE(a.Analyze(MakePacket(cmn::MediaType::Video, tu, 0, 0)));

	EXPECT_EQ(a.GetWidth(), 80);
	EXPECT_EQ(a.GetHeight(), 48);
	EXPECT_FALSE(a.IsMultiFrame());
}

TEST(BitstreamAnalyzer, H264ResolutionExtracted)
{
	BitstreamAnalyzer a;
	ASSERT_TRUE(a.Init(cmn::MediaCodecId::H264));

	auto au = WithStartCode(kH264Sps);
	ASSERT_TRUE(a.Analyze(MakePacket(cmn::MediaType::Video, au, 0, 0)));

	EXPECT_GT(a.GetWidth(), 0);
	EXPECT_GT(a.GetHeight(), 0);
}

TEST(BitstreamAnalyzer, H265ResolutionExtracted)
{
	BitstreamAnalyzer a;
	ASSERT_TRUE(a.Init(cmn::MediaCodecId::H265));

	auto au = WithStartCode(kH265Sps);
	ASSERT_TRUE(a.Analyze(MakePacket(cmn::MediaType::Video, au, 0, 0)));

	EXPECT_GT(a.GetWidth(), 0);
	EXPECT_GT(a.GetHeight(), 0);
}

// ============================================================================
// Layer C — multi-frame detection (no splitting; count + warn only)
// ============================================================================

TEST(BitstreamAnalyzer, AacSingleFrameIsNotMultiFrame)
{
	BitstreamAnalyzer a;
	ASSERT_TRUE(a.Init(cmn::MediaCodecId::Aac));

	auto frame = MakeAdtsFrame(20, 0xC0);
	ASSERT_TRUE(a.Analyze(MakePacket(cmn::MediaType::Audio, frame, 100, 100)));

	EXPECT_EQ(a.GetFrameCount(), 1);
	EXPECT_FALSE(a.IsMultiFrame());
}

TEST(BitstreamAnalyzer, AacMultiFrameBufferIsDetected)
{
	BitstreamAnalyzer a;
	ASSERT_TRUE(a.Init(cmn::MediaCodecId::Aac));

	auto frame0 = MakeAdtsFrame(16, 0xA0);
	auto frame1 = MakeAdtsFrame(24, 0xB0);
	std::vector<uint8_t> pes;
	pes.insert(pes.end(), frame0.begin(), frame0.end());
	pes.insert(pes.end(), frame1.begin(), frame1.end());

	// The whole buffer is analyzed as one packet; it is NOT split.
	ASSERT_TRUE(a.Analyze(MakePacket(cmn::MediaType::Audio, pes, 9000, 9000)));

	EXPECT_EQ(a.GetFrameCount(), 2);
	EXPECT_TRUE(a.IsMultiFrame());
	// The input timestamp is reported unchanged.
	EXPECT_EQ(a.GetPts(), 9000);
}

TEST(BitstreamAnalyzer, H264SinglePictureIsNotMultiFrame)
{
	BitstreamAnalyzer a;
	ASSERT_TRUE(a.Init(cmn::MediaCodecId::H264));

	// One IDR slice (first_mb_in_slice == 0).
	auto idr = WithStartCode({0x65, 0x88, 0x80});
	ASSERT_TRUE(a.Analyze(MakePacket(cmn::MediaType::Video, idr, 0, 0)));

	EXPECT_EQ(a.GetFrameCount(), 1);
	EXPECT_FALSE(a.IsMultiFrame());
}

TEST(BitstreamAnalyzer, H264TwoPicturesAreDetected)
{
	BitstreamAnalyzer a;
	ASSERT_TRUE(a.Init(cmn::MediaCodecId::H264));

	// Two pictures in one buffer: each slice has first_mb_in_slice == 0.
	auto au = WithStartCode({0x65, 0x88, 0x80});
	auto second = WithStartCode({0x41, 0x88, 0x80});
	au.insert(au.end(), second.begin(), second.end());

	ASSERT_TRUE(a.Analyze(MakePacket(cmn::MediaType::Video, au, 0, 0)));

	EXPECT_EQ(a.GetFrameCount(), 2);
	EXPECT_TRUE(a.IsMultiFrame());
}

// ============================================================================
// Layer D — IsFormatChanged (baseline + change detection)
//
// The first successful analysis only establishes the baseline and must report
// "no change". Video compares width/height; audio compares sample rate/channels.
// This is the method the HW video decoders rely on to decide a codec reinit.
// ============================================================================

TEST(BitstreamAnalyzer, FormatChangeFirstAnalysisIsBaseline)
{
	BitstreamAnalyzer a;
	ASSERT_TRUE(a.Init(cmn::MediaCodecId::Vp8));

	// First packet only sets the baseline -> no change reported.
	auto key = MakeVp8KeyFrame(128, 96);
	EXPECT_FALSE(a.IsFormatChanged(MakePacket(cmn::MediaType::Video, key, 0, 0)));
	EXPECT_EQ(a.GetWidth(), 128);
	EXPECT_EQ(a.GetHeight(), 96);
}

TEST(BitstreamAnalyzer, FormatChangeSameResolutionReportsNoChange)
{
	BitstreamAnalyzer a;
	ASSERT_TRUE(a.Init(cmn::MediaCodecId::Vp8));

	ASSERT_FALSE(a.IsFormatChanged(MakePacket(cmn::MediaType::Video, MakeVp8KeyFrame(128, 96), 0, 0)));  // baseline

	// Same resolution again -> no change.
	auto key2 = MakeVp8KeyFrame(128, 96);
	EXPECT_FALSE(a.IsFormatChanged(MakePacket(cmn::MediaType::Video, key2, 1, 1)));
}

TEST(BitstreamAnalyzer, FormatChangeResolutionChangeIsDetected)
{
	BitstreamAnalyzer a;
	ASSERT_TRUE(a.Init(cmn::MediaCodecId::Vp8));

	ASSERT_FALSE(a.IsFormatChanged(MakePacket(cmn::MediaType::Video, MakeVp8KeyFrame(128, 96), 0, 0)));  // baseline

	// A key frame with a different resolution -> change detected.
	auto key2 = MakeVp8KeyFrame(320, 240);
	EXPECT_TRUE(a.IsFormatChanged(MakePacket(cmn::MediaType::Video, key2, 1, 1)));
	EXPECT_EQ(a.GetWidth(), 320);
	EXPECT_EQ(a.GetHeight(), 240);
}

TEST(BitstreamAnalyzer, FormatChangeInterFrameAfterChangeReportsNoChange)
{
	BitstreamAnalyzer a;
	ASSERT_TRUE(a.Init(cmn::MediaCodecId::Vp8));

	ASSERT_FALSE(a.IsFormatChanged(MakePacket(cmn::MediaType::Video, MakeVp8KeyFrame(128, 96), 0, 0)));  // baseline
	ASSERT_TRUE(a.IsFormatChanged(MakePacket(cmn::MediaType::Video, MakeVp8KeyFrame(320, 240), 1, 1)));  // changed

	// An inter frame carries no resolution; the retained 320x240 must NOT look
	// like a change (a non-key frame must never reset width/height to 0).
	auto inter = MakeVp8InterFrame();
	EXPECT_FALSE(a.IsFormatChanged(MakePacket(cmn::MediaType::Video, inter, 2, 2)));
	EXPECT_EQ(a.GetWidth(), 320);
	EXPECT_EQ(a.GetHeight(), 240);
}

TEST(BitstreamAnalyzer, FormatChangeLateResolutionIsBaselineNotChange)
{
	BitstreamAnalyzer a;
	ASSERT_TRUE(a.Init(cmn::MediaCodecId::Vp8));

	// Stream starts mid-GOP: the first packet is an inter frame with no resolution.
	auto inter = MakeVp8InterFrame();
	EXPECT_FALSE(a.IsFormatChanged(MakePacket(cmn::MediaType::Video, inter, 0, 0)));
	EXPECT_EQ(a.GetWidth(), 0);

	// The first key frame provides the resolution. Since the baseline was still
	// all-zero, this first real detection reports "no change" (it is the baseline).
	EXPECT_FALSE(a.IsFormatChanged(MakePacket(cmn::MediaType::Video, MakeVp8KeyFrame(128, 96), 1, 1)));
	EXPECT_EQ(a.GetWidth(), 128);

	// A subsequent different resolution is then detected as a change.
	EXPECT_TRUE(a.IsFormatChanged(MakePacket(cmn::MediaType::Video, MakeVp8KeyFrame(320, 240), 2, 2)));
	EXPECT_EQ(a.GetWidth(), 320);
}

TEST(BitstreamAnalyzer, FormatChangeAudioSampleRateChangeIsDetected)
{
	BitstreamAnalyzer a;
	ASSERT_TRUE(a.Init(cmn::MediaCodecId::Aac));

	// 44100 Hz, 2 ch -> baseline.
	ASSERT_FALSE(a.IsFormatChanged(MakePacket(cmn::MediaType::Audio, MakeAdtsFrameCfg(20, 0xC0, 4, 2), 0, 0)));
	ASSERT_EQ(a.GetSampleRate(), 44100);

	// 48000 Hz, 2 ch -> change.
	EXPECT_TRUE(a.IsFormatChanged(MakePacket(cmn::MediaType::Audio, MakeAdtsFrameCfg(20, 0xC0, 3, 2), 1, 1)));
	EXPECT_EQ(a.GetSampleRate(), 48000);
}

TEST(BitstreamAnalyzer, FormatChangeAudioChannelChangeIsDetected)
{
	BitstreamAnalyzer a;
	ASSERT_TRUE(a.Init(cmn::MediaCodecId::Aac));

	// 44100 Hz, 2 ch -> baseline.
	ASSERT_FALSE(a.IsFormatChanged(MakePacket(cmn::MediaType::Audio, MakeAdtsFrameCfg(20, 0xC0, 4, 2), 0, 0)));
	ASSERT_EQ(a.GetChannels(), 2);

	// 44100 Hz, 1 ch -> change.
	EXPECT_TRUE(a.IsFormatChanged(MakePacket(cmn::MediaType::Audio, MakeAdtsFrameCfg(20, 0xC0, 4, 1), 1, 1)));
	EXPECT_EQ(a.GetChannels(), 1);
}

TEST(BitstreamAnalyzer, FormatChangeAudioSameFormatReportsNoChange)
{
	BitstreamAnalyzer a;
	ASSERT_TRUE(a.Init(cmn::MediaCodecId::Aac));

	ASSERT_FALSE(a.IsFormatChanged(MakePacket(cmn::MediaType::Audio, MakeAdtsFrameCfg(20, 0xC0, 4, 2), 0, 0)));  // baseline
	EXPECT_FALSE(a.IsFormatChanged(MakePacket(cmn::MediaType::Audio, MakeAdtsFrameCfg(24, 0xC0, 4, 2), 1, 1)));  // same format
}

TEST(BitstreamAnalyzer, FormatChangeFalseWhenAnalyzeFails)
{
	BitstreamAnalyzer a;
	ASSERT_TRUE(a.Init(cmn::MediaCodecId::Aac));

	ASSERT_FALSE(a.IsFormatChanged(MakePacket(cmn::MediaType::Audio, MakeAdtsFrameCfg(20, 0xC0, 4, 2), 0, 0)));  // baseline

	// A null packet makes Analyze() fail; IsFormatChanged() must report no change
	// (and must not touch the previously detected format).
	EXPECT_FALSE(a.IsFormatChanged(nullptr));
	EXPECT_EQ(a.GetSampleRate(), 44100);
	EXPECT_EQ(a.GetChannels(), 2);
}
