//==============================================================================
//
//  OvenMediaEngine
//
//  Created by getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#include <base/ovcrypto/base_64.h>
#include "payload_attr.h"

PayloadAttr::PayloadAttr()
{
	_id = 0;

	for(bool &flag : _rtcpfb_support_flag)
	{
		flag = false;
	}
}

PayloadAttr::~PayloadAttr()
{
}

void PayloadAttr::SetId(uint8_t id)
{
	_id = id;
}

uint8_t PayloadAttr::GetId() const
{
	return _id;
}

void PayloadAttr::SetRtpmap(const uint8_t payload_type, const ov::String &codec, uint32_t rate, const ov::String &parameters)
{
	_id = payload_type;
	_codec_str = codec;
	_rate = rate;
	_codec_param = parameters;

	if(codec.LowerCaseString() == "h264")
	{
		_codec = SupportCodec::H264;
	}
	else if(codec.LowerCaseString() == "h265")
	{
		_codec = SupportCodec::H265;
	}
	else if(codec.LowerCaseString() == "vp8")
	{
		_codec = SupportCodec::VP8;
	}
	else if(codec.LowerCaseString() == "vp9")
	{
		_codec = SupportCodec::VP9;
	}
	else if(codec.LowerCaseString() == "opus")
	{
		_codec = SupportCodec::OPUS;
	}
	else if(codec.LowerCaseString() == "mpeg4-generic")
	{
		_codec = SupportCodec::MPEG4_GENERIC;
	}
	else if(codec.LowerCaseString() == "red")
	{
		_codec = SupportCodec::RED;
	}
	else if(codec.LowerCaseString() == "rtx")
	{
		_codec = SupportCodec::RTX;
	}
	else
	{
		_codec = SupportCodec::Unknown;
	}
}

// a=rtcp-fb:96 nack pli
void PayloadAttr::EnableRtcpFb(const RtcpFbType &type, const bool on)
{
	_rtcpfb_support_flag[(int)type] = on;
}

PayloadAttr::SupportCodec PayloadAttr::GetCodec() const
{
	return _codec;
}

ov::String PayloadAttr::GetCodecStr() const
{
	return _codec_str;
}

uint32_t PayloadAttr::GetCodecRate() const
{
	return _rate;
}

ov::String PayloadAttr::GetCodecParams() const
{
	return _codec_param;
}

bool PayloadAttr::EnableRtcpFb(const ov::String &type, const bool on)
{
	ov::String type_name = type.UpperCaseString().Trim();
	
	if(type_name == "GOOG-REMB")
	{
		_rtcpfb_support_flag[(int)(RtcpFbType::GoogRemb)] = on;
	}
	else if(type_name == "TRANSPORT-CC")
	{
		_rtcpfb_support_flag[(int)(RtcpFbType::TransportCc)] = on;
	}
	else if(type_name == "CCM FIR")
	{
		_rtcpfb_support_flag[(int)(RtcpFbType::CcmFir)] = on;
	}
	else if(type_name == "NACK")
	{
		_rtcpfb_support_flag[(int)(RtcpFbType::Nack)] = on;
	}
	else if(type_name == "NACK PLI")
	{
		_rtcpfb_support_flag[(int)(RtcpFbType::NackPli)] = on;
	}
	else
	{
		return false;
	}

	return true;
}

bool PayloadAttr::IsRtcpFbEnabled(const PayloadAttr::RtcpFbType &type) const
{
	return _rtcpfb_support_flag[(int)type];
}

