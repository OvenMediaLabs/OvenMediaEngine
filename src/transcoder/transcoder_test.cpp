//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  Covers: codec module table lookups
//
//  NOTE: Transcoder tests may require FFmpeg initialization. Tests that do NOT
//        require actual codec instances can be added here.
//
//==============================================================================
#include <gtest/gtest.h>

#include <algorithm>
#include <thread>

#include "transcoder_encoder.h"
#include "transcoder_modules.h"
#include "transcoder_whisper_model_registry.h"

// The registrations below are the software ones, which are always present; the
// hardware modules need a device and are not part of these expectations.
class TranscodeModulesTest : public ::testing::Test
{
protected:
	tc::TranscodeModules *_modules = tc::TranscodeModules::GetInstance();
};

TEST_F(TranscodeModulesTest, RejectsCodecTheModuleDidNotRegister)
{
	// The DEFAULT video decoder registers H264, H265, VP8 and AV1. Media type, module,
	// device and direction all match for VP9, so only the codec can reject it.
	EXPECT_EQ(_modules->GetModule(/*coder_type=*/false, cmn::MediaCodecId::Vp9, cmn::MediaCodecModuleId::DEFAULT, 0), nullptr);

	// LIBAOM registers AV1 alone.
	EXPECT_EQ(_modules->GetModule(/*coder_type=*/true, cmn::MediaCodecId::H264, cmn::MediaCodecModuleId::LIBAOM, 0), nullptr);
}

TEST_F(TranscodeModulesTest, FindsRegisteredCodec)
{
	EXPECT_NE(_modules->GetModule(/*coder_type=*/false, cmn::MediaCodecId::H264, cmn::MediaCodecModuleId::DEFAULT, 0), nullptr);
	EXPECT_NE(_modules->GetModule(/*coder_type=*/true, cmn::MediaCodecId::Av1, cmn::MediaCodecModuleId::LIBAOM, 0), nullptr);
	EXPECT_NE(_modules->GetModule(/*coder_type=*/true, cmn::MediaCodecId::Jpeg, cmn::MediaCodecModuleId::DEFAULT, 0), nullptr);
}

TEST_F(TranscodeModulesTest, KeepsDecoderAndEncoderApart)
{
	// The DEFAULT audio module decodes only, the FDKAAC module encodes only.
	EXPECT_NE(_modules->GetModule(/*coder_type=*/false, cmn::MediaCodecId::Aac, cmn::MediaCodecModuleId::DEFAULT, 0), nullptr);
	EXPECT_EQ(_modules->GetModule(/*coder_type=*/true, cmn::MediaCodecId::Aac, cmn::MediaCodecModuleId::DEFAULT, 0), nullptr);

	EXPECT_NE(_modules->GetModule(/*coder_type=*/true, cmn::MediaCodecId::Aac, cmn::MediaCodecModuleId::FDKAAC, 0), nullptr);
	EXPECT_EQ(_modules->GetModule(/*coder_type=*/false, cmn::MediaCodecId::Aac, cmn::MediaCodecModuleId::FDKAAC, 0), nullptr);
}

TEST_F(TranscodeModulesTest, FindsCodecsRegisteredForTheDefaultModule)
{
	EXPECT_NE(_modules->GetModule(/*coder_type=*/false, cmn::MediaCodecId::Mp3, cmn::MediaCodecModuleId::DEFAULT, 0), nullptr);
	EXPECT_NE(_modules->GetModule(/*coder_type=*/true, cmn::MediaCodecId::Whisper, cmn::MediaCodecModuleId::DEFAULT, 0), nullptr);
}

TEST_F(TranscodeModulesTest, RejectsUnregisteredDevice)
{
	EXPECT_EQ(_modules->GetModule(/*coder_type=*/false, cmn::MediaCodecId::H264, cmn::MediaCodecModuleId::DEFAULT, 1), nullptr);
}

