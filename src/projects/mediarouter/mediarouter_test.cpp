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
// `ApplyInBandSequenceHeaderToAv1Config` after the enhanced-RTMP (FLV) ingest path
// synthesized the lenient default av1C blob `0x81 0x00 0x00 0x00`. The helper copies the
// matchable fields (per AV1 ISOBMFF binding v1.3.0 section 2.3.4 (Semantics)) from the
// in-band Sequence Header summary onto the av1C. `initial_presentation_delay_*` is NOT
// among them: it is an av1C-only field with no Sequence Header "SHALL match" rule, so the
// helper must leave it untouched - the tests below pin down both behaviours.
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
	// op-0 initial display delay is set here only to prove it is NOT copied into the av1C
	// (it is a distinct field from av1C `initial_presentation_delay`, with no spec match rule).
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

	// `initial_presentation_delay` must NOT be copied from the Sequence Header's
	// `initial_display_delay` (distinct fields, no match rule per AV1 ISOBMFF binding v1.3.0
	// section 2.3.4 (Semantics)); the av1C keeps its synthesized default of 0/0.
	EXPECT_EQ(av1_config->InitialPresentationDelayPresent(), 0);
	EXPECT_EQ(av1_config->InitialPresentationDelayMinusOne(), 0);
}

TEST(MediaRouterNormalizeAv1InBandSh, InBandSequenceHeaderLeavesInitialPresentationDelayUntouched)
{
	std::shared_ptr<AV1DecoderConfigurationRecord> av1_config;
	ASSERT_NO_FATAL_FAILURE(MakeSynthesizedDefaultAv1Config(av1_config));

	// Seed the av1C with a non-zero presentation delay BEFORE the helper runs.
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
	summary.initial_display_delay_present_for_op_0 = 0;
	summary.initial_display_delay_minus_1_for_op_0 = 0;

	MediaRouterNormalize::ApplyInBandSequenceHeaderToAv1Config(av1_config, summary);

	// The helper must NOT touch `initial_presentation_delay` - it is a distinct field from the
	// Sequence Header's `initial_display_delay` with no spec match rule (AV1 ISOBMFF binding
	// v1.3.0 section 2.3.4 (Semantics)). The seeded 1/5 is preserved.
	EXPECT_EQ(av1_config->InitialPresentationDelayPresent(), 1);
	EXPECT_EQ(av1_config->InitialPresentationDelayMinusOne(), 5);
}
