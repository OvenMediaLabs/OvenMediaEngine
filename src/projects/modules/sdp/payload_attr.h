//==============================================================================
//
//  OvenMediaEngine
//
//  Created by getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include "sdp_base.h"

class PayloadAttr
{
public:
	PayloadAttr();
	virtual ~PayloadAttr();

	enum class SupportCodec
	{
		Unknown,
		VP8,
		VP9,
		H264,
		H265,
		MPEG4_GENERIC,
		OPUS,
		RED,
		RTX
	};

	enum class Mpeg4GenericMode
	{
		Generic,
		CELP_cbr,
		CELP_vbr,
		AAC_lbr,
		AAC_hbr,
		MPS_lbr,
		MPS_hbr
	};

	enum class RtcpFbType
	{
		GoogRemb,
		TransportCc,
		CcmFir,
		Nack,
		NackPli,

		NumberOfRtcpFbType
	};

	void SetId(uint8_t id);
	uint8_t GetId() const;

	void SetRtpmap(const uint8_t payload_type, const ov::String &codec, uint32_t rate, const ov::String &parameters = "");
	SupportCodec GetCodec() const;
	ov::String GetCodecStr() const;
	uint32_t GetCodecRate() const;
	ov::String GetCodecParams() const;

	// a=rtcp-fb:96 nack pli
	bool EnableRtcpFb(const ov::String &type, bool on);
	void EnableRtcpFb(const RtcpFbType &type, bool on);
	bool IsRtcpFbEnabled(const RtcpFbType &type) const;

	// a=fmtp:111 maxplaybackrate=16000; useinbandfec=1; maxaveragebitrate=20000
	void SetFmtp(const ov::String &fmtp);
	ov::String GetFmtp() const;

	// H264 Specific
	std::shared_ptr<ov::Data> GetH264ExtraDataAsAnnexB() const
	{
		if(GetH264SPS() == nullptr || GetH264PPS() == nullptr)
		{
			return nullptr;
		}

		auto data = std::make_shared<ov::Data>();
		ov::ByteStream stream(data);

		stream.WriteBE((uint8_t)0);
		stream.WriteBE((uint8_t)0);
		stream.WriteBE((uint8_t)0);
		stream.WriteBE((uint8_t)1);
		stream.Write(GetH264SPS());
		stream.WriteBE((uint8_t)0);
		stream.WriteBE((uint8_t)0);
		stream.WriteBE((uint8_t)0);
		stream.WriteBE((uint8_t)1);
		stream.Write(GetH264PPS());

		return data;
	}

	std::shared_ptr<ov::Data> GetH264SPS() const {return _h264_sps_bytes;}
	std::shared_ptr<ov::Data> GetH264PPS() const {return _h264_pps_bytes;}

	// MPEG4-GENERIC AUDIO Specific
	Mpeg4GenericMode GetMpeg4GenericMode() const {return _mpeg4_generic_mode;}
	uint32_t GetMpeg4GenericSizeLength() const {return _mpeg4_generic_size_length;}
	uint32_t GetMpeg4GenericIndexLength() const {return _mpeg4_generic_index_length;}
	uint32_t GetMpeg4GenericIndexDeltaLength() const {return _mpeg4_generic_index_delta_length;}
	std::shared_ptr<ov::Data> GetMpeg4GenericConfig() const {return _mpeg4_generic_config;}

	// H265 Specific
	std::shared_ptr<ov::Data> GetH265ExtraDataAsAnnexB() const
	{
		if(GetH265VPS() == nullptr || GetH265SPS() == nullptr || GetH265PPS() == nullptr)
		{
			return nullptr;
		}

		auto data = std::make_shared<ov::Data>();
		ov::ByteStream stream(data);

		stream.WriteBE((uint8_t)0);
		stream.WriteBE((uint8_t)0);
		stream.WriteBE((uint8_t)0);
		stream.WriteBE((uint8_t)1);
		stream.Write(GetH265VPS());
		stream.WriteBE((uint8_t)0);
		stream.WriteBE((uint8_t)0);
		stream.WriteBE((uint8_t)0);
		stream.WriteBE((uint8_t)1);
		stream.Write(GetH265SPS());
		stream.WriteBE((uint8_t)0);
		stream.WriteBE((uint8_t)0);
		stream.WriteBE((uint8_t)0);
		stream.WriteBE((uint8_t)1);
		stream.Write(GetH265PPS());

		return data;
	}

	std::shared_ptr<ov::Data> GetH265VPS() const {return _h265_vps_bytes;}
	std::shared_ptr<ov::Data> GetH265SPS() const {return _h265_sps_bytes;}
	std::shared_ptr<ov::Data> GetH265PPS() const {return _h265_pps_bytes;}
	uint32_t GetH265MaxDONDiff() const {return _h265_max_don_diff;}

private:
	uint8_t _id = 0;
	SupportCodec _codec = SupportCodec::Unknown;
	ov::String _codec_str = "";
	uint32_t _rate = 0;
	ov::String _codec_param = "";

	// MPEG4-GENERIC AUDIO Specific
	Mpeg4GenericMode _mpeg4_generic_mode = Mpeg4GenericMode::Generic;
	uint32_t _mpeg4_generic_size_length = 0;
	uint32_t _mpeg4_generic_index_length = 0;
	uint32_t _mpeg4_generic_index_delta_length = 0;
	std::shared_ptr<ov::Data> _mpeg4_generic_config = nullptr;

	bool _rtcpfb_support_flag[(int)(RtcpFbType::NumberOfRtcpFbType)];

	ov::String _fmtp = "";

	// H264 Specific
	std::shared_ptr<ov::Data>	_h264_sps_bytes = nullptr;
	std::shared_ptr<ov::Data>	_h264_pps_bytes = nullptr;

	// H265 Specific
	std::shared_ptr<ov::Data>	_h265_vps_bytes = nullptr;
	std::shared_ptr<ov::Data>	_h265_sps_bytes = nullptr;
	std::shared_ptr<ov::Data>	_h265_pps_bytes = nullptr;
	uint32_t _h265_max_don_diff = 0;
};
