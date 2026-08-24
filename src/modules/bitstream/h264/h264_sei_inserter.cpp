//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2026 OvenMedia Labs. All rights reserved.
//
//==============================================================================

#include "h264_sei_inserter.h"

#include <base/ovlibrary/byte_io.h>
#include <modules/bitstream/nalu/nalu.h>

#include "h264_common.h"
#include "h264_sps_rewriter.h"

#define OV_LOG_TAG "H264SeiInserter"

namespace
{
	constexpr int64_t SECONDS_PER_DAY = 24 * 60 * 60;

	// Log prefix shared with the rest of MediaRouter and Transcoder
	ov::String LogPrefix(const std::shared_ptr<info::Stream> &stream)
	{
		return ov::String::FormatString("[%s]", stream->GetUri().CStr());
	}

	// A larger jump is a new anchor point, not elapsed time
	constexpr double DISCONTINUITY_THRESHOLD_SEC = 2.0;
}  // namespace

bool H264TimecodeGenerator::IsDropFrameRate(double fps)
{
	// 1000/1001 rates only: 29.97 and 59.94. 23.976 is excluded because SMPTE defines no drop
	// frame counting for it.
	if (fps <= 0.0)
	{
		return false;
	}

	for (const int32_t nominal : {30, 60})
	{
		if (::fabs(fps - ((nominal * 1000.0) / 1001.0)) < 0.01)
		{
			return true;
		}
	}

	return false;
}

void H264TimecodeGenerator::FrameNumberToTimecode(int64_t frame_number, int32_t nominal_fps,
												  bool drop_frame,
												  uint8_t &hours, uint8_t &minutes, uint8_t &seconds,
												  uint16_t &frames)
{
	// Guard the divisions below: a measured frame rate under 0.5 rounds to 0
	if (nominal_fps < 1)
	{
		nominal_fps = 1;
	}

	int64_t counted = frame_number;

	if (drop_frame == true)
	{
		// SMPTE 12M: skip the lowest frame numbers at the start of every minute except every
		// tenth. 30 nominal drops 2 per minute, 60 nominal drops 4.
		const int64_t dropped_per_minute = nominal_fps / 15;
		const int64_t frames_per_minute	 = (nominal_fps * 60LL) - dropped_per_minute;
		const int64_t frames_per_10min	 = (nominal_fps * 600LL) - (9 * dropped_per_minute);

		const int64_t ten_minute_blocks = frame_number / frames_per_10min;
		const int64_t remainder			= frame_number % frames_per_10min;

		counted = frame_number + (9 * dropped_per_minute * ten_minute_blocks);
		if (remainder >= dropped_per_minute)
		{
			counted += dropped_per_minute * ((remainder - dropped_per_minute) / frames_per_minute);
		}
	}

	frames	= static_cast<uint16_t>(counted % nominal_fps);
	seconds = static_cast<uint8_t>((counted / nominal_fps) % 60);
	minutes = static_cast<uint8_t>((counted / (nominal_fps * 60LL)) % 60);
	hours	= static_cast<uint8_t>((counted / (nominal_fps * 3600LL)) % 24);
}

double H264TimecodeGenerator::GetTimeOfDaySeconds() const
{
	struct timespec now = {};
	::clock_gettime(CLOCK_REALTIME, &now);

	const double fraction = now.tv_nsec / 1000000000.0;

	if (_timezone.local == true)
	{
		struct tm broken_down = {};
		::localtime_r(&now.tv_sec, &broken_down);

		return (broken_down.tm_hour * 3600.0) +
			   (broken_down.tm_min * 60.0) +
			   broken_down.tm_sec +
			   fraction;
	}

	// Unix time counts no leap seconds, so every day is exactly SECONDS_PER_DAY long and a fixed
	// offset needs no time zone database
	int64_t seconds_of_day = (now.tv_sec + _timezone.offset_seconds) % SECONDS_PER_DAY;
	if (seconds_of_day < 0)
	{
		seconds_of_day += SECONDS_PER_DAY;
	}

	return seconds_of_day + fraction;
}

