//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2024 OvenMedia Labs. All rights reserved.
//
//==============================================================================

#include "h264_sei.h"

#include <ctype.h>

#include <base/ovlibrary/bit_reader.h>
#include <base/ovlibrary/bit_writer.h>
#include <base/ovlibrary/ovlibrary.h>

#define OV_LOG_TAG "H264SEI"

namespace
{
	// Sign extends a value read as i(v): the top bit of <bits> is the sign bit.
	int32_t SignExtend(uint32_t value, uint8_t bits)
	{
		if ((bits == 0) || (bits >= 32))
		{
			return static_cast<int32_t>(value);
		}

		const uint32_t sign_bit = 1U << (bits - 1);
		if ((value & sign_bit) != 0)
		{
			// Set the bits above the field so that the two's complement is preserved
			value |= ~((1U << bits) - 1);
		}

		return static_cast<int32_t>(value);
	}
}  // namespace

std::shared_ptr<ov::Data> H264SEI::Serialize()
{
	// sei_payload()
	ov::BitWriter sei_payload_data(1024);

	// The payload is written exactly as given: whoever supplies it owns the whole layout,
	// including any UUID of its own
	if (_payload_data != nullptr)
	{
		sei_payload_data.WriteData(_payload_data->GetDataAs<uint8_t>(), _payload_data->GetLength());
	}

	// sei_message()
	auto payload_size = sei_payload_data.GetDataSize();

	ov::BitWriter writer(payload_size + 3);	 // payload data + payload_size(max 2)  + rbsp

	// Payload Type (8)
	writer.WriteBytes<uint8_t>(_payload_type);

	auto remaining_size = payload_size;
	while (remaining_size >= 255)
	{
		writer.WriteBytes<uint8_t>(255);
		remaining_size -= 255;
	}
	writer.WriteBytes<uint8_t>(remaining_size);

	writer.WriteData(sei_payload_data.GetData(), sei_payload_data.GetDataSize());

	// rbsp_trailing_bits()
	writer.WriteBits(8, _rbsp_trailing_bits);

	return writer.GetDataObject();
}

bool H264SEI::SplitMessages(const std::shared_ptr<const ov::Data> &rbsp, std::vector<Message> &messages)
{
	if (rbsp == nullptr)
	{
		return false;
	}

	const auto *buffer = rbsp->GetDataAs<uint8_t>();
	size_t length	   = rbsp->GetLength();
	size_t position	   = 0;

	// NalUnitFragmentHeader::Parse folds trailing_zero_8bits into the NAL, so trim it back off
	// when that uncovers rbsp_trailing_bits()
	size_t trimmed = length;
	while ((trimmed > 0) && (buffer[trimmed - 1] == 0x00))
	{
		trimmed--;
	}
	if ((trimmed > 0) && (buffer[trimmed - 1] == 0x80))
	{
		length = trimmed;
	}

	while (position < length)
	{
		// rbsp_trailing_bits() is a single 0x80 byte, because sei_payload() always leaves the
		// bitstream byte aligned.
		if (((length - position) == 1) && (buffer[position] == 0x80))
		{
			break;
		}

		// payloadType, 0xFF continuation
		uint32_t payload_type = 0;
		while ((position < length) && (buffer[position] == 0xFF))
		{
			payload_type += 255;
			position++;
		}
		if (position >= length)
		{
			return false;
		}
		payload_type += buffer[position++];

		// payloadSize, 0xFF continuation
		uint32_t payload_size = 0;
		while ((position < length) && (buffer[position] == 0xFF))
		{
			payload_size += 255;
			position++;
		}
		if (position >= length)
		{
			return false;
		}
		payload_size += buffer[position++];

		if ((position + payload_size) > length)
		{
			return false;
		}

		Message message;
		message.payload_type = payload_type;
		message.payload		 = std::make_shared<ov::Data>(buffer + position, payload_size);
		messages.push_back(std::move(message));

		position += payload_size;
	}

	return true;
}

