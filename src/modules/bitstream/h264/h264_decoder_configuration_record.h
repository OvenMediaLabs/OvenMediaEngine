#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <base/common_types.h>
#include <base/info/decoder_configuration_record.h>
#include "h264_parser.h"

// ISO 14496-15, 5.2.4.1
//	aligned(8) class AVCDecoderConfigurationRecord {
//		unsigned int(8) configurationVersion = 1;
//		unsigned int(8) AVCProfileIndication;
//		unsigned int(8) profile_compatibility;
//		unsigned int(8) AVCLevelIndication;
//		bit(6) reserved = ‘111111’b;
//		unsigned int(2) lengthSizeMinusOne;
//		bit(3) reserved = ‘111’b;
//		unsigned int(5) numOfSequenceParameterSets;
//		for (i=0; i< numOfSequenceParameterSets;  i++) {
//			unsigned int(16) sequenceParameterSetLength ;
//			bit(8*sequenceParameterSetLength) sequenceParameterSetNALUnit;
//		}
//		unsigned int(8) numOfPictureParameterSets;
//		for (i=0; i< numOfPictureParameterSets;  i++) {
//			unsigned int(16) pictureParameterSetLength;
//			bit(8*pictureParameterSetLength) pictureParameterSetNALUnit;
//		}
//		if( profile_idc  ==  100  ||  profile_idc  ==  110  ||
//		    profile_idc  ==  122  ||  profile_idc  ==  144 )
//		{
//			bit(6) reserved = ‘111111’b;
//			unsigned int(2) chroma_format;
//			bit(5) reserved = ‘11111’b;
//			unsigned int(3) bit_depth_luma_minus8;
//			bit(5) reserved = ‘11111’b;
//			unsigned int(3) bit_depth_chroma_minus8;
//			unsigned int(8) numOfSequenceParameterSetExt;
//			for (i=0; i< numOfSequenceParameterSetExt; i++) {
//				unsigned int(16) sequenceParameterSetExtLength;
//				bit(8*sequenceParameterSetExtLength) sequenceParameterSetExtNALUnit;
//			}
//		}
//	}

#define MIN_AVCDECODERCONFIGURATIONRECORD_SIZE	7

class AVCDecoderConfigurationRecord : public DecoderConfigurationRecord
{
public:
	bool IsValid() const override;
	ov::String GetCodecsParameter() const override;

	// Instance can be initialized by putting raw data in AVCDecoderConfigurationRecord.
	bool Parse(const std::shared_ptr<const ov::Data> &data) override;
	bool Equals(const std::shared_ptr<DecoderConfigurationRecord> &other) override;

	MAY_THROWS(ov::BitReaderError)
	static std::shared_ptr<AVCDecoderConfigurationRecord> ParseV2(ov::BitReader &reader)
	{
		auto record = std::make_shared<AVCDecoderConfigurationRecord>();
		if (record->ParseV2Internal(reader) == false)
		{
			return nullptr;
		}
		return record;
	}
	
	// Instance can be initialized by putting SPS/PPS in AVCDecoderConfigurationRecord.
	bool AddSPS(const std::shared_ptr<const ov::Data> &sps);
	bool AddPPS(const std::shared_ptr<const ov::Data> &pps);
	bool AddSPSExt(const std::shared_ptr<const ov::Data> &sps_ext);

	std::shared_ptr<const ov::Data> Serialize() override;

	uint8_t Version() const;
	uint8_t	ProfileIndication() const;
	uint8_t Compatibility() const;
	uint8_t LevelIndication() const;
	uint8_t LengthMinusOne() const;
	uint8_t NumOfSPS() const;
	uint8_t NumOfPPS() const;
	uint8_t NumOfSPSExt() const;
	std::shared_ptr<const ov::Data>	GetSPSData(int index) const;
	std::shared_ptr<const ov::Data>	GetPPSData(int index) const;
	std::shared_ptr<const ov::Data>	GetSPSExtData(int index) const;

	// Get SPS
	bool GetSPS(int sps_id, H264SPS &sps) const;
	bool GetPPS(int pps_id, H264PPS &pps) const;

	uint8_t ChromaFormat() const;
	uint8_t BitDepthLumaMinus8() const;
	uint8_t BitDepthChromaMinus8() const;

	// Helper functions
	int32_t GetWidth() const;
	int32_t GetHeight() const;

	std::tuple<std::shared_ptr<ov::Data>, FragmentationHeader> GetSpsPpsAsAnnexB(uint8_t start_code_size, bool need_aud=false);
	ov::String GetInfoString() const;

private:
	MAY_THROWS(ov::BitReaderError)
	bool ParseV2Internal(ov::BitReader &reader);
	
	// It only be called by AddSPS and AddPPS
	void SetVersion(uint8_t version);
	void SetProfileIndication(uint8_t profile_indication);
	void SetCompatibility(uint8_t profile_compatibility);
	void SetLevelIndication(uint8_t level_indication);
	void SetLengthMinusOne(uint8_t length_minus_one);
	void SetChromaFormat(uint8_t chroma_format);
	void SetBitDepthLumaMinus8(uint8_t bit_depth_luma_minus8);
	void SetBitDepthChromaMinus8(uint8_t bit_depth_chroma_minus8);

	////////////////////////////////////////
	// Decoder configuration record
	////////////////////////////////////////
	uint8_t		_version = 1;
	uint8_t		_profile_indication = 0;
	uint8_t		_profile_compatibility = 0;
	uint8_t		_level_indication = 0;
	uint8_t		_reserved1 = 0;			// (6 bits) = 111111b
	uint8_t		_length_minus_one = 3;	// (2 bits)	= length of the NALUnitLength 0, 1, 3 corresponding to 1, 2, 4 (Usually 3)
	uint8_t		_reserved2 = 0;			// (3 bits) = 111b
	
	// for(int i=0; i<_num_of_sps; i++)
		// sps_length(16 bits) + sps
	uint8_t		_num_of_sps = 0;		// (5 bits)
	std::vector<std::shared_ptr<const ov::Data>>	_sps_data_list;
	// sps_id, sps
	std::map<uint8_t, H264SPS> _sps_map;

	// for(int i=0; i<_num_of_pps; i++)
		// pps_length(16 bits) + sps
	uint8_t		_num_of_pps = 0;		// (8 bits)
	std::vector<std::shared_ptr<const ov::Data>>	_pps_data_list;
	// pps_id, pps
	std::map<uint8_t, H264PPS> _pps_map;

	// if _profile_indication == 100 or 110 or 122 or 144
	uint8_t		_reserved3 = 0;			// (6 bits) = 111111b
	uint8_t		_chroma_format = 0;		// (2 bits)
	uint8_t		_reserved4 = 0;			// (5 bits) = 11111b
	uint8_t		_bit_depth_luma_minus8 = 0;	// (3 bits)
	uint8_t     _reserved5 = 0; 		// (5 bits) = 11111b
	uint8_t		_bit_depth_chroma_minus8 = 0; // (3 bits)

	// for(int i=0; i<_num_of_sps_ext; i++)
		// sps_ext_length(16 bits) + sps_ext
	uint8_t		_num_of_sps_ext = 0;		// (8 bits)
	std::vector<std::shared_ptr<const ov::Data>>	_sps_ext_data_list;


	//////////////////////////////////////////
	// Helper
	//////////////////////////////////////////
	H264SPS _h264_sps;
	std::shared_ptr<ov::Data> _sps_pps_annexb_data = nullptr;
	FragmentationHeader _sps_pps_annexb_frag_header;
};