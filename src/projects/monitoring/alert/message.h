//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Gilhoon Choi
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

namespace mon::alrt
{
	class Message
	{
	public:
		constexpr static const uint32_t INGRESS_CODE_MASK		   = 0x110000;
		constexpr static const uint32_t INGRESS_CODE_STATUS_MASK   = INGRESS_CODE_MASK | 0x001000;
		constexpr static const uint32_t INGRESS_CODE_METRIC_MASK   = INGRESS_CODE_MASK | 0x002000;
		constexpr static const uint32_t EGRESS_CODE_MASK		   = 0x120000;
		constexpr static const uint32_t EGRESS_CODE_STATUS_MASK	   = EGRESS_CODE_MASK | 0x001000;
		constexpr static const uint32_t EGRESS_CODE_READY_MASK	   = EGRESS_CODE_MASK | 0x002000;
		constexpr static const uint32_t EGRESS_CODE_TRANSCODE_MASK = EGRESS_CODE_MASK | 0x004000;
		constexpr static const uint32_t INTERNAL_QUEUE_CODE_MASK   = 0x140000;

		enum class Code : uint32_t
		{
			OK,

			// Ingress Codes
			INGRESS_STREAM_CREATED = INGRESS_CODE_STATUS_MASK,
			INGRESS_STREAM_PREPARED,
			INGRESS_STREAM_DELETED,
			INGRESS_STREAM_CREATION_FAILED_DUPLICATE_NAME,

			INGRESS_BITRATE_LOW = INGRESS_CODE_METRIC_MASK,
			INGRESS_BITRATE_HIGH,
			INGRESS_FRAMERATE_LOW,
			INGRESS_FRAMERATE_HIGH,
			INGRESS_WIDTH_SMALL,
			INGRESS_WIDTH_LARGE,
			INGRESS_HEIGHT_SMALL,
			INGRESS_HEIGHT_LARGE,
			INGRESS_SAMPLERATE_LOW,
			INGRESS_SAMPLERATE_HIGH,
			INGRESS_LONG_KEY_FRAME_INTERVAL,
			INGRESS_HAS_BFRAME,

			// Egress Codes
			EGRESS_STREAM_CREATED = EGRESS_CODE_STATUS_MASK,
			EGRESS_STREAM_PREPARED,
			EGRESS_STREAM_DELETED,

			EGRESS_STREAM_CREATION_FAILED_OUTPUT_PROFILE,
			EGRESS_STREAM_CREATION_FAILED_DECODER,
			EGRESS_STREAM_CREATION_FAILED_ENCODER,
			EGRESS_STREAM_CREATION_FAILED_FILTER,

			EGRESS_TRANSCODE_FAILED_DECODING = EGRESS_CODE_TRANSCODE_MASK,
			EGRESS_TRANSCODE_FAILED_ENCODING,
			EGRESS_TRANSCODE_FAILED_FILTERING,

			EGRESS_LLHLS_READY = EGRESS_CODE_READY_MASK,
			EGRESS_HLS_READY,

			// Internal Codes
			INTERNAL_QUEUE_CONGESTION = INTERNAL_QUEUE_CODE_MASK
		};

		static std::shared_ptr<Message> CreateMessage(Code code, const ov::String &description)
		{
			auto message		  = std::make_shared<Message>();

			message->_code		  = code;
			message->_description = description;

			return message;
		}

		Code GetCode() const
		{
			return _code;
		}

		const ov::String &GetDescription() const
		{
			return _description;
		}