void H264TimecodeGenerator::Reset()
{
	_anchored			   = false;
	_discontinuity		   = true;
	_has_last_frame_number = false;
}

bool H264TimecodeGenerator::Generate(int64_t pts, double timebase_expr, double fps,
									 H264SeiClockTimestamp &timestamp)
{
	if ((fps <= 0.0) || (timebase_expr <= 0.0))
	{
		// Without a frame rate there is no way to derive n_frames
		return false;
	}

	if (_anchored == true)
	{
		// Re-anchor on any break in the presentation timeline
		const double delta = static_cast<double>(pts - _last_pts) * timebase_expr;
		if ((delta < 0.0) || (delta > DISCONTINUITY_THRESHOLD_SEC))
		{
			_anchored = false;
		}
	}

	if (_anchored == false)
	{
		_anchor_pts = pts;

		// Time of day at the first stamped picture, read in the configured zone
		_anchor_value = GetTimeOfDaySeconds();

		_anchored	   = true;
		_discontinuity = true;
	}

	_last_pts = pts;

	const bool drop_frame = IsDropFrameRate(fps);

	// The counting rate is the nominal one (30 for 29.97). lround(), not ceil(): a measured
	// 30.0041 would become 31 and the timecode would run slow.
	const int32_t nominal_fps = static_cast<int32_t>(::lround(drop_frame ? (fps * 1.001) : fps));

	// Rounded separately, never as one sum. The PTS is quantized to the track's timebase, so the
	// elapsed part lands a hair off the frame grid (0.96, 1.98, 3.00 at 60 fps on 1/1000), which
	// rounding absorbs on its own. Adding the anchor first would not: it is a wall clock reading,
	// and a sub-frame phase near 0.5 tips every one of those to the wrong side.
	int64_t frame_number = ::llround(_anchor_value * fps) +
						   ::llround(static_cast<double>(pts - _anchor_pts) * timebase_expr * fps);

	// Wrap at midnight: hours_value is u(5) and must stay below 24
	const int64_t frames_per_day = ::llround(SECONDS_PER_DAY * fps);
	if (frames_per_day > 0)
	{
		frame_number %= frames_per_day;
		if (frame_number < 0)
		{
			frame_number += frames_per_day;
		}
	}

	FrameNumberToTimecode(frame_number, nominal_fps, drop_frame,
						  timestamp.hours, timestamp.minutes, timestamp.seconds, timestamp.n_frames);

	// cnt_dropped_flag marks a picture where drop frame counting skipped numbers
	bool dropped = false;
	if (drop_frame == true && _has_last_frame_number == true && frame_number == (_last_frame_number + 1))
	{
		uint8_t previous_hours = 0, previous_minutes = 0, previous_seconds = 0;
		uint16_t previous_frames = 0;
		FrameNumberToTimecode(_last_frame_number, nominal_fps, drop_frame,
							  previous_hours, previous_minutes, previous_seconds, previous_frames);

		// A new second must restart at 0 unless numbers were skipped
		dropped = (previous_seconds != timestamp.seconds) && (timestamp.n_frames != 0);
	}

	_has_last_frame_number = true;
	_last_frame_number	   = frame_number;

	timestamp.units_field_based_flag = false;
	timestamp.counting_type			 = drop_frame ? 2 : 1;
	timestamp.full_timestamp_flag	 = true;
	timestamp.cnt_dropped_flag		 = dropped;
	timestamp.discontinuity_flag	 = _discontinuity;
	timestamp.time_offset			 = 0;

	_discontinuity = false;

	return true;
}

namespace
{
	bool IsVclNalUnitType(H264NalUnitType nal_unit_type)
	{
		return (nal_unit_type >= H264NalUnitType::NonIdrSlice) &&
			   (nal_unit_type <= H264NalUnitType::IdrSlice);
	}

}  // namespace

