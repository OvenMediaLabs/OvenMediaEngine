//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <map>

#include "base/info/stream.h"
#include "base/info/codec.h"
#include "codec/codec_base.h"

// Outcome of an encode step (SendFrame / ReceivePacket).
// The result enum carries both "is there output" and "keep draining", so no
// separate flags are needed:
//   Again     : encoder drained for now — stop the receive loop, nothing to report
//   NoData    : nothing to report, but keep draining (e.g. a packet was dropped)
//   DataReady : a packet was encoded; forward it and keep draining
//   DataError : a fatal error occurred; forward it (terminates the thread)
struct EncodeResult
{
	TranscodeResult result = TranscodeResult::Again;
	std::shared_ptr<MediaPacket> packet = nullptr;
	ov::String error = "";

	static EncodeResult NoOutput()
	{
		return { TranscodeResult::Again, nullptr };
	}

	// A packet was encoded; forward it and keep draining.
	static EncodeResult Encoded(std::shared_ptr<MediaPacket> packet)
	{
		return { TranscodeResult::DataReady, std::move(packet) };
	}

	// A fatal error occurred; no packet.
	static EncodeResult Error(ov::String error = "")
	{
		return { TranscodeResult::DataError, nullptr, error };
	}
};

class TranscodeEncoder : public TranscodeBase<MediaFrame, MediaPacket>
{
public:
	typedef std::function<void(TranscodeResult, int32_t, std::shared_ptr<MediaPacket>)> CompleteHandler;
	static std::shared_ptr<std::vector<std::shared_ptr<info::CodecCandidate>>> GetCandidates(bool hwaccels_enable, ov::String hwaccles_modules, std::shared_ptr<MediaTrack> track);
	// Instantiate creates the encoder object for the given codec/module without calling Configure.
	// Use this to inspect encoder properties (e.g. IsInputOnly()) before committing to full creation.
	static std::shared_ptr<TranscodeEncoder> Instantiate(cmn::MediaCodecId codec_id, cmn::MediaCodecModuleId module_id, const info::Stream &stream_info);
	static std::shared_ptr<TranscodeEncoder> Create(int32_t encoder_id, std::shared_ptr<info::Stream> info, std::shared_ptr<MediaTrack> output_track, std::shared_ptr<std::vector<std::shared_ptr<info::CodecCandidate>>> candidates, CompleteHandler complete_handler);

public:
	TranscodeEncoder(info::Stream stream_info);
	~TranscodeEncoder() override;

	void SetEncoderId(int32_t encoder_id);
	void SetCompleteHandler(CompleteHandler complete_handler);
	void Complete(TranscodeResult result, std::shared_ptr<MediaPacket> packet);
	// KeyframeGridRestore: a track change can shift the keyframe cadence (a
	// reinitialized codec session opens with an immediate keyframe). When armed,
	// one keyframe is forced where the previous cadence would have produced
	// one, then the codec continues from there. FRAME mode mirrors the codec's
	// own frame counting; TIME mode projects from the last keyframe timestamp.
	void ArmKeyframeGridRestore();
	std::shared_ptr<MediaTrack> &GetRefTrack();
	cmn::Timebase GetTimebase() const;

	virtual cmn::AudioSample::Format GetSupportAudioFormat() const noexcept = 0;
	virtual cmn::VideoPixelFormatId GetSupportVideoFormat() const noexcept = 0;
	virtual cmn::BitstreamFormat GetBitstreamFormat() const noexcept = 0;

	// Returns true if this encoder consumes input but does not produce output packets
	// back into the transcoding pipeline (e.g. STT encoders that push data forward directly).
	virtual bool IsInputOnly() const noexcept { return false; }

	struct EncoderInfo
	{
		cmn::MediaCodecId       codec_id  = cmn::MediaCodecId::None;
		cmn::MediaCodecModuleId module_id = cmn::MediaCodecModuleId::None;
		bool                    hw_accel  = false;
		// Codec-specific extended fields (key/value pairs).
		// e.g. Whisper: {"model":"small", "language":"ko", "translation":"false"}
		std::map<ov::String, ov::String> properties;
	};

	virtual EncoderInfo GetInfo() const
	{
		EncoderInfo info;
		info.codec_id  = GetCodecID();
		info.module_id = GetModuleID();
		info.hw_accel  = IsHWAccel();
		return info;
	}

	// Pause/resume output of this encoder.
	// Default is no-op; override in encoders that support pausing (e.g. EncoderWhisper).
	virtual void Pause()          {}
	virtual void Resume()         {}
	virtual bool IsPaused() const { return false; }

	virtual bool Configure(std::shared_ptr<MediaTrack> output_track) override;
	bool Configure(std::shared_ptr<MediaTrack> output_track, size_t max_queue_size);

	void SendBuffer(std::shared_ptr<const MediaFrame> media_frame) override;

	virtual void Stop();
	virtual void Flush();

protected:
	virtual void ThreadLoop();

	// Implemented by each per-backend encoder.
	virtual bool Initialize() = 0;
	virtual void Uninitialize() {}
	virtual EncodeResult SendFrame(const std::shared_ptr<const MediaFrame> &frame, bool force_keyframe)
	{
		(void)frame;
		(void)force_keyframe;
		return EncodeResult::NoOutput();
	}
	virtual EncodeResult ReceivePacket() { return EncodeResult::NoOutput(); }
	virtual bool NeedReinitForFrame(const std::shared_ptr<const MediaFrame> &frame)
	{
		(void)frame;
		return false;
	}
	bool Reinitialize();
	bool ComputeForceKeyframe(const std::shared_ptr<const MediaFrame> &frame);
	void SetupForceKeyframeByTime();
	bool ComputeKeyframeGridRestore(const std::shared_ptr<const MediaFrame> &frame);
	bool ComputeTimeModeGridRestore(const std::shared_ptr<const MediaFrame> &frame, const std::shared_ptr<MediaTrack> &track);
	bool ComputeFrameModeGridRestore(const std::shared_ptr<MediaTrack> &track);

protected:
	int32_t _encoder_id = -1;

	info::Stream _stream_info;
	std::shared_ptr<MediaTrack> _track = nullptr;

	ov::Future _codec_init_event;

	std::atomic<bool> _kill_flag{false};
	std::thread _codec_thread;

	CompleteHandler _complete_handler;

	// 0: no force keyframe,  > 0: force keyframe by sum of duration
	int64_t _force_keyframe_by_time_interval = 0;
	// -1: force keyframe
	int64_t _accumulate_frame_duration = -1;
	// Time interval from the last inserted keyframe
	int64_t _last_keyframe_delta_time = 0;

	// KeyframeGridRestore state. The last output keyframe position is tracked
	// in Complete(); the frame counter and the restore target are touched only
	// by the codec thread.
	std::atomic<int64_t> _last_keyframe_pts{-1};
	std::atomic<bool> _keyframe_grid_restore_armed{false};
	int64_t _keyframe_grid_restore_target_pts = -1;
	// FRAME mode cadence mirror. The value counts the frames of the current
	// cadence cycle including its opening keyframe, so it rests at N right
	// before the next cadence position.
	int32_t _frames_since_cadence_keyframe = 0;
};