		constexpr static const char *StringFromMessageCode(Code message_code)
		{
			switch (message_code)
			{
				OV_CASE_RETURN_ENUM_STRING(Code, OK);

				OV_CASE_RETURN_ENUM_STRING(Code, INGRESS_STREAM_CREATED);
				OV_CASE_RETURN_ENUM_STRING(Code, INGRESS_STREAM_PREPARED);
				OV_CASE_RETURN_ENUM_STRING(Code, INGRESS_STREAM_DELETED);
				OV_CASE_RETURN_ENUM_STRING(Code, INGRESS_STREAM_CREATION_FAILED_DUPLICATE_NAME);

				OV_CASE_RETURN_ENUM_STRING(Code, INGRESS_BITRATE_LOW);
				OV_CASE_RETURN_ENUM_STRING(Code, INGRESS_BITRATE_HIGH);
				OV_CASE_RETURN_ENUM_STRING(Code, INGRESS_FRAMERATE_LOW);
				OV_CASE_RETURN_ENUM_STRING(Code, INGRESS_FRAMERATE_HIGH);
				OV_CASE_RETURN_ENUM_STRING(Code, INGRESS_WIDTH_SMALL);
				OV_CASE_RETURN_ENUM_STRING(Code, INGRESS_WIDTH_LARGE);
				OV_CASE_RETURN_ENUM_STRING(Code, INGRESS_HEIGHT_SMALL);
				OV_CASE_RETURN_ENUM_STRING(Code, INGRESS_HEIGHT_LARGE);
				OV_CASE_RETURN_ENUM_STRING(Code, INGRESS_SAMPLERATE_LOW);
				OV_CASE_RETURN_ENUM_STRING(Code, INGRESS_SAMPLERATE_HIGH);
				OV_CASE_RETURN_ENUM_STRING(Code, INGRESS_LONG_KEY_FRAME_INTERVAL);
				OV_CASE_RETURN_ENUM_STRING(Code, INGRESS_HAS_BFRAME);

				OV_CASE_RETURN_ENUM_STRING(Code, EGRESS_STREAM_CREATED);
				OV_CASE_RETURN_ENUM_STRING(Code, EGRESS_STREAM_PREPARED);
				OV_CASE_RETURN_ENUM_STRING(Code, EGRESS_STREAM_DELETED);
				OV_CASE_RETURN_ENUM_STRING(Code, EGRESS_STREAM_CREATION_FAILED_OUTPUT_PROFILE);
				OV_CASE_RETURN_ENUM_STRING(Code, EGRESS_STREAM_CREATION_FAILED_DECODER);
				OV_CASE_RETURN_ENUM_STRING(Code, EGRESS_STREAM_CREATION_FAILED_ENCODER);
				OV_CASE_RETURN_ENUM_STRING(Code, EGRESS_STREAM_CREATION_FAILED_FILTER);
				OV_CASE_RETURN_ENUM_STRING(Code, EGRESS_TRANSCODE_FAILED_DECODING);
				OV_CASE_RETURN_ENUM_STRING(Code, EGRESS_TRANSCODE_FAILED_ENCODING);
				OV_CASE_RETURN_ENUM_STRING(Code, EGRESS_TRANSCODE_FAILED_FILTERING);

				OV_CASE_RETURN_ENUM_STRING(Code, EGRESS_LLHLS_READY);
				OV_CASE_RETURN_ENUM_STRING(Code, EGRESS_HLS_READY);

				OV_CASE_RETURN_ENUM_STRING(Code, INTERNAL_QUEUE_CONGESTION);

			}

			OV_ASSERT2(false);
			return "UNKNOWN";
		}