bool H264SeiInserter::IsInsertable(const std::shared_ptr<info::Stream> &stream,
								   const std::shared_ptr<const MediaTrack> &track,
								   const std::shared_ptr<MediaPacket> &packet,
								   bool *warned)
{
	// True the first time for this track when `warned` is given, every time when it is not
	auto should_warn = [warned]() {
		if (warned == nullptr)
		{
			return true;
		}

		if (*warned == true)
		{
			return false;
		}

		*warned = true;

		return true;
	};

	if (packet->GetData() == nullptr)
	{
		if (should_warn() == true)
		{
			logtw("%s SEI insertion needs a packet with data. track(%u)",
				  LogPrefix(stream).CStr(), packet->GetTrackId());
		}

		return false;
	}

	if (track->GetCodecId() != cmn::MediaCodecId::H264)
	{
		// H.265 needs its own NAL header and a different timecode SEI, not implemented
		if (should_warn() == true)
		{
			logtw("%s SEI insertion is only supported for H264. track(%u)",
				  LogPrefix(stream).CStr(), packet->GetTrackId());
		}

		return false;
	}

	const auto format = packet->GetBitstreamFormat();
	if ((format != cmn::BitstreamFormat::H264_ANNEXB) && (format != cmn::BitstreamFormat::H264_AVCC))
	{
		if (should_warn() == true)
		{
			logtw("%s Not supported bitstream format for SEI insertion: %s. track(%u)",
				  LogPrefix(stream).CStr(), GetBitstreamFormatString(format), packet->GetTrackId());
		}

		return false;
	}

	return true;
}

std::optional<size_t> H264SeiInserter::FindFirstVclIndex(const uint8_t *buffer,
														 const FragmentationHeader *fragment)
{
	if ((buffer == nullptr) || (fragment == nullptr))
	{
		return std::nullopt;
	}

	for (size_t i = 0; i < fragment->GetCount(); i++)
	{
		auto nal = fragment->GetFragment(i);
		if (nal.has_value() == false)
		{
			continue;
		}

		const size_t offset = std::get<0>(nal.value());
		const size_t length = std::get<1>(nal.value());
		if (length < 1)
		{
			continue;
		}

		if (IsVclNalUnitType(static_cast<H264NalUnitType>(buffer[offset] & kH264NalUnitTypeMask)) == true)
		{
			return i;
		}
	}

	return std::nullopt;
}

bool H264SeiInserter::EmitSeiNal(const std::shared_ptr<info::Stream> &stream,
								 const std::shared_ptr<MediaPacket> &packet,
								 const FragmentationHeader *fragment,
								 const std::shared_ptr<ov::Data> &rbsp,
								 bool replace,
								 size_t nal_index)
{
	if ((fragment == nullptr) || (rbsp == nullptr))
	{
		logte("%s Invalid argument. track(%u)",
			  LogPrefix(stream).CStr(), packet->GetTrackId());
		return false;
	}

	auto nalu = std::make_shared<ov::Data>();
	nalu->Append(NalHeader::CreateH264(static_cast<uint8_t>(H264NalUnitType::Sei)));
	nalu->Append(rbsp);

	const auto format = packet->GetBitstreamFormat();
	auto result		  = (replace == true)
							? NalUnitInsertor::Replace(packet->GetData(), fragment, nalu, format, nal_index)
							: NalUnitInsertor::Insert(packet->GetData(), fragment, nalu, format, nal_index);

	if (result.has_value() == false)
	{
		logte("%s Could not %s the SEI NAL unit at %zu. track(%u)",
			  LogPrefix(stream).CStr(), (replace == true) ? "replace" : "insert", nal_index,
			  packet->GetTrackId());
		return false;
	}

	packet->SetData(std::get<0>(result.value()));
	packet->SetFragHeader(std::get<1>(result.value()).get());

	return true;
}

