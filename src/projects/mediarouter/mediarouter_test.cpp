//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  tests/mediarouter/test_mediarouter_placeholder.cpp
//  Covers: MediaRouter stream routing, normalization, application management
//
//  NOTE: Skeleton. MediaRouter requires a running Orchestrator context.
//        Set up an integration-style fixture to instantiate both before
//        writing real tests here.
//
//==============================================================================
#include <base/ovlibrary/data.h>
#include <gtest/gtest.h>
#include <modules/bitstream/av1/av1_decoder_configuration_record.h>
#include <modules/bitstream/av1/av1_types.h>

#include "mediarouter_nomalize.h"

TEST(MediaRouterPlaceholder, TodoImplementTests)
{
	GTEST_SKIP() << "MediaRouter tests not yet implemented - see tests/mediarouter/";
}

// ---------------------------------------------------------------------------
// Regression coverage for the in-band Sequence Header to av1C field sync.
//
// The CodedFrames branch of `MediaRouterNormalize::ProcessAV1OBUStream` enters
// `ApplyInBandSequenceHeaderToAv1Config` after `flv_video_parser::ParseAV1`
// synthesized the lenient default av1C blob `0x81 0x00 0x00 0x00`. The helper
// must copy every cross-checked field (per AV1 ISOBMFF binding v1.2.0 section
// 2.3.2) from the in-band Sequence Header summary onto the av1C - including
// the `initial_presentation_delay_*` pair that was historically missed and
// caused the synthesized av1C to go stale on streams with op-0 delay != 0.
// ---------------------------------------------------------------------------
namespace
{
	// Returns via out-param so the parse can be a hard precondition: gtest `ASSERT_*` expands to
	// `return;` and is only usable in a void-returning function. Call through
	// `ASSERT_NO_FATAL_FAILURE` so a parse failure stops the test instead of cascading.
	void MakeSynthesizedDefaultAv1Config(std::shared_ptr<AV1DecoderConfigurationRecord> &out)
	{
		const std::vector<uint8_t> bytes = {0x81, 0x00, 0x00, 0x00};
		auto data						 = std::make_shared<ov::Data>(bytes.data(), bytes.size());
		auto record						 = std::make_shared<AV1DecoderConfigurationRecord>();
		ASSERT_TRUE(record->Parse(data));
		out = record;
	}
}  // namespace

TEST(MediaRouterNormalizeAv1InBandSh, InBandSequenceHeaderUpdatesAllAv1ConfigFields)
{
	std::shared_ptr<AV1DecoderConfigurationRecord> av1_config;
	ASSERT_NO_FATAL_FAILURE(MakeSynthesizedDefaultAv1Config(av1_config));

	// Pre-condition: synthesized lenient defaults.
	ASSERT_EQ(av1_config->SeqProfile(), 0);
	ASSERT_EQ(av1_config->SeqLevelIdx0(), 0);
	ASSERT_EQ(av1_config->InitialPresentationDelayPresent(), 0);
	ASSERT_EQ(av1_config->InitialPresentationDelayMinusOne(), 0);

	Av1SequenceHeaderSummary summary;
	summary.parsed								   = true;
	summary.seq_profile							   = 1;
	summary.seq_level_idx_0						   = 8;
	summary.seq_tier_0							   = 1;
	summary.high_bitdepth						   = 1;
	summary.twelve_bit							   = 0;
	summary.monochrome							   = 1;
	summary.chroma_subsampling_x				   = 1;
	summary.chroma_subsampling_y				   = 0;
	summary.chroma_sample_position				   = 2;
	// REGRESSION GUARD: op-0 initial display delay present + non-zero value
	// must propagate to the av1C via SetInitialPresentationDelay().
	summary.initial_display_delay_present_for_op_0 = 1;
	summary.initial_display_delay_minus_1_for_op_0 = 7;

	MediaRouterNormalize::ApplyInBandSequenceHeaderToAv1Config(av1_config, summary);

	EXPECT_EQ(av1_config->SeqProfile(), summary.seq_profile);
	EXPECT_EQ(av1_config->SeqLevelIdx0(), summary.seq_level_idx_0);
	EXPECT_EQ(av1_config->SeqTier0(), summary.seq_tier_0);
	EXPECT_EQ(av1_config->HighBitdepth(), summary.high_bitdepth);
	EXPECT_EQ(av1_config->TwelveBit(), summary.twelve_bit);
	EXPECT_EQ(av1_config->Monochrome(), summary.monochrome);
	EXPECT_EQ(av1_config->ChromaSubsamplingX(), summary.chroma_subsampling_x);
	EXPECT_EQ(av1_config->ChromaSubsamplingY(), summary.chroma_subsampling_y);
	EXPECT_EQ(av1_config->ChromaSamplePosition(), summary.chroma_sample_position);

	// Regression: synthesized av1C must mirror the in-band SH op-0 delay signaling
	// so the downstream AV1DecoderConfigurationRecord::ValidateConfigObus() cross-check
	// (AV1 ISOBMFF binding v1.2.0 section 2.3.2 "initial_presentation_delay_minus_one,
	// when present, all shall match") stays consistent with the actual bitstream.
	EXPECT_EQ(av1_config->InitialPresentationDelayPresent(), 1);
	EXPECT_EQ(av1_config->InitialPresentationDelayMinusOne(), 7);
}

TEST(MediaRouterNormalizeAv1InBandSh, InBandSequenceHeaderClearsInitialPresentationDelayWhenAbsent)
{
	std::shared_ptr<AV1DecoderConfigurationRecord> av1_config;
	ASSERT_NO_FATAL_FAILURE(MakeSynthesizedDefaultAv1Config(av1_config));

	// Seed the av1C with a non-zero op-0 presentation delay BEFORE the helper runs.
	// This forces the test to exercise the clear-from-non-zero path: a tautological
	// 0/0 -> 0/0 assertion would also pass if the helper omitted the
	// SetInitialPresentationDelay() call entirely, so it must not start from default.
	av1_config->SetInitialPresentationDelay(true, 5);
	ASSERT_EQ(av1_config->InitialPresentationDelayPresent(), 1);
	ASSERT_EQ(av1_config->InitialPresentationDelayMinusOne(), 5);

	Av1SequenceHeaderSummary summary;
	summary.parsed								   = true;
	summary.seq_profile							   = 0;
	summary.seq_level_idx_0						   = 4;
	summary.seq_tier_0							   = 0;
	summary.high_bitdepth						   = 0;
	summary.twelve_bit							   = 0;
	summary.monochrome							   = 0;
	summary.chroma_subsampling_x				   = 1;
	summary.chroma_subsampling_y				   = 1;
	summary.chroma_sample_position				   = 0;
	// op-0 delay absent in the in-band Sequence Header: helper must encode
	// `present = 0` and wipe the stale `initial_presentation_delay_minus_one`
	// back to 0 (av1_decoder_configuration_record.cpp:461 false branch).
	summary.initial_display_delay_present_for_op_0 = 0;
	summary.initial_display_delay_minus_1_for_op_0 = 0;

	MediaRouterNormalize::ApplyInBandSequenceHeaderToAv1Config(av1_config, summary);

	// Regression: the helper must reset the seeded 1/5 state back to 0/0.
	// If SetInitialPresentationDelay() is omitted the av1C keeps 1/5 and this fails.
	EXPECT_EQ(av1_config->InitialPresentationDelayPresent(), 0);
	EXPECT_EQ(av1_config->InitialPresentationDelayMinusOne(), 0);
}