		static ov::String DescriptionFromMessageCode(Code message_code)
		{
			return DescriptionFromMessageCode(message_code, 0, 0);
		}

#define RETURN_DESCRIPTION(code, format, ...) \
	case code:                                \
		return ov::String::FormatString(format, ##__VA_ARGS__)

		template <typename T>
		static ov::String DescriptionFromMessageCode(Code message_code, T config_value, T measured_value)
		{
			auto config	  = static_cast<T>(config_value);
			auto measured = static_cast<T>(measured_value);

			switch (message_code)
			{
				RETURN_DESCRIPTION(Code::OK, "The current status is good");

				// Ingress Codes
				RETURN_DESCRIPTION(Code::INGRESS_STREAM_CREATED, "A new ingress stream has been created");
				RETURN_DESCRIPTION(Code::INGRESS_STREAM_PREPARED, "A ingress stream has been prepared");
				RETURN_DESCRIPTION(Code::INGRESS_STREAM_DELETED, "A ingress stream has been deleted");
				RETURN_DESCRIPTION(Code::INGRESS_STREAM_CREATION_FAILED_DUPLICATE_NAME, "Failed to create stream because the specified stream name is already in use");

				RETURN_DESCRIPTION(Code::INGRESS_BITRATE_LOW, "The ingress stream's current bitrate (%d bps) is lower than the configured bitrate (%d bps)", measured, config);
				RETURN_DESCRIPTION(Code::INGRESS_BITRATE_HIGH, "The ingress stream's current bitrate (%d bps) is higher than the configured bitrate (%d bps)", measured, config);
				RETURN_DESCRIPTION(Code::INGRESS_FRAMERATE_LOW, "The ingress stream's current framerate (%.2f fps) is lower than the configured framerate (%.2f fps)", static_cast<double>(measured), static_cast<double>(config));
				RETURN_DESCRIPTION(Code::INGRESS_FRAMERATE_HIGH, "The ingress stream's current framerate (%f fps) is higher than the configured framerate (%f fps)", static_cast<double>(measured), static_cast<double>(config));
				RETURN_DESCRIPTION(Code::INGRESS_WIDTH_SMALL, "The ingress stream's width (%d) is smaller than the configured width (%d)", measured, config);
				RETURN_DESCRIPTION(Code::INGRESS_WIDTH_LARGE, "The ingress stream's width (%d) is larger than the configured width (%d)", measured, config);
				RETURN_DESCRIPTION(Code::INGRESS_HEIGHT_SMALL, "The ingress stream's height (%d) is smaller than the configured height (%d)", measured, config);
				RETURN_DESCRIPTION(Code::INGRESS_HEIGHT_LARGE, "The ingress stream's height (%d) is larger than the configured height (%d)", measured, config);
				RETURN_DESCRIPTION(Code::INGRESS_SAMPLERATE_LOW, "The ingress stream's current samplerate (%d) is lower than the configured samplerate (%d)", measured, config);
				RETURN_DESCRIPTION(Code::INGRESS_SAMPLERATE_HIGH, "The ingress stream's current samplerate (%d) is higher than the configured samplerate (%d)", measured, config);
				RETURN_DESCRIPTION(Code::INGRESS_LONG_KEY_FRAME_INTERVAL, "The ingress stream's current keyframe interval (%.1f seconds) is too long. Please use a keyframe interval of %.1f seconds or less", static_cast<double>(measured), static_cast<double>(config));
				RETURN_DESCRIPTION(Code::INGRESS_HAS_BFRAME, "There are B-Frames in the ingress stream");

				// Egress Codes
				RETURN_DESCRIPTION(Code::EGRESS_STREAM_CREATED, "A new egress stream has been created");
				RETURN_DESCRIPTION(Code::EGRESS_STREAM_PREPARED, "A egress stream has been prepared");
				RETURN_DESCRIPTION(Code::EGRESS_STREAM_DELETED, "A egress stream has been deleted");

				RETURN_DESCRIPTION(Code::EGRESS_STREAM_CREATION_FAILED_OUTPUT_PROFILE, "Failed to create stream because the output profile configuration is invalid");
				RETURN_DESCRIPTION(Code::EGRESS_STREAM_CREATION_FAILED_DECODER, "Failed to create stream because the decoder could not be created");
				RETURN_DESCRIPTION(Code::EGRESS_STREAM_CREATION_FAILED_ENCODER, "Failed to create stream because the encoder could not be created");
				RETURN_DESCRIPTION(Code::EGRESS_STREAM_CREATION_FAILED_FILTER, "Failed to create stream because the filter could not be created");

				RETURN_DESCRIPTION(Code::EGRESS_TRANSCODE_FAILED_DECODING, "Failed to decode frames");
				RETURN_DESCRIPTION(Code::EGRESS_TRANSCODE_FAILED_ENCODING, "Failed to encode frames");
				RETURN_DESCRIPTION(Code::EGRESS_TRANSCODE_FAILED_FILTERING, "Failed to filter frames");

				RETURN_DESCRIPTION(Code::EGRESS_LLHLS_READY, "LLHLS stream is ready to play - initial segment(s) have been generated");
				RETURN_DESCRIPTION(Code::EGRESS_HLS_READY, "HLS stream is ready to play - initial segment(s) have been generated");

				// Internal Codes
				RETURN_DESCRIPTION(Code::INTERNAL_QUEUE_CONGESTION, "Internal queue(s) is currently congested");
			}

			OV_ASSERT2(false);
			return "Unknown message code";
		}

	private:
		Code _code = Code::OK;
		ov::String _description;
	};
}  // namespace mon::alrt