std::shared_ptr<ov::Data> H264SEI::JoinMessages(const std::vector<Message> &messages)
{
	// payloadType and payloadSize need at most a few bytes each, plus rbsp_trailing_bits
	size_t reserve = 1;
	for (const auto &message : messages)
	{
		reserve += 8 + ((message.payload != nullptr) ? message.payload->GetLength() : 0);
	}

	ov::BitWriter writer(reserve);

	for (const auto &message : messages)
	{
		uint32_t remaining_type = message.payload_type;
		while (remaining_type >= 255)
		{
			writer.WriteBytes<uint8_t>(255);
			remaining_type -= 255;
		}
		writer.WriteBytes<uint8_t>(static_cast<uint8_t>(remaining_type));

		size_t remaining_size = (message.payload != nullptr) ? message.payload->GetLength() : 0;
		while (remaining_size >= 255)
		{
			writer.WriteBytes<uint8_t>(255);
			remaining_size -= 255;
		}
		writer.WriteBytes<uint8_t>(static_cast<uint8_t>(remaining_size));

		if ((message.payload != nullptr) && (message.payload->GetLength() > 0))
		{
			writer.WriteData(message.payload->GetDataAs<uint8_t>(), message.payload->GetLength());
		}
	}

	// rbsp_trailing_bits()
	writer.WriteBytes<uint8_t>(0x80);

	return writer.GetDataObject();
}

ov::String H264SEI::GetInfoString()
{
	ov::String info;

	info.AppendFormat("Payload Type: %s (%u, ", PayloadTypeToString(_payload_type).CStr(), static_cast<uint8_t>(_payload_type));
	if (_payload_data != nullptr)
	{
		info.AppendFormat("Payload Data (Data): %zu bytes\n%s", _payload_data->GetLength(), _payload_data->Dump(nullptr, nullptr).CStr());
	}

	return info;
}

bool H264SEI::ParsePicTiming(const std::shared_ptr<const ov::Data> &payload,
							 const H264SeiSpsContext &sps_context,
							 H264SeiPicTiming &pic_timing)
{
	if ((payload == nullptr) || (sps_context.IsValid() == false))
	{
		return false;
	}

	// The payload is an RBSP slice, so a plain BitReader is used. Using
	// NalUnitBitstreamParser here would skip emulation prevention bytes a second time.
	BitReader reader(payload->GetDataAs<uint8_t>(), payload->GetLength());

	if (sps_context.cpb_dpb_delays_present)
	{
		if ((reader.ReadBits(sps_context.cpb_removal_delay_length, pic_timing.cpb_removal_delay) == false) ||
			(reader.ReadBits(sps_context.dpb_output_delay_length, pic_timing.dpb_output_delay) == false))
		{
			return false;
		}
	}

	if (sps_context.pic_struct_present == false)
	{
		// Without pic_struct there is no clock timestamp at all.
		return true;
	}

	if (reader.ReadBits(4, pic_timing.pic_struct) == false)
	{
		return false;
	}

	auto num_clock_ts = H264SeiPicTiming::NumClockTSFromPicStruct(pic_timing.pic_struct);
	if (num_clock_ts == 0)
	{
		// Reserved pic_struct value. The rest cannot be interpreted.
		return false;
	}

	for (uint8_t index = 0; index < num_clock_ts; index++)
	{
		uint8_t clock_timestamp_flag = 0;
		if (reader.ReadBit(clock_timestamp_flag) == false)
		{
			return false;
		}

		if (clock_timestamp_flag == 0)
		{
			// Keep the slot: SerializePicTiming() reads the vector index as the slot number
			pic_timing.clock_timestamps.emplace_back().present = false;
			continue;
		}

		H264SeiClockTimestamp timestamp;
		uint8_t units_field_based_flag = 0, full_timestamp_flag = 0;
		uint8_t discontinuity_flag = 0, cnt_dropped_flag = 0;

		if ((reader.ReadBits(2, timestamp.ct_type) == false) ||
			(reader.ReadBit(units_field_based_flag) == false) ||
			(reader.ReadBits(5, timestamp.counting_type) == false) ||
			(reader.ReadBit(full_timestamp_flag) == false) ||
			(reader.ReadBit(discontinuity_flag) == false) ||
			(reader.ReadBit(cnt_dropped_flag) == false))
		{
			return false;
		}

		uint8_t n_frames = 0;
		if (reader.ReadBits(8, n_frames) == false)
		{
			return false;
		}

		timestamp.units_field_based_flag = (units_field_based_flag != 0);
		timestamp.full_timestamp_flag	= (full_timestamp_flag != 0);
		timestamp.discontinuity_flag	= (discontinuity_flag != 0);
		timestamp.cnt_dropped_flag		= (cnt_dropped_flag != 0);
		timestamp.n_frames				= n_frames;
		timestamp.time_offset_length	= sps_context.time_offset_length;

		if (full_timestamp_flag)
		{
			if ((reader.ReadBits(6, timestamp.seconds) == false) ||
				(reader.ReadBits(6, timestamp.minutes) == false) ||
				(reader.ReadBits(5, timestamp.hours) == false))
			{
				return false;
			}
		}
		else
		{
			uint8_t seconds_flag = 0, minutes_flag = 0, hours_flag = 0;
			if (reader.ReadBit(seconds_flag) == false)
			{
				return false;
			}
			if (seconds_flag)
			{
				if ((reader.ReadBits(6, timestamp.seconds) == false) ||
					(reader.ReadBit(minutes_flag) == false))
				{
					return false;
				}
				if (minutes_flag)
				{
					if ((reader.ReadBits(6, timestamp.minutes) == false) ||
						(reader.ReadBit(hours_flag) == false))
					{
						return false;
					}
					if (hours_flag)
					{
						if (reader.ReadBits(5, timestamp.hours) == false)
						{
							return false;
						}
					}
				}
			}
		}

		if (sps_context.time_offset_length > 0)
		{
			uint32_t raw_time_offset = 0;
			if (reader.ReadBits(sps_context.time_offset_length, raw_time_offset) == false)
			{
				return false;
			}

			// time_offset is i(v): sign extend from time_offset_length bits
			timestamp.time_offset = SignExtend(raw_time_offset, sps_context.time_offset_length);
		}

		pic_timing.clock_timestamps.push_back(timestamp);
	}

	return true;
}

