//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace oprf
			{
				struct BypassIfMatch : public Item
				{
				protected:
					ov::String _codec	   = "";
					ov::String _bitrate	   = "";
					ov::String _samplerate = "";
					ov::String _framerate  = "";
					ov::String _channel	   = "";
					ov::String _width	   = "";
					ov::String _height	   = "";
					ov::String _sar		   = "";

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(GetCodec, _codec)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetBitrate, _bitrate)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetFramerate, _framerate)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetSamplerate, _samplerate)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetChannel, _channel)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetWidth, _width)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetHeight, _height)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetSAR, _sar)

				protected:
					void MakeList() override
					{
						// Common
						Register<Optional>("Codec", &_codec, nullptr, [=]() -> std::shared_ptr<ConfigError> {
							auto val = _codec.UpperCaseString();
							return (val == "EQ") ? nullptr : CreateConfigErrorPtr("BypassIfMatch.Codec must be specified only eq");
						});
						Register<Optional>("Bitrate", &_bitrate, nullptr, [=]() -> std::shared_ptr<ConfigError> {
							auto val = _bitrate.UpperCaseString();
							// return (val == "EQ" ||  val == "GTE" || val == "LTE") ? nullptr : CreateConfigErrorPtr("BypassIfMatch.Bitrate must specified only one of eq, lte, gte");
							return CreateConfigErrorPtr("BypassIfMatch.Bitrate is not yet supported");
						});
						// Video
						Register<Optional>("Framerate", &_framerate, nullptr, [=]() -> std::shared_ptr<ConfigError> {
							auto val = _framerate.UpperCaseString();
							return (val == "EQ" || val == "GTE" || val == "LTE") ? nullptr : CreateConfigErrorPtr("BypassIfMatch.Framerate must specified only one of eq, lte, gte");
						});
						Register<Optional>("Width", &_width, nullptr, [=]() -> std::shared_ptr<ConfigError> {
							auto val = _width.UpperCaseString();
							return (val == "EQ" || val == "GTE" || val == "LTE") ? nullptr : CreateConfigErrorPtr("BypassIfMatch.Width must specified only one of eq, lte, gte");
						});
						Register<Optional>("Height", &_height, nullptr, [=]() -> std::shared_ptr<ConfigError> {
							auto val = _height.UpperCaseString();
							return (val == "EQ" || val == "GTE" || val == "LTE") ? nullptr : CreateConfigErrorPtr("BypassIfMatch.Height must specified only one of eq, lte, gte");
						});
						Register<Optional>("SAR", &_sar, nullptr, [=]() -> std::shared_ptr<ConfigError> {
							auto val = _sar.UpperCaseString();
							return (val == "EQ") ? nullptr : CreateConfigErrorPtr("BypassIfMatch.SAR must specified only one of eq");
						});
						// Audio
						Register<Optional>("Samplerate", &_samplerate, nullptr, [=]() -> std::shared_ptr<ConfigError> {
							auto val = _samplerate.UpperCaseString();
							return (val == "EQ" || val == "GTE" || val == "LTE") ? nullptr : CreateConfigErrorPtr("BypassIfMatch.Samplerate must specified only one of eq, lte, gte");
						});
						Register<Optional>("Channel", &_channel, nullptr, [=]() -> std::shared_ptr<ConfigError> {
							auto val = _channel.UpperCaseString();
							return (val == "EQ" || val == "GTE" || val == "LTE") ? nullptr : CreateConfigErrorPtr("BypassIfMatch.Channel must specified only one of eq, lte, gte");
						});
					}
				};
			}  // namespace oprf
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg