//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "ovt_signaling.h"

#include <main/main.h>

namespace ovt
{
	ov::String GetOvtAgent()
	{
		return ov::String::FormatString("OvenMediaEngine/%s%s", OME_VERSION, OME_GIT_VERSION_EXTRA);
	}

	OriginVerdict JudgeOriginResponse(const Json::Value &root, uint32_t max_media_header_version)
	{
		if (HasNonObjectOvt(root))
		{
			return OriginVerdict::NotAnObject;
		}

		if (HasOvtObject(root) == false)
		{
			return OriginVerdict::Ovt1;
		}

		auto media_header_version = GetMediaHeaderVersion(root);
		if (media_header_version.has_value() == false)
		{
			return OriginVerdict::NoMediaHeaderVersion;
		}

		// Anything at or below is read: a build knows every layout up to its own.
		// `ovt.version`, the origin's generation, is not consulted here and never refuses a peer.
		return (*media_header_version > max_media_header_version) ? OriginVerdict::MediaHeaderTooNew
																  : OriginVerdict::Ovt2;
	}

	Json::Value MakeRequestOvtObject()
	{
		Json::Value object;
		object["agent"]	  = GetOvtAgent().CStr();
		object["version"] = OVT_PROTOCOL_VERSION;
		return object;
	}

	ov::String SanitizeForLog(const ov::String &text)
	{
		auto length = std::min(text.GetLength(), MAX_LOG_STRING_LENGTH);
		std::string sanitized(text.CStr(), length);

		for (auto &character : sanitized)
		{
			if ((static_cast<unsigned char>(character) < 0x20) || (character == 0x7F))
			{
				character = '.';
			}
		}

		return sanitized.c_str();
	}

	ov::String TruncateForResponse(const ov::String &text)
	{
		if (text.GetLength() <= MAX_RESPONSE_FIELD_LENGTH)
		{
			return text;
		}

		return text.Substring(0, MAX_RESPONSE_FIELD_LENGTH) + "...";
	}

	Json::Value MakeResponseOvtObject()
	{
		Json::Value object;
		object["agent"]				 = GetOvtAgent().CStr();
		object["version"]			 = OVT_PROTOCOL_VERSION;
		object["mediaHeaderVersion"] = OVT_MEDIA_HEADER_VERSION;
		return object;
	}

	ov::String CodecToken(const ov::String &codec_name)
	{
		return ov::String::FormatString("codec/%s", codec_name.CStr());
	}

	ov::String CodecToken(cmn::MediaCodecId codec_id)
	{
		return CodecToken(ov::String(cmn::GetCodecIdString(codec_id)));
	}

	ov::String BitstreamToken(cmn::BitstreamFormat format)
	{
		return ov::String::FormatString("bitstream/%s", cmn::GetBitstreamFormatString(format));
	}

	ov::String PacketTypeToken(cmn::PacketType packet_type)
	{
		return ov::String::FormatString("packettype/%s", cmn::GetPacketTypeString(packet_type));
	}

	const std::set<ov::String> &SupportedRequiredTokens()
	{
		static const std::set<ov::String> tokens = [] {
			std::set<ov::String> set;

			for (auto codec_id : cmn::ALL_MEDIA_CODEC_IDS)
			{
				set.insert(CodecToken(codec_id));
			}

			for (auto format : cmn::ALL_BITSTREAM_FORMATS)
			{
				set.insert(BitstreamToken(format));
			}

			for (auto packet_type : cmn::ALL_PACKET_TYPES)
			{
				set.insert(PacketTypeToken(packet_type));
			}

			for (const char *feature : {"feature/track-notify", "feature/stop-reason", "feature/track-id-rendition", "feature/playlist-full", "feature/codecs-string"})
			{
				set.insert(feature);
			}

			return set;
		}();

		return tokens;
	}

	ov::String MakeRequiredPayload(const std::vector<ov::String> &tokens)
	{
		Json::Value root;
		root["required"] = Json::Value(Json::arrayValue);
		for (const auto &token : tokens)
		{
			root["required"].append(token.CStr());
		}

		return ov::Json::Stringify(root);
	}

	std::optional<std::set<uint32_t>> ParseTrackIdArray(const Json::Value &value, size_t *ignored)
	{
		if (ignored != nullptr)
		{
			*ignored = 0;
		}

		if (value.isArray() == false)
		{
			return std::nullopt;
		}

		std::set<uint32_t> track_ids;
		for (const auto &track_id : value)
		{
			if (track_id.isUInt() == false)
			{
				if (ignored != nullptr)
				{
					(*ignored)++;
				}
				continue;
			}

			track_ids.insert(track_id.asUInt());
		}

		return track_ids;
	}

	std::optional<std::vector<ov::String>> ParseRequiredTokens(const Json::Value &required)
	{
		if (required.isArray() == false)
		{
			return std::nullopt;
		}

		std::vector<ov::String> tokens;
		for (const auto &token : required)
		{
			if (token.isString() == false)
			{
				return std::nullopt;
			}

			tokens.push_back(token.asCString());
		}

		return tokens;
	}
}  // namespace ovt