std::shared_ptr<ov::Data> H264SEI::SerializePicTiming(const H264SeiPicTiming &pic_timing,
													  const H264SeiSpsContext &sps_context)
{
	if (sps_context.IsValid() == false)
	{
		logte("Invalid SPS context: %s", sps_context.GetInfoString().CStr());
		return nullptr;
	}

	// Worst case is roughly 3 clock timestamps of ~70 bits plus the delays
	ov::BitWriter writer(64);

	if (sps_context.cpb_dpb_delays_present)
	{
		writer.WriteBits(sps_context.cpb_removal_delay_length, pic_timing.cpb_removal_delay);
		writer.WriteBits(sps_context.dpb_output_delay_length, pic_timing.dpb_output_delay);
	}

	if (sps_context.pic_struct_present)
	{
		writer.WriteBits(4, pic_timing.pic_struct);

		auto num_clock_ts = H264SeiPicTiming::NumClockTSFromPicStruct(pic_timing.pic_struct);
		if (num_clock_ts == 0)
		{
			logte("Reserved pic_struct value: %u", pic_timing.pic_struct);
			return nullptr;
		}

		for (uint8_t index = 0; index < num_clock_ts; index++)
		{
			if ((index >= pic_timing.clock_timestamps.size()) ||
				(pic_timing.clock_timestamps[index].present == false))
			{
				// clock_timestamp_flag = 0, this slot carries no timestamp
				writer.WriteBits(1, 0);
				continue;
			}

			const auto &timestamp = pic_timing.clock_timestamps[index];

			writer.WriteBits(1, 1);	 // clock_timestamp_flag
			writer.WriteBits(2, timestamp.ct_type);
			writer.WriteBits(1, timestamp.units_field_based_flag ? 1 : 0);
			writer.WriteBits(5, timestamp.counting_type);
			writer.WriteBits(1, timestamp.full_timestamp_flag ? 1 : 0);
			writer.WriteBits(1, timestamp.discontinuity_flag ? 1 : 0);
			writer.WriteBits(1, timestamp.cnt_dropped_flag ? 1 : 0);
			writer.WriteBits(8, timestamp.n_frames & 0xFF);

			if (timestamp.full_timestamp_flag)
			{
				writer.WriteBits(6, timestamp.seconds);
				writer.WriteBits(6, timestamp.minutes);
				writer.WriteBits(5, timestamp.hours);
			}
			else
			{
				// The full form is always written, so the differential form stays empty
				writer.WriteBits(1, 0);	 // seconds_flag
			}

			if (sps_context.time_offset_length > 0)
			{
				// i(v): write the two's complement truncated to time_offset_length bits
				uint64_t raw = static_cast<uint64_t>(static_cast<int64_t>(timestamp.time_offset));
				uint64_t mask = (sps_context.time_offset_length >= 64)
									? UINT64_MAX
									: ((1ULL << sps_context.time_offset_length) - 1);
				writer.WriteBits(sps_context.time_offset_length, raw & mask);
			}
		}
	}

	// sei_payload() trailing bits (ITU-T H.264 7.3.2.3.1): payloadSize is in bytes, so a payload
	// that does not end on a byte boundary has to be padded.
	if ((writer.GetBitCount() % 8) != 0)
	{
		writer.WriteBits(1, 1);	 // payload_bit_equal_to_one
		while ((writer.GetBitCount() % 8) != 0)
		{
			writer.WriteBits(1, 0);	 // payload_bit_equal_to_zero
		}
	}

	return writer.GetDataObject();
}

bool H264SeiTimecodeZone::Parse(const ov::String &text, H264SeiTimecodeZone &zone)
{
	auto value = text.Trim();
	auto upper = value.UpperCaseString();

	// An absent setting reads as UTC, so a stream carries the same timecode wherever it runs
	if ((value.IsEmpty() == true) || (upper == "UTC") || (upper == "Z"))
	{
		zone.local			= false;
		zone.offset_seconds = 0;

		return true;
	}

	if (upper == "LOCAL")
	{
		zone.local			= true;
		zone.offset_seconds = 0;

		return true;
	}

	const char sign = value[0];
	if ((sign != '+') && (sign != '-'))
	{
		return false;
	}

	// +HH, +HHMM and +HH:MM all reduce to the same digits
	auto digits = value.Substring(1).Replace(":", "");
	if ((digits.GetLength() != 2) && (digits.GetLength() != 4))
	{
		return false;
	}

	for (size_t i = 0; i < digits.GetLength(); i++)
	{
		if (::isdigit(static_cast<unsigned char>(digits[i])) == 0)
		{
			return false;
		}
	}

	const int32_t hours   = ((digits[0] - '0') * 10) + (digits[1] - '0');
	const int32_t minutes = (digits.GetLength() == 4) ? (((digits[2] - '0') * 10) + (digits[3] - '0')) : 0;
	const int32_t offset  = (hours * 3600) + (minutes * 60);

	// +14:00 is the widest offset anywhere in use
	if ((minutes > 59) || (offset > (14 * 3600)))
	{
		return false;
	}

	zone.local			= false;
	zone.offset_seconds = (sign == '-') ? -offset : offset;

	return true;
}

ov::String H264SeiTimecodeZone::ToString() const
{
	if (local == true)
	{
		return "Local";
	}

	if (offset_seconds == 0)
	{
		return "UTC";
	}

	const int32_t magnitude = (offset_seconds < 0) ? -offset_seconds : offset_seconds;

	return ov::String::FormatString("%c%02d:%02d", (offset_seconds < 0) ? '-' : '+',
									magnitude / 3600, (magnitude % 3600) / 60);
}
