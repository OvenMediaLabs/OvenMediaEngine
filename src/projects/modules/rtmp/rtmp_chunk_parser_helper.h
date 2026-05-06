//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================

// Intentionally included from RTMP chunk parser .cpp files only.
namespace
{
	std::optional<uint32_t> ParseTimestampField(
		const uint32_t stream_id,
		ov::ByteStream &stream,
		bool &is_extended_timestamp,
		uint32_t &extended_timestamp,
		uint32_t &message_header_length,
		const uint32_t encoded_value,
		const uint32_t extended_timestamp_indicator,
		const size_t extended_timestamp_size)
	{
		if (encoded_value != extended_timestamp_indicator)
		{
			is_extended_timestamp = false;
			extended_timestamp	  = 0U;
			return encoded_value;
		}

		if (stream.IsRemained(extended_timestamp_size) == false)
		{
			logtp("Need more data to parse extended timestamp field: %zu bytes (current: %zu)", extended_timestamp_size, stream.Remained());
			return std::nullopt;
		}

		logtd("Extended timestamp is present for stream id: %u", stream_id);

		extended_timestamp	  = stream.ReadBE32();
		is_extended_timestamp = true;
		message_header_length += static_cast<uint32_t>(extended_timestamp_size);

		return extended_timestamp;
	}

	std::optional<int64_t> ResolveTimestampDelta(
		const uint32_t stream_id,
		const int64_t preceding_timestamp,
		const uint32_t timestamp_delta,
		const bool is_extended_timestamp,
		const uint32_t signed_extended_timestamp_delta_sign_bit,
		const int64_t max_negative_extended_timestamp_delta_ms)
	{
		if (is_extended_timestamp && (timestamp_delta >= signed_extended_timestamp_delta_sign_bit))
		{
			const auto signed_timestamp_delta = static_cast<int32_t>(timestamp_delta);
			const auto backward_delta		  = -1 * static_cast<int64_t>(signed_timestamp_delta);

			if ((signed_timestamp_delta < 0) && (backward_delta <= max_negative_extended_timestamp_delta_ms))
			{
				const auto resolved_timestamp = preceding_timestamp + signed_timestamp_delta;
				if (resolved_timestamp < 0)
				{
					logte("Reject signed ext delta: sid=%u prev=%" PRId64 " raw=0x%08x signed=%" PRId32,
						  stream_id,
						  preceding_timestamp,
						  timestamp_delta,
						  signed_timestamp_delta);
					return std::nullopt;
				}

				logtw("Accept signed ext delta: sid=%u prev=%" PRId64 " raw=0x%08x signed=%" PRId32,
					  stream_id,
					  preceding_timestamp,
					  timestamp_delta,
					  signed_timestamp_delta);
				return resolved_timestamp;
			}
		}

		return preceding_timestamp + timestamp_delta;
	}

	int64_t CalculateRolledTimestamp(
		[[maybe_unused]] const uint32_t stream_id,
		const int64_t last_timestamp,
		int64_t parsed_timestamp)
	{
		constexpr int64_t SERIAL_BITS					  = 32;
		constexpr int64_t SERIAL_MODULO					  = (1LL << SERIAL_BITS);
		constexpr int64_t SERIAL_HALF_RANGE				  = (1LL << (SERIAL_BITS - 1));
		constexpr int64_t SIGNED_SERIAL_MODULO			  = (1LL << (SERIAL_BITS - 1));
		constexpr uint64_t SERIAL_VALUE_MASK			  = 0xFFFFFFFFULL;
		constexpr int64_t MAX_SIGNED_WRAP_COMPAT_DELTA_MS = 10 * 1000;

		const auto last_serial							  = static_cast<uint32_t>(last_timestamp & SERIAL_VALUE_MASK);
		const auto parsed_serial						  = static_cast<uint32_t>(parsed_timestamp);

		if (last_serial == parsed_serial)
		{
			return last_timestamp;
		}

		const int64_t serial_epoch_base = last_timestamp - static_cast<int64_t>(last_serial);
		int64_t resolved_timestamp		= serial_epoch_base + parsed_serial;
		const int64_t serial_diff		= static_cast<int64_t>(parsed_serial) - last_serial;
		const bool parsed_serial_is_after_previous =
			((serial_diff > 0) && (serial_diff < SERIAL_HALF_RANGE)) ||
			((serial_diff < 0) && ((-serial_diff) > SERIAL_HALF_RANGE));

		const int64_t signed_wrap_forward_delta = SIGNED_SERIAL_MODULO - static_cast<int64_t>(last_serial) + parsed_serial;
		const bool signed_wrap_compat_candidate =
			(parsed_serial_is_after_previous == false) &&
			(last_serial < SIGNED_SERIAL_MODULO) &&
			(parsed_serial < SIGNED_SERIAL_MODULO) &&
			(parsed_serial < last_serial) &&
			(signed_wrap_forward_delta > 0) &&
			(signed_wrap_forward_delta <= MAX_SIGNED_WRAP_COMPAT_DELTA_MS);

		if (parsed_serial_is_after_previous)
		{
			if (resolved_timestamp <= last_timestamp)
			{
				resolved_timestamp += SERIAL_MODULO;
				logtd("Timestamp is rolled forward: last TS: %" PRId64 ", parsed: %" PRId64 ", resolved: %" PRId64,
					  last_timestamp,
					  parsed_timestamp,
					  resolved_timestamp);
			}
		}
		else if (signed_wrap_compat_candidate)
		{
			resolved_timestamp = last_timestamp + signed_wrap_forward_delta;
			logtd("Timestamp is rolled forward with signed-31bit compatibility: last TS: %" PRId64 ", parsed: %" PRId64 ", delta: %" PRId64 ", resolved: %" PRId64,
				  last_timestamp,
				  parsed_timestamp,
				  signed_wrap_forward_delta,
				  resolved_timestamp);
		}
		else if (resolved_timestamp > last_timestamp)
		{
			resolved_timestamp -= SERIAL_MODULO;
			logti("Timestamp is resolved as backward T0: last TS: %" PRId64 ", parsed: %" PRId64 ", resolved: %" PRId64,
				  last_timestamp,
				  parsed_timestamp,
				  resolved_timestamp);
		}

		return resolved_timestamp;
	}
}  // namespace
