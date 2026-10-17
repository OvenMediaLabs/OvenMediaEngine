//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  Covers: AVCDecoderConfigurationRecord::Equals() across a record parsed from
//  an avcC and a record built from the same SPS/PPS NAL units, and the profile,
//  level and bit depth the record reports
//
//==============================================================================
#include <base/ovlibrary/bit_writer.h>
#include <gtest/gtest.h>
#include <modules/bitstream/h264/h264_decoder_configuration_record.h>
#include <modules/bitstream/nalu/nal_unit_test_helpers.h>

#include <algorithm>

namespace
{
	using ome_test::ApplyEmulationPrevention;
	using ome_test::ToBytes;
	using ome_test::WriteTrailing;
	using ome_test::WriteUE;

	// A real 1920x1080 High profile (100) SPS, 4:2:0 8 bit, so the avcC that carries it writes
	// chroma_format 1 and both bit depths 0.
	const std::vector<uint8_t> kHighProfileSps = {
		0x67, 0x64, 0x00, 0x28, 0xac, 0xd9, 0x40, 0x78, 0x02, 0x27, 0xe5, 0x84,
		0x00, 0x00, 0x03, 0x00, 0x04, 0x00, 0x00, 0x03, 0x00, 0xf0, 0x3c, 0x60, 0xc9, 0x20};

	// A real Baseline profile (66) SPS. ParseSPS infers chroma_format_idc 1 for it while the avcC
	// carries no trailer at all, so this pair disagrees in the opposite direction.
	const std::vector<uint8_t> kBaselineSps = {
		0x67, 0x42, 0xC0, 0x2A, 0x95, 0xA0, 0x1E, 0x00, 0x89, 0xF9, 0x70, 0x11,
		0x00, 0x00, 0x03, 0x03, 0xE8, 0x00, 0x01, 0xD4, 0xC0, 0xE0, 0x00, 0x00,
		0x13, 0x12, 0xD0, 0x00, 0x04, 0xC4, 0xB5, 0xBB, 0xCB, 0x82, 0x80};

	const std::vector<uint8_t> kPps = {0x68, 0xCE, 0x3C, 0x80};

	// The same PPS with pps_id re-encoded as 1 (ue(0) is "1", ue(1) is "010"): every other field
	// keeps its value, only the bytes differ
	const std::vector<uint8_t> kOtherPps = {0x68, 0x53, 0x8F, 0x20};

	std::shared_ptr<ov::Data> AsData(const std::vector<uint8_t> &bytes)
	{
		return std::make_shared<ov::Data>(bytes.data(), bytes.size());
	}

	// The profiles whose SPS codes chroma_format_idc and the bit depths (ITU-T H.264 7.3.2.1.1)
	const std::vector<uint8_t> kProfilesCodingTheChromaFormat = {100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139, 134, 135};

	bool CodesTheChromaFormat(uint8_t profile_idc)
	{
		return std::find(kProfilesCodingTheChromaFormat.begin(), kProfilesCodingTheChromaFormat.end(), profile_idc) !=
			   kProfilesCodingTheChromaFormat.end();
	}

	// A 640x320 SPS with the given profile, constraint flags, level and bit depth
	// (ITU-T H.264 7.3.2.1.1)
	std::vector<uint8_t> MakeSps(uint8_t profile_idc, uint8_t constraint_flags, uint8_t level_idc,
								 uint32_t bit_depth_minus8 = 0)
	{
		ov::BitWriter w(64);

		w.WriteBits(8, profile_idc);
		w.WriteBits(8, constraint_flags);
		w.WriteBits(8, level_idc);
		WriteUE(w, 0);	// seq_parameter_set_id

		if (CodesTheChromaFormat(profile_idc))
		{
			WriteUE(w, 1);					// chroma_format_idc (4:2:0)
			WriteUE(w, bit_depth_minus8);	// bit_depth_luma_minus8
			WriteUE(w, bit_depth_minus8);	// bit_depth_chroma_minus8
			w.WriteBits(1, 0);				// qpprime_y_zero_transform_bypass_flag
			w.WriteBits(1, 0);				// seq_scaling_matrix_present_flag
		}

		WriteUE(w, 4);		// log2_max_frame_num_minus4
		WriteUE(w, 0);		// pic_order_cnt_type
		WriteUE(w, 4);		// log2_max_pic_order_cnt_lsb_minus4
		WriteUE(w, 1);		// max_num_ref_frames
		w.WriteBits(1, 0);	// gaps_in_frame_num_value_allowed_flag
		WriteUE(w, 39);		// pic_width_in_mbs_minus1
		WriteUE(w, 19);		// pic_height_in_map_units_minus1
		w.WriteBits(1, 1);	// frame_mbs_only_flag
		w.WriteBits(1, 1);	// direct_8x8_inference_flag
		w.WriteBits(1, 0);	// frame_cropping_flag
		w.WriteBits(1, 0);	// vui_parameters_present_flag

		WriteTrailing(w);

		std::vector<uint8_t> nal = {0x67};	// nal_ref_idc(3) + nal_unit_type(7, SPS)
		const auto ebsp = ApplyEmulationPrevention(ToBytes(w));
		nal.insert(nal.end(), ebsp.begin(), ebsp.end());

		return nal;
	}