void PayloadAttr::SetFmtp(const ov::String &fmtp)
{
	if(_codec == SupportCodec::H264)
	{
		// a=fmtp:96 packetization-mode=1;profile-level-id=42c01f;prop-parameter-sets=Z0LAH9kAUAW7AWoCAgKAAAADAIAAAB5HjBkk,aMuMsg==
		auto components = fmtp.Split(";");
		for(const auto &component : components)
		{
			auto index = component.IndexOf('=');
			if(index == -1)
			{
				continue;
			}

			auto name = component.Substring(0, index).Trim();
			auto value = component.Substring(index+1).Trim();

			if(name.LowerCaseString() == "sprop-parameter-sets")
			{
				auto sets = value.Split(",");
				if(sets.size() != 2)
				{
					continue;
				}

				_h264_sps_bytes = ov::Base64::Decode(sets[0]);
				_h264_pps_bytes = ov::Base64::Decode(sets[1]);
			}
		}
	}
	else if(_codec == SupportCodec::H265)
	{
		// a=fmtp:96 packetization-mode=0;profile-space=0;profile-id=1;tier-flag=0;level-id=123;interop-constraints=000000000000;sprop-vps=QAEMAf//AWAAAAMAAAMAAAMAAAMAe6wJ;sprop-sps=QgEBAWAAAAMAAAMAAAMAAAMAe6ADwIAQ5Y2uSTBrlnAIAAAfSAAHVOBA;sprop-pps=RAHgdrAmQA==;config=0000...6b02640
		auto components = fmtp.Split(";");
		for(const auto &component : components)
		{
			auto index = component.IndexOf('=');
			if(index == -1)
			{
				continue;
			}

			auto name = component.Substring(0, index).Trim();
			auto value = component.Substring(index+1).Trim();

			if(name.LowerCaseString() == "sprop-vps")
			{
				_h265_vps_bytes = ov::Base64::Decode(value);
			}
			else if(name.LowerCaseString() == "sprop-sps")
			{
				_h265_sps_bytes = ov::Base64::Decode(value);
			}
			else if(name.LowerCaseString() == "sprop-pps")
			{
				_h265_pps_bytes = ov::Base64::Decode(value);
			}
			else if(name.LowerCaseString() == "sprop-max-don-diff")
			{
				_h265_max_don_diff = ov::Converter::ToInt32(value.CStr());
			}
		}
	}
	else if(_codec == SupportCodec::MPEG4_GENERIC)
	{
		// https://tools.ietf.org/html/rfc3640#section-3.3
		// In the case of mpeg4-generic, there are several modes, which are classified by fmtp.
		// fmtp:96 profile-level-id=1;mode=AAC-hbr;sizelength=13;indexlength=3;indexdeltalength=3;config=1190

		auto params = fmtp.Split(";");
		for(const auto &param : params)
		{
			auto key_value = param.Split("=");
			if(key_value.size() != 2)
			{
				// fmtp parsing error
				logt("SDP", "fmtp parsing error : %s", fmtp.CStr());
				continue;
			}

			auto key = key_value[0].Trim();
			auto value = key_value[1].Trim();

			if(key.UpperCaseString() == "SIZELENGTH")
			{
				_mpeg4_generic_size_length = ov::Converter::ToInt32(value.CStr());
			}
			else if(key.UpperCaseString() == "INDEXLENGTH")
			{
				_mpeg4_generic_index_length = ov::Converter::ToInt32(value.CStr());
			}
			else if(key.UpperCaseString() == "INDEXDELTALENGTH")
			{
				_mpeg4_generic_index_delta_length = ov::Converter::ToInt32(value.CStr());
			}
			else if(key.UpperCaseString() == "CONFIG")
			{
				// (RFC3640) A hexadecimal representation of an octet string that expresses the
				// media payload configuration.  Configuration data is mapped onto
				// the hexadecimal octet string in an MSB-first basis.  The first bit
				// of the configuration data SHALL be located at the MSB of the first
				// octet.  In the last octet, if necessary to achieve octet-
				// alignment, up to 7 zero-valued padding bits shall follow the
				// configuration data.
			
				size_t config_len = (value.GetLength()+1)/2;
				uint8_t config[config_len];

				for(size_t i=0; i<config_len; i++)
				{
					for(size_t j=0; j<2; j++)
					{
						char c = value.Get((i*2)+j); // 0, 1, 2, 3
						char converted;

						if(c >= '0' && c <= '9') 
						{
							converted = c - '0';
						} 
						else if(c >= 'A' && c <= 'F') 
						{
							converted = 10 + c - 'A';
						} 
						else if(c >= 'a' && c <= 'f') 
						{
							converted = 10 + c - 'a';
						} 
						else 
						{
							return;
						}

						if(j == 0)
						{
							config[i] = converted << 4;
						}
						else if(j == 1)
						{
							config[i] |= converted;
						}
					}
				}
			
				_mpeg4_generic_config = std::make_shared<ov::Data>(&config[0], config_len);
			}
			else if(key.UpperCaseString() == "MODE")
			{
				if(value.UpperCaseString() == "GENERIC")
				{
					_mpeg4_generic_mode = Mpeg4GenericMode::Generic;
				}
				else if(value.UpperCaseString() == "CELP-CBR")
				{
					_mpeg4_generic_mode = Mpeg4GenericMode::CELP_cbr;
				}
				else if(value.UpperCaseString() == "CELP-VBR")
				{
					_mpeg4_generic_mode = Mpeg4GenericMode::CELP_vbr;
				}
				else if(value.UpperCaseString() == "AAC-LBR")
				{
					_mpeg4_generic_mode = Mpeg4GenericMode::AAC_lbr;
				}
				else if(value.UpperCaseString() == "AAC-HBR")
				{
					_mpeg4_generic_mode = Mpeg4GenericMode::AAC_hbr;
				}
				else if(value.UpperCaseString() == "MPS-LBR")
				{
					_mpeg4_generic_mode = Mpeg4GenericMode::MPS_lbr;
				}
				else if(value.UpperCaseString() == "MPS-HBR")
				{
					_mpeg4_generic_mode = Mpeg4GenericMode::MPS_hbr;
				}
			}
		}
	}

	_fmtp = fmtp;
}

ov::String PayloadAttr::GetFmtp() const
{
	return _fmtp;
}
