//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  Covers: AVCDecoderConfigurationRecord::Equals() across a record parsed from
//  an avcC and a record built from the same SPS/PPS NAL units
//
//==============================================================================
#include <gtest/gtest.h>
#include <modules/bitstream/h264/h264_decoder_configuration_record.h>

namespace
{
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