	// ISO/IEC 14496-15 5.2.4.1: the chroma format, the bit depths and the SPS extension count are
	// written only for profile_idc 100/110/122/144, which is the asymmetry this file is about.
	std::shared_ptr<ov::Data> MakeAvcC(const std::vector<uint8_t> &sps, const std::vector<uint8_t> &pps,
									   uint8_t version = 0x01, uint8_t length_minus_one = 3,
									   bool with_sps_ext = false)
	{
		std::vector<uint8_t> out;

		out.push_back(version);	 // configurationVersion
		out.push_back(sps[1]);	 // AVCProfileIndication
		out.push_back(sps[2]);	 // profile_compatibility
		out.push_back(sps[3]);	 // AVCLevelIndication
		out.push_back(static_cast<uint8_t>(0xFC | length_minus_one));  // 111111b + lengthSizeMinusOne
		out.push_back(0xE1);	// 111b + numOfSequenceParameterSets(1)
		out.push_back(static_cast<uint8_t>(sps.size() >> 8));
		out.push_back(static_cast<uint8_t>(sps.size() & 0xFF));
		out.insert(out.end(), sps.begin(), sps.end());
		out.push_back(0x01);	// numOfPictureParameterSets
		out.push_back(static_cast<uint8_t>(pps.size() >> 8));
		out.push_back(static_cast<uint8_t>(pps.size() & 0xFF));
		out.insert(out.end(), pps.begin(), pps.end());

		const uint8_t profile = sps[1];
		if ((profile == 100) || (profile == 110) || (profile == 122) || (profile == 144))
		{
			out.push_back(0xFC | 0x01);	 // 111111b + chroma_format(1, 4:2:0)
			out.push_back(0xF8 | 0x00);	 // 11111b + bit_depth_luma_minus8(0)
			out.push_back(0xF8 | 0x00);	 // 11111b + bit_depth_chroma_minus8(0)

			if (with_sps_ext == true)
			{
				out.push_back(0x01);				  // numOfSequenceParameterSetExt
				out.push_back(0x00); out.push_back(0x02);  // sequenceParameterSetExtLength
				out.push_back(0x6D); out.push_back(0x01);
			}
			else
			{
				out.push_back(0x00);
			}
		}

		return AsData(out);
	}

	// What a provider does with a sequence header
	std::shared_ptr<AVCDecoderConfigurationRecord> ParsedRecord(const std::vector<uint8_t> &sps,
																const std::vector<uint8_t> &pps = kPps)
	{
		auto record = std::make_shared<AVCDecoderConfigurationRecord>();
		if (record->Parse(MakeAvcC(sps, pps)) == false)
		{
			return nullptr;
		}

		return record;
	}

	// What MediaRouterNormalize does with the in-band parameter sets of an access unit
	std::shared_ptr<AVCDecoderConfigurationRecord> BuiltRecord(const std::vector<uint8_t> &sps,
															  const std::vector<uint8_t> &pps = kPps)
	{
		auto record = std::make_shared<AVCDecoderConfigurationRecord>();
		if ((record->AddSPS(AsData(sps)) == false) || (record->AddPPS(AsData(pps)) == false))
		{
			return nullptr;
		}

		return record;
	}
}  // namespace

// MediaRouterNormalize compares the record parsed from the sequence header against one rebuilt
// from the in-band SPS/PPS of the access units that follow. An inequality republishes the track
// version and swaps the original avcC for what Serialize() writes, so the two have to compare
// equal.
TEST(AVCDecoderConfigurationRecord, AParsedHighProfileRecordEqualsOneBuiltFromTheSameSps)
{
	auto parsed = ParsedRecord(kHighProfileSps);
	auto built	= BuiltRecord(kHighProfileSps);

	ASSERT_NE(parsed, nullptr);
	ASSERT_NE(built, nullptr);
	ASSERT_TRUE(parsed->IsValid());
	ASSERT_TRUE(built->IsValid());
	ASSERT_EQ(parsed->ProfileIndication(), 100);

	EXPECT_TRUE(parsed->Equals(built))
		<< "the avcC trailer is only in the parsed record, and it is coded in the SPS anyway";
	EXPECT_TRUE(built->Equals(parsed)) << "and the comparison has to be symmetric";
}