bool H264SeiInserter::InsertPicTiming(const std::shared_ptr<info::Stream> &stream,
									  const std::shared_ptr<const MediaTrack> &track,
									  const std::shared_ptr<MediaPacket> &packet)
{
	if (_log_prefix.IsEmpty() == true)
	{
		_log_prefix = LogPrefix(stream);
	}

	if (IsInsertable(stream, track, packet, &_warned_unsupported) == false)
	{
		return false;
	}

	if (_started == false)
	{
		_started = true;

		// Drop any stale anchor so the timecode starts from this picture
		_generator.Reset();

		logti("%s Started stamping pic_timing on every picture. track(%u), timezone(%s)",
			  _log_prefix.CStr(), packet->GetTrackId(),
			  _generator.GetTimezone().ToString().CStr());
	}

	// Parsing the fragment header walks the whole bitstream, so it is done once here and handed
	// down. PatchSps() refreshes it if it rebuilds the access unit.
	NalUnitFragmentHeader fragment_header;
	if (NalUnitFragmentHeader::Parse(packet->GetData(), fragment_header) == false)
	{
		logte("%s Failed to parse NALU fragment header. track(%u)",
			  _log_prefix.CStr(), packet->GetTrackId());
		return false;
	}

	// The SPS that precedes the SEI has to carry the flag already
	if (PatchSps(packet, fragment_header) == false)
	{
		return false;
	}

	return Stamp(stream, track, packet, fragment_header.GetFragmentHeader());
}

bool H264SeiInserter::InsertUserData(const std::shared_ptr<info::Stream> &stream,
									 const std::shared_ptr<const MediaTrack> &track,
									 const std::shared_ptr<MediaPacket> &packet,
									 const ov::String &data)
{
	if (IsInsertable(stream, track, packet, nullptr) == false)
	{
		return false;
	}

	const auto current_time = ov::Time::GetTimestampInMs();
	auto payload_data		= data.Replace("${EpochTime}", ov::String::FormatString("%" PRId64, current_time));

	// UUID(16) + timeCode(8, big endian) + data. The format OvenPlayer reads.
	auto payload = std::make_shared<ov::Data>(sizeof(OME_USER_DATA_UUID) + sizeof(uint64_t) + payload_data.GetLength());
	payload->Append(OME_USER_DATA_UUID, sizeof(OME_USER_DATA_UUID));

	uint8_t time_code[sizeof(uint64_t)] = {0};
	ByteWriter<uint64_t>::WriteBigEndian(time_code, static_cast<uint64_t>(current_time));
	payload->Append(time_code, sizeof(time_code));
	payload->Append(payload_data.ToData());

	H264SEI sei;
	sei.SetPayloadType(H264SEI::PayloadType::USER_DATA_UNREGISTERED);
	sei.SetPayloadData(payload);

	auto rbsp = sei.Serialize();
	if (rbsp == nullptr)
	{
		logte("%s Failed to serialize a user_data_unregistered SEI. track(%u)",
			  LogPrefix(stream).CStr(), packet->GetTrackId());
		return false;
	}

	NalUnitFragmentHeader fragment_header;
	if (NalUnitFragmentHeader::Parse(packet->GetData(), fragment_header) == false)
	{
		logte("%s Failed to parse NALU fragment header. track(%u)",
			  LogPrefix(stream).CStr(), packet->GetTrackId());
		return false;
	}

	const FragmentationHeader *fragment = fragment_header.GetFragmentHeader();
	if (fragment == nullptr)
	{
		logte("%s Failed to get NALU fragment header. track(%u)",
			  LogPrefix(stream).CStr(), packet->GetTrackId());
		return false;
	}

	// A suffix SEI does not exist in H.264, so it goes in front of the primary coded picture.
	// APPEND_TO_END only as a fallback: an access unit with no VCL NAL is not a picture.
	const auto *buffer = packet->GetData()->GetDataAs<uint8_t>();
	const size_t nal_index =
		FindFirstVclIndex(buffer, fragment).value_or(NalUnitInsertor::APPEND_TO_END);

	return EmitSeiNal(stream, packet, fragment, rbsp, false, nal_index);
}

H264SeiInserter::SeiPlacement H264SeiInserter::DecideSeiPlacement(
	MediaTrackId track_id,
	const std::shared_ptr<MediaPacket> &packet,
	const FragmentationHeader *fragment)
{
	SeiPlacement placement;

	const auto *buffer = packet->GetData()->GetDataAs<uint8_t>();

	std::optional<size_t> pic_timing_nal_index;
	std::vector<H264SEI::Message> pic_timing_messages;
	size_t pic_timing_index = 0;

	for (size_t i = 0; i < fragment->GetCount(); i++)
	{
		auto nal = fragment->GetFragment(i);
		if (nal.has_value() == false)
		{
			continue;
		}

		const size_t offset = std::get<0>(nal.value());
		const size_t length = std::get<1>(nal.value());
		if (length < 1)
		{
			continue;
		}

		const auto nal_unit_type = static_cast<H264NalUnitType>(buffer[offset] & kH264NalUnitTypeMask);

		if (nal_unit_type == H264NalUnitType::Sps)
		{
			// ParseSPS expects the NAL header and handles emulation prevention bytes itself
			H264SPS sps;
			if (H264Parser::ParseSPS(buffer + offset, length, sps) == true)
			{
				_sps_context	  = MakeSpsContext(sps);
				_has_sps_context = true;
			}

			continue;
		}

		if ((nal_unit_type == H264NalUnitType::Sei) && (pic_timing_nal_index.has_value() == false))
		{
			// Skip the NAL header, then unescape so that the messages can be split byte wise
			auto escaped = std::make_shared<ov::Data>(buffer + offset + 1, length - 1);
			auto rbsp	 = NalUnitInsertor::RemoveEmulationPreventionBytes(escaped);

			std::vector<H264SEI::Message> messages;
			if (H264SEI::SplitMessages(rbsp, messages) == true)
			{
				for (size_t index = 0; index < messages.size(); index++)
				{
					if (messages[index].Is(H264SEI::PayloadType::PICTURE_TIMING) == true)
					{
						pic_timing_nal_index = i;
						pic_timing_messages	 = std::move(messages);
						pic_timing_index	 = index;
						break;
					}
				}
			}

			continue;
		}
	}

	if (_has_sps_context == false)
	{
		// The SPS is in band only, so stamping starts at the first parameter set
		if (_warned_no_sps == false)
		{
			_warned_no_sps = true;
			logtw("%s Could not stamp pic_timing yet: no SPS has been seen. track(%u)",
				  _log_prefix.CStr(), track_id);
		}

		return placement;
	}

	if (_sps_context.pic_struct_present == false)
	{
		// Without the flag a decoder reads neither pic_struct nor the clock timestamps
		if (_warned_no_pic_struct == false)
		{
			_warned_no_pic_struct = true;
			logtw("%s Could not stamp pic_timing: the SPS has pic_struct_present_flag=0. track(%u), sps(%s)",
				  _log_prefix.CStr(), track_id, _sps_context.GetInfoString().CStr());
		}

		return placement;
	}

	if (pic_timing_nal_index.has_value() == true)
	{
		// Only one pic_timing per access unit (ITU-T H.264 7.4.1.2.3): rewrite the existing one
		// and keep its cpb/dpb delays
		placement.action		  = SeiPlacement::Action::Replace;
		placement.nal_index		  = pic_timing_nal_index.value();
		placement.messages		  = std::move(pic_timing_messages);
		placement.pic_timing_index = pic_timing_index;

		return placement;
	}

	auto first_vcl_index = FindFirstVclIndex(buffer, fragment);
	if (first_vcl_index.has_value() == true)
	{
		placement.action	= SeiPlacement::Action::Insert;
		placement.nal_index = first_vcl_index.value();
	}

	return placement;
}

bool H264SeiInserter::PatchSps(const std::shared_ptr<MediaPacket> &packet,
							   NalUnitFragmentHeader &fragment_header)
{
	const auto format = packet->GetBitstreamFormat();

	bool patched = false;

	// One pass per SPS: each patch rebuilds the access unit, so the scan restarts
	for (size_t guard = 0; guard < 8; guard++)
	{
		const FragmentationHeader *fragment = fragment_header.GetFragmentHeader();
		if (fragment == nullptr)
		{
			break;
		}

		const auto *buffer					 = packet->GetData()->GetDataAs<uint8_t>();
		std::optional<size_t> target_index	 = std::nullopt;
		std::shared_ptr<ov::Data> patched_sps = nullptr;

		for (size_t i = 0; i < fragment->GetCount(); i++)
		{
			auto nal = fragment->GetFragment(i);
			if (nal.has_value() == false)
			{
				continue;
			}

			const size_t offset = std::get<0>(nal.value());
			const size_t length = std::get<1>(nal.value());
			if (length < 1)
			{
				continue;
			}

			if (static_cast<H264NalUnitType>(buffer[offset] & kH264NalUnitTypeMask) != H264NalUnitType::Sps)
			{
				continue;
			}

			auto sps_nal = std::make_shared<ov::Data>(buffer + offset, length);
			// nullptr when the flag is already set
			auto result = H264SpsRewriter::EnablePicStructPresent(sps_nal);
			if (result != nullptr)
			{
				target_index = i;
				patched_sps	 = result;
				break;
			}
		}

		if (target_index.has_value() == false)
		{
			break;
		}

		auto replaced = NalUnitInsertor::Replace(packet->GetData(), fragment, patched_sps,
												 format, target_index.value());
		if (replaced.has_value() == false)
		{
			if (_warned_patch_failed == false)
			{
				_warned_patch_failed = true;
				logtw("%s Could not replace the patched SPS. track(%u)",
					  _log_prefix.CStr(), packet->GetTrackId());
			}
			break;
		}

		packet->SetData(std::get<0>(replaced.value()));
		packet->SetFragHeader(std::get<1>(replaced.value()).get());
		patched = true;

		if (_logged_patch_applied == false)
		{
			_logged_patch_applied = true;
			logti("%s Enabled pic_struct_present_flag in the SPS so that pic_timing can carry a timecode. track(%u)",
				  _log_prefix.CStr(), packet->GetTrackId());
		}

		// The access unit is a different buffer now, so the caller's header has to follow it
		if (NalUnitFragmentHeader::Parse(packet->GetData(), fragment_header) == false)
		{
			logte("%s Could not re-read the access unit after patching the SPS. track(%u)",
				  _log_prefix.CStr(), packet->GetTrackId());
			return false;
		}
	}

	if (patched == true)
	{
		// The cached context came from the unpatched SPS
		_sps_context.pic_struct_present = true;
		_patched_sps					 = true;
	}

	return true;
}

