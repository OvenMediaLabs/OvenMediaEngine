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

#include "transcoder_modules.h"

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