// The mirror case: for a profile with no avcC trailer the parsed record keeps chroma_format 0
// while the SPS infers 4:2:0, so populating the built record from the SPS would break this one
TEST(AVCDecoderConfigurationRecord, AParsedBaselineRecordEqualsOneBuiltFromTheSameSps)
{
	auto parsed = ParsedRecord(kBaselineSps);
	auto built	= BuiltRecord(kBaselineSps);

	ASSERT_NE(parsed, nullptr);
	ASSERT_NE(built, nullptr);
	ASSERT_EQ(parsed->ProfileIndication(), 66);

	EXPECT_TRUE(parsed->Equals(built));
	EXPECT_TRUE(built->Equals(parsed));
}

// Dropping the trailer must not cost the comparison its teeth: every value it gave up is coded in
// the SPS, so changing one changes the SPS payload
TEST(AVCDecoderConfigurationRecord, ADifferentSpsIsNotEqual)
{
	auto high	  = BuiltRecord(kHighProfileSps);
	auto baseline = BuiltRecord(kBaselineSps);

	ASSERT_NE(high, nullptr);
	ASSERT_NE(baseline, nullptr);

	EXPECT_FALSE(high->Equals(baseline));
	EXPECT_FALSE(baseline->Equals(high));
}

TEST(AVCDecoderConfigurationRecord, ASpsThatDiffersInOneByteIsNotEqual)
{
	auto modified_sps = kHighProfileSps;
	// The last byte of the SPS payload, so profile, level and resolution all stay put
	modified_sps.back() ^= 0x01;

	auto original = BuiltRecord(kHighProfileSps);
	auto modified = BuiltRecord(modified_sps);

	ASSERT_NE(original, nullptr);
	ASSERT_NE(modified, nullptr);
	ASSERT_EQ(original->ProfileIndication(), modified->ProfileIndication());

	EXPECT_FALSE(original->Equals(modified));
}

TEST(AVCDecoderConfigurationRecord, ADifferentPpsIsNotEqual)
{
	auto original = BuiltRecord(kHighProfileSps, kPps);
	auto other	  = BuiltRecord(kHighProfileSps, kOtherPps);

	ASSERT_NE(original, nullptr);
	ASSERT_NE(other, nullptr);

	EXPECT_FALSE(original->Equals(other));
}

TEST(AVCDecoderConfigurationRecord, RejectsNullptrAndAnotherRecordType)
{
	auto record = BuiltRecord(kHighProfileSps);
	ASSERT_NE(record, nullptr);

	EXPECT_FALSE(record->Equals(nullptr));

	class ForeignRecord : public DecoderConfigurationRecord
	{
	public:
		bool Parse(const std::shared_ptr<const ov::Data> & /*data*/) override { return true; }
		bool IsValid() const override { return true; }
		bool Equals(const std::shared_ptr<DecoderConfigurationRecord> & /*other*/) override { return false; }
		ov::String GetCodecsParameter() const override { return ""; }

	protected:
		std::shared_ptr<const ov::Data> Serialize() override { return nullptr; }
	};

	EXPECT_FALSE(record->Equals(std::make_shared<ForeignRecord>()));
}

// configurationVersion, lengthSizeMinusOne and the SPS extensions are filled in by Parse() alone,
// exactly like the chroma format and the bit depths, so comparing any of them would make a parsed
// record differ from a built one for good
TEST(AVCDecoderConfigurationRecord, IgnoresTheFieldsOnlyParseCanFill)
{
	struct Case
	{
		const char *name;
		uint8_t version;
		uint8_t length_minus_one;
		bool with_sps_ext;
	};

	for (const auto &test : {Case{"a non-conformant configurationVersion", 0x00, 3, false},
							 Case{"a two byte NAL length prefix", 0x01, 1, false},
							 Case{"an SPS extension the built record cannot carry", 0x01, 3, true}})
	{
		auto parsed = std::make_shared<AVCDecoderConfigurationRecord>();
		ASSERT_TRUE(parsed->Parse(MakeAvcC(kHighProfileSps, kPps, test.version,
										   test.length_minus_one, test.with_sps_ext)))
			<< test.name;

		auto built = BuiltRecord(kHighProfileSps);
		ASSERT_NE(built, nullptr);

		EXPECT_TRUE(parsed->Equals(built)) << test.name;
	}
}