bool H264SeiInserter::Stamp(const std::shared_ptr<info::Stream> &stream,
							const std::shared_ptr<const MediaTrack> &track,
							const std::shared_ptr<MediaPacket> &packet,
							const FragmentationHeader *fragment)
{
	if (fragment == nullptr)
	{
		logte("%s Failed to get NALU fragment header. track(%u)",
			  _log_prefix.CStr(), packet->GetTrackId());
		return false;
	}

	auto placement = DecideSeiPlacement(packet->GetTrackId(), packet, fragment);
	if (placement.action == SeiPlacement::Action::Skip)
	{
		return false;
	}

	const auto &sps_context = _sps_context;

	// SMPTE counts in whole frames per second, so under 1 fps there is no counting rate
	const double fps = track->GetFrameRate();
	if (fps < 1.0)
	{
		if (_warned_no_frame_rate == false)
		{
			_warned_no_frame_rate = true;
			logtw("%s Could not stamp pic_timing: the frame rate is %.3f, so n_frames cannot be derived. track(%u)",
				  _log_prefix.CStr(), fps, packet->GetTrackId());
		}

		return false;
	}

	H264SeiClockTimestamp timestamp;
	if (_generator.Generate(packet->GetPts(), track->GetTimeBase().GetExpr(), fps, timestamp) == false)
	{
		return false;
	}
	timestamp.time_offset_length = sps_context.time_offset_length;

	uint8_t pic_struct = 0;
	DetectPictureStructure(packet->GetData()->GetDataAs<uint8_t>(), fragment, sps_context,
						   pic_struct, timestamp.ct_type);

	H264SeiPicTiming pic_timing;

	if (placement.action == SeiPlacement::Action::Replace)
	{
		// The message has to be read the way the encoder wrote it. PatchSps() may have turned
		// pic_struct_present_flag on since then, and reading a message that has no pic_struct as
		// if it had one walks off the end of the payload.
		auto encoder_context				 = sps_context;
		encoder_context.pic_struct_present	 = (_patched_sps == false) && sps_context.pic_struct_present;

		const auto &existing = placement.messages[placement.pic_timing_index].payload;
		if (H264SEI::ParsePicTiming(existing, encoder_context, pic_timing) == false)
		{
			logte("%s Failed to parse the existing pic_timing. track(%u)",
				  _log_prefix.CStr(), packet->GetTrackId());
			return false;
		}

		// The encoder's cpb/dpb delays stay; the timecode is ours, and so is the picture structure
		// when the encoder never wrote one
		pic_timing.clock_timestamps.clear();
		if (encoder_context.pic_struct_present == false)
		{
			pic_timing.pic_struct = pic_struct;
		}
	}
	else
	{
		pic_timing.pic_struct = pic_struct;
	}

	pic_timing.clock_timestamps.push_back(timestamp);

	auto payload = H264SEI::SerializePicTiming(pic_timing, sps_context);
	if (payload == nullptr)
	{
		logte("%s Failed to serialize pic_timing", _log_prefix.CStr());
		return false;
	}

	std::shared_ptr<ov::Data> rbsp = nullptr;
	if (placement.action == SeiPlacement::Action::Replace)
	{
		// Re-emit the other messages of this SEI NAL verbatim
		auto messages								 = placement.messages;
		messages[placement.pic_timing_index].payload = payload;
		rbsp										 = H264SEI::JoinMessages(messages);
	}
	else
	{
		H264SEI::Message message;
		message.payload_type = static_cast<uint32_t>(H264SEI::PayloadType::PICTURE_TIMING);
		message.payload		 = payload;

		rbsp = H264SEI::JoinMessages({message});
	}

	if (rbsp == nullptr)
	{
		logte("%s Failed to build the pic_timing SEI", _log_prefix.CStr());
		return false;
	}

	const bool replace = (placement.action == SeiPlacement::Action::Replace);
	if (EmitSeiNal(stream, packet, fragment, rbsp, replace, placement.nal_index) == false)
	{
		return false;
	}

	// Runs on every picture: only the first stamp is logged above trace
	if (_logged_every_frame == false)
	{
		_logged_every_frame = true;
		logti("%s %s pic_timing at NAL %zu. track(%u), timecode(%s), fps(%.3f), payload(%zu bytes), sps(%s)",
			  _log_prefix.CStr(), replace ? "Replaced" : "Inserted",
			  placement.nal_index, packet->GetTrackId(),
			  timestamp.GetTimecodeString().CStr(), fps,
			  payload->GetLength(), sps_context.GetInfoString().CStr());
	}
	else
	{
		logtt("%s %s pic_timing at NAL %zu. track(%u), timecode(%s)",
			  _log_prefix.CStr(), replace ? "Replaced" : "Inserted",
			  placement.nal_index, packet->GetTrackId(),
			  timestamp.GetTimecodeString().CStr());
	}

	return true;
}