// Whisper used to run on an NVIDIA GPU and <Modules>nv:N</Modules> picked which
// one. Inference is CPU-only now, so the key is ignored - but a configuration
// carrying it must still resolve to a usable encoder instead of none at all.
class WhisperCandidateTest : public ::testing::Test
{
protected:
	static std::shared_ptr<MediaTrack> MakeWhisperTrack(const char *modules)
	{
		auto track = std::make_shared<MediaTrack>();
		track->SetId(1);
		track->SetMediaType(cmn::MediaType::Subtitle);
		track->SetCodecId(cmn::MediaCodecId::Whisper);
		track->SetCodecModules(modules);
		return track;
	}
};

TEST_F(WhisperCandidateTest, ResolvesToTheDefaultModuleWithoutModules)
{
	auto candidates = TranscodeEncoder::GetCandidates(/*hwaccels_enable=*/false, "", MakeWhisperTrack(""));

	ASSERT_NE(candidates, nullptr);
	ASSERT_EQ(candidates->size(), 1u);
	EXPECT_EQ(candidates->at(0)->GetModuleId(), cmn::MediaCodecModuleId::DEFAULT);
	EXPECT_EQ(candidates->at(0)->GetDeviceId(), 0);
}

// The Whisper special case must not swallow <Modules> on other non-video
// codecs: an audio rendition that picks fdkaac has to keep that choice.
TEST_F(WhisperCandidateTest, LeavesAudioModuleSelectionAlone)
{
	auto track = std::make_shared<MediaTrack>();
	track->SetId(2);
	track->SetMediaType(cmn::MediaType::Audio);
	track->SetCodecId(cmn::MediaCodecId::Aac);
	track->SetCodecModules("fdkaac");

	auto candidates = TranscodeEncoder::GetCandidates(/*hwaccels_enable=*/false, "", track);

	ASSERT_NE(candidates, nullptr);
	ASSERT_EQ(candidates->size(), 1u);
	EXPECT_EQ(candidates->at(0)->GetModuleId(), cmn::MediaCodecModuleId::FDKAAC);
}

TEST_F(WhisperCandidateTest, IgnoresAGpuModuleSelection)
{
	// Both with and without HWAccels enabled: an nv:N left over from a GPU
	// configuration must not leave the track with an empty candidate list.
	for (const bool hwaccels_enable : {false, true})
	{
		auto candidates = TranscodeEncoder::GetCandidates(hwaccels_enable, "", MakeWhisperTrack("nv:0"));

		ASSERT_NE(candidates, nullptr) << "hwaccels_enable " << hwaccels_enable;
		ASSERT_EQ(candidates->size(), 1u) << "hwaccels_enable " << hwaccels_enable;
		EXPECT_EQ(candidates->at(0)->GetModuleId(), cmn::MediaCodecModuleId::DEFAULT);
		EXPECT_EQ(candidates->at(0)->GetCodecId(), cmn::MediaCodecId::Whisper);
	}
}

// The thread count handed to one STT track when <Threads> is omitted.
TEST(WhisperThreadDefault, StaysWithinTheMachine)
{
	const int32_t hardware_threads = WhisperModelRegistry::GetHardwareThreads();
	const int32_t threads		   = WhisperModelRegistry::GetDefaultThreadCount();

	EXPECT_GE(threads, 1);
	EXPECT_LE(threads, 8) << "whisper scales poorly past 8 threads";
	EXPECT_LE(threads, hardware_threads) << "must never oversubscribe the machine";
}

// With no live states the share is just the request capped by the budget;
// the per-track division is exercised by the registry with real models.
TEST(WhisperThreadShare, IsCappedByTheBudget)
{
	auto *registry = WhisperModelRegistry::GetInstance();

	registry->SetMaxThreads(3);
	EXPECT_EQ(registry->GetThreadShare(8), 3);
	EXPECT_EQ(registry->GetThreadShare(2), 2);
	EXPECT_GE(registry->GetThreadShare(0), 1) << "a request of 0 means the default, never zero threads";

	// Back to the default budget so other tests see the registry as configured.
	registry->SetMaxThreads(0);
	EXPECT_EQ(registry->GetThreadShare(8), std::min(8, WhisperModelRegistry::GetHardwareThreads()));
}