TEST(AVCDecoderConfigurationRecord, NamesTheProfileAndTheLevel)
{
	struct Case
	{
		uint8_t profile_idc;
		uint8_t constraint_flags;
		uint8_t level_idc;
		const char *profile;
		const char *level;
	};

	for (const auto &test : {Case{66, 0x00, 30, "Baseline", "3"},
							 Case{66, 0x40, 31, "Constrained Baseline", "3.1"},
							 Case{77, 0x00, 40, "Main", "4"},
							 Case{88, 0x00, 21, "Extended", "2.1"},
							 Case{100, 0x00, 41, "High", "4.1"},
							 Case{100, 0x08, 42, "Progressive High", "4.2"},
							 Case{100, 0x0C, 31, "Constrained High", "3.1"},
							 Case{110, 0x00, 50, "High 10", "5"},
							 Case{110, 0x08, 50, "Progressive High 10", "5"},
							 Case{110, 0x10, 51, "High 10 Intra", "5.1"},
							 Case{122, 0x00, 52, "High 4:2:2", "5.2"},
							 Case{122, 0x10, 52, "High 4:2:2 Intra", "5.2"},
							 Case{244, 0x00, 62, "High 4:4:4 Predictive", "6.2"},
							 Case{244, 0x10, 62, "High 4:4:4 Intra", "6.2"},
							 Case{44, 0x00, 10, "CAVLC 4:4:4 Intra", "1"},
							 // Level 1b: 11 with constraint_set3_flag in Baseline, 9 in High
							 Case{66, 0x10, 11, "Baseline", "1b"},
							 Case{100, 0x00, 9, "High", "1b"},
							 Case{100, 0x10, 11, "High", "1.1"},
							 // Values the specification does not define have no name
							 Case{83, 0x00, 30, "", "3"},
							 Case{100, 0x00, 14, "High", ""},
							 Case{100, 0x00, 0, "High", ""}})
	{
		auto record = BuiltRecord(MakeSps(test.profile_idc, test.constraint_flags, test.level_idc));
		ASSERT_NE(record, nullptr) << "profile_idc " << static_cast<int>(test.profile_idc);

		EXPECT_EQ(record->GetProfileName(), test.profile) << "profile_idc " << static_cast<int>(test.profile_idc);
		EXPECT_EQ(record->GetLevelName(), test.level) << "level_idc " << static_cast<int>(test.level_idc);
	}
}

TEST(AVCDecoderConfigurationRecord, TakesTheBitDepthFromTheSps)
{
	auto high10 = BuiltRecord(MakeSps(110, 0x00, 40, 2));
	ASSERT_NE(high10, nullptr);
	EXPECT_EQ(high10->GetBitDepth(), 10);

	auto baseline = BuiltRecord(kBaselineSps);
	ASSERT_NE(baseline, nullptr);
	EXPECT_EQ(baseline->GetBitDepth(), 8) << "a profile that does not code the bit depth is 8 bit";

	// MakeAvcC writes bit_depth_luma_minus8 0 in the trailer, so the SPS value has to win
	auto parsed = ParsedRecord(MakeSps(110, 0x00, 40, 2));
	ASSERT_NE(parsed, nullptr);
	ASSERT_EQ(parsed->BitDepthLumaMinus8(), 0);
	EXPECT_EQ(parsed->GetBitDepth(), 10);
}

// Missing one of these would misread every field that follows, the resolution included
TEST(AVCDecoderConfigurationRecord, ReadsTheChromaFormatBlockOfEveryProfileThatCodesIt)
{
	for (auto profile_idc : kProfilesCodingTheChromaFormat)
	{
		const auto nal = MakeSps(profile_idc, 0x00, 40, 2);

		H264SPS sps;
		ASSERT_TRUE(H264Parser::ParseSPS(nal.data(), nal.size(), sps)) << "profile_idc " << static_cast<int>(profile_idc);

		EXPECT_EQ(sps.GetWidth(), 640U) << "profile_idc " << static_cast<int>(profile_idc);
		EXPECT_EQ(sps.GetHeight(), 320U) << "profile_idc " << static_cast<int>(profile_idc);
		EXPECT_EQ(sps.GetBitDepthLuma(), 10) << "profile_idc " << static_cast<int>(profile_idc);
	}
}

// bit_depth_luma_minus8 and bit_depth_chroma_minus8 are 0 to 6
TEST(AVCDecoderConfigurationRecord, RejectsAnSpsWithAnOutOfRangeBitDepth)
{
	auto highest = MakeSps(244, 0x00, 40, 6);
	H264SPS sps;
	ASSERT_TRUE(H264Parser::ParseSPS(highest.data(), highest.size(), sps));
	EXPECT_EQ(sps.GetBitDepthLuma(), 14);

	auto out_of_range = MakeSps(244, 0x00, 40, 7);
	EXPECT_FALSE(H264Parser::ParseSPS(out_of_range.data(), out_of_range.size(), sps));
	EXPECT_EQ(BuiltRecord(out_of_range), nullptr);
}