H264SeiSpsContext H264SeiInserter::MakeSpsContext(const H264SPS &sps)
{
	H264SeiSpsContext context;

	context.cpb_dpb_delays_present	 = sps.IsCpbDpbDelaysPresent();
	context.cpb_removal_delay_length = sps.GetCpbRemovalDelayLength();
	context.dpb_output_delay_length	 = sps.GetDpbOutputDelayLength();
	context.time_offset_length		 = sps.GetTimeOffsetLength();
	context.pic_struct_present		 = sps.IsPicStructPresent();

	context.frame_mbs_only		  = sps.IsFrameMbsOnly();
	context.log2_max_frame_num	  = sps.GetLog2MaxFrameNum();
	context.separate_colour_plane = sps.IsSeparateColourPlane();

	return context;
}

void H264SeiInserter::DetectPictureStructure(const uint8_t *buffer,
											 const FragmentationHeader *fragment,
											 const H264SeiSpsContext &sps_context,
											 uint8_t &pic_struct,
											 uint8_t &ct_type)
{
	// frame_mbs_only_flag = 1 forbids fields, so every picture is a progressive frame
	if (sps_context.frame_mbs_only == true)
	{
		pic_struct = 0;
		ct_type	   = 0;
		return;
	}

	// Everything else is decided by the slice header; until it is read the structure is unknown
	pic_struct = 0;
	ct_type	   = 2;

	auto vcl_index = FindFirstVclIndex(buffer, fragment);
	if (vcl_index.has_value() == false)
	{
		return;
	}

	auto nal = fragment->GetFragment(vcl_index.value());
	if (nal.has_value() == false)
	{
		return;
	}

	// slice_header() up to field_pic_flag (ITU-T H.264 7.3.3). The parser skips emulation
	// prevention bytes, so the NAL is handed over as it stands.
	NalUnitBitstreamParser parser(buffer + std::get<0>(nal.value()), std::get<1>(nal.value()));

	H264NalUnitHeader nal_header;
	if (H264Parser::ParseNalUnitHeader(parser, nal_header) == false)
	{
		return;
	}

	// Read only to walk past them: field_pic_flag is what this is after
	uint32_t first_mb_in_slice = 0, slice_type = 0, pic_parameter_set_id = 0;
	if ((parser.ReadUEV(first_mb_in_slice) == false) ||
		(parser.ReadUEV(slice_type) == false) ||
		(parser.ReadUEV(pic_parameter_set_id) == false))
	{
		return;
	}

	if (sps_context.separate_colour_plane == true)
	{
		uint8_t colour_plane_id = 0;
		if (parser.ReadBits(2, colour_plane_id) == false)
		{
			return;
		}
	}

	uint32_t frame_num = 0;
	if (parser.ReadBits(sps_context.log2_max_frame_num, frame_num) == false)
	{
		return;
	}

	uint8_t field_pic_flag = 0;
	if (parser.ReadBit(field_pic_flag) == false)
	{
		return;
	}

	if (field_pic_flag == 0)
	{
		// A coded frame in a stream that allows fields. Nothing here says whether the content is
		// progressive or two interleaved fields, so the structure stays unknown.
		return;
	}

	uint8_t bottom_field_flag = 0;
	if (parser.ReadBit(bottom_field_flag) == false)
	{
		return;
	}

	// ITU-T H.264 Table D-1: 1 = top field, 2 = bottom field
	pic_struct = (bottom_field_flag != 0) ? 2 : 1;
	ct_type	   = 1;	 // interlaced
}
