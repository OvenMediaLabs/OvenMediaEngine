//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2026 OvenMedia Labs. All rights reserved.
//
//==============================================================================

#pragma once

#include <base/info/stream.h>
#include <base/mediarouter/media_buffer.h>

#include <vector>

#include <modules/bitstream/nalu/nal_unit_fragment_header.h>

#include "h264_parser.h"
#include "h264_sei.h"

// Turns a PTS into a SMPTE timecode. The anchor is taken once and every later value comes off the
// PTS; reading the wall clock per picture would pick up jitter and could go backwards.
class H264TimecodeGenerator
{
public:
	// True for the 1000/1001 rates (29.97, 59.94), where SMPTE drop frame counting is what keeps
	// the timecode aligned with elapsed time. Derived from the rate, never configured.
	static bool IsDropFrameRate(double fps);

	// Frame count -> hours/minutes/seconds/frames. Drop frame skips numbers at minute boundaries.
	static void FrameNumberToTimecode(int64_t frame_number, int32_t nominal_fps, bool drop_frame,
									  uint8_t &hours, uint8_t &minutes, uint8_t &seconds, uint16_t &frames);

	// Drops the anchor. The next picture becomes the new anchor and is flagged as discontinuous.
	void Reset();

	// Fills in hours/minutes/seconds/n_frames for this picture. `pts` is in the track's time base.
	// False when fps is unknown, in which case n_frames cannot be derived. Every field the caller
	// already set, ct_type included, is left alone.
	bool Generate(int64_t pts, double timebase_expr, double fps, H264SeiClockTimestamp &timestamp);

private:
	// Seconds elapsed since midnight, local time
	static double GetTimeOfDaySeconds();

	bool _anchored		 = false;
	int64_t _anchor_pts	 = 0;
	double _anchor_value = 0.0;
	// Set on the first picture and whenever the PTS is not continuous with the previous one
	bool _discontinuity = true;
	int64_t _last_pts	= 0;
	// Used to notice that drop frame counting skipped one or more numbers
	bool _has_last_frame_number	= false;
	int64_t _last_frame_number	= 0;
};


// Writes SEI messages into an H.264 access unit. pic_timing is the only one that remembers
// anything between pictures, so an instance belongs to one track; the rest is static.
class H264SeiInserter
{
public:
	// OME Specific UUID
	// 464d4c47-5241-494e-434f-4c4f-55524201
	static constexpr uint8_t OME_USER_DATA_UUID[16] = {0x46, 0x4D, 0x4C, 0x47, 0x52, 0x41, 0x49, 0x4E, 0x43, 0x4F, 0x4C, 0x4F, 0x55, 0x52, 0x42, 0x01};

	// pic_timing (payloadType 1): a SMPTE timecode. Call on every picture of a stamping track, the
	// first one included. True when the packet was modified.
	bool InsertPicTiming(const std::shared_ptr<info::Stream> &stream,
						 const std::shared_ptr<const MediaTrack> &track,
						 const std::shared_ptr<MediaPacket> &packet);

	// user_data_unregistered (payloadType 5): a one-off message, so there is no state to keep.
	// `data` may contain the ${EpochTime} macro, expanded here to the insertion time.
	static bool InsertUserData(const std::shared_ptr<info::Stream> &stream,
							   const std::shared_ptr<const MediaTrack> &track,
							   const std::shared_ptr<MediaPacket> &packet,
							   const ov::String &data);

private:
	// Where a pic_timing SEI belongs in an access unit
	struct SeiPlacement
	{
		enum class Action
		{
			// Cannot be stamped, for example no SPS seen yet
			Skip,
			// Build one and insert it before the first VCL NAL
			Insert,
			// An access unit may carry only one pic_timing (ITU-T H.264 7.4.1.2.3)
			Replace
		};

		Action action	 = Action::Skip;
		size_t nal_index = 0;

		// Replace only: every message of the target SEI NAL, so the untouched ones
		// (buffering_period and such) can be re-emitted verbatim
		std::vector<H264SEI::Message> messages;
		size_t pic_timing_index = 0;
	};

	// True when this track and packet can carry an SEI NAL. `warned` suppresses the log after the
	// first time; nullptr logs every time.
	static bool IsInsertable(const std::shared_ptr<info::Stream> &stream,
							 const std::shared_ptr<const MediaTrack> &track,
							 const std::shared_ptr<MediaPacket> &packet,
							 bool *warned);

	// A prefix SEI must precede the primary coded picture (ITU-T H.264 7.4.1.2.3), so the first
	// VCL NAL is where one gets inserted
	static std::optional<size_t> FindFirstVclIndex(const uint8_t *buffer,
												   const FragmentationHeader *fragment);

	// Wraps the RBSP in an SEI NAL unit and writes it into the access unit
	static bool EmitSeiNal(const std::shared_ptr<info::Stream> &stream,
						   const std::shared_ptr<MediaPacket> &packet,
						   const FragmentationHeader *fragment,
						   const std::shared_ptr<ov::Data> &rbsp,
						   bool replace,
						   size_t nal_index);

	// pic_struct and ct_type of this picture, read from the SPS and the first slice header.
	// ct_type is 2 (unknown) rather than 0 when the structure cannot be determined.
	static void DetectPictureStructure(const uint8_t *buffer,
									   const FragmentationHeader *fragment,
									   const H264SeiSpsContext &sps_context,
									   uint8_t &pic_struct,
									   uint8_t &ct_type);

	// Scans the access unit, refreshes the SPS cache in `state` and returns the placement
	SeiPlacement DecideSeiPlacement(MediaTrackId track_id,
									const std::shared_ptr<MediaPacket> &packet,
									const FragmentationHeader *fragment);

	// Sets pic_struct_present_flag in every SPS of this access unit. `fragment_header` is refreshed
	// whenever the unit is rebuilt; false means it could no longer be read and is now stale.
	bool PatchSps(const std::shared_ptr<MediaPacket> &packet,
				  NalUnitFragmentHeader &fragment_header);

	bool Stamp(const std::shared_ptr<info::Stream> &stream,
			   const std::shared_ptr<const MediaTrack> &track,
			   const std::shared_ptr<MediaPacket> &packet,
			   const FragmentationHeader *fragment);

	// The SPS fields the pic_timing codec needs, read off a parsed SPS
	static H264SeiSpsContext MakeSpsContext(const H264SPS &sps);

	// [#vhost#app/stream] as the rest of MediaRouter and Transcoder print it. Set on the first
	// call, so the instance methods do not need the stream handed to them.
	ov::String _log_prefix;

	// The SPS is parsed from the in band NAL: an output track has no decoder configuration
	// record yet at this point
	bool _has_sps_context = false;
	H264SeiSpsContext _sps_context;

	// Set once an SPS of this track has been patched. Whatever the encoder wrote into its own
	// pic_timing messages predates that, so those carry no pic_struct however the SPS reads now.
	bool _patched_sps = false;

	// Set by the first InsertPicTiming(): the picture the timecode starts from
	bool _started = false;

	H264TimecodeGenerator _generator;

	// One log line per track, not per picture
	bool _warned_unsupported   = false;
	bool _warned_no_sps		   = false;
	bool _warned_no_pic_struct = false;
	bool _warned_patch_failed  = false;
	bool _warned_no_frame_rate = false;
	bool _logged_patch_applied = false;
	bool _logged_every_frame   = false;
};
