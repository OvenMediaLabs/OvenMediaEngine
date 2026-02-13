//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "base/info/stream.h"
#include "base/info/codec.h"
#include "codec/codec_base.h"

class TranscodeEncoder : public TranscodeBase<MediaFrame, MediaPacket>
{
public:
	typedef std::function<void(TranscodeResult, int32_t, std::shared_ptr<MediaPacket>)> CompleteHandler;
	static std::shared_ptr<std::vector<std::shared_ptr<info::CodecCandidate>>> GetCandidates(bool hwaccels_enable, ov::String hwaccles_modules, std::shared_ptr<MediaTrack> track);
	static std::shared_ptr<TranscodeEncoder> Create(int32_t encoder_id, std::shared_ptr<info::Stream> info, std::shared_ptr<MediaTrack> output_track, std::shared_ptr<std::vector<std::shared_ptr<info::CodecCandidate>>> candidates, CompleteHandler complete_handler);

public:
	TranscodeEncoder(info::Stream stream_info);
	~TranscodeEncoder() override;

	void SetEncoderId(int32_t encoder_id);
	void SetCompleteHandler(CompleteHandler complete_handler);
	void Complete(TranscodeResult result, std::shared_ptr<MediaPacket> packet);
	std::shared_ptr<MediaTrack> &GetRefTrack();
	cmn::Timebase GetTimebase() const;

	virtual cmn::AudioSample::Format GetSupportAudioFormat() const noexcept = 0;
	virtual cmn::VideoPixelFormatId GetSupportVideoFormat() const noexcept = 0;
	virtual cmn::BitstreamFormat GetBitstreamFormat() const noexcept = 0;

	bool InitCodecInteral();
	virtual bool InitCodec() = 0;
	virtual void DeinitCodec();
	virtual bool SetCodecParams() = 0;
	virtual void CodecThread();

	virtual void Stop();

	bool PushProcess(std::shared_ptr<const MediaFrame> media_frame);
	bool PopProcess();

	virtual void Flush();

	virtual bool Configure(std::shared_ptr<MediaTrack> output_track) override;
	bool Configure(std::shared_ptr<MediaTrack> output_track, size_t max_queue_size);

	void SendBuffer(std::shared_ptr<const MediaFrame> media_frame) override;

protected:
	int32_t _encoder_id = -1;

	info::Stream _stream_info;
	std::shared_ptr<MediaTrack> _track = nullptr;

	ov::Future _codec_init_event;

	cmn::BitstreamFormat _bitstream_format = cmn::BitstreamFormat::Unknown;
	cmn::PacketType _packet_type = cmn::PacketType::Unknown;

	std::atomic<bool> _kill_flag{false};
	std::thread _codec_thread;

	CompleteHandler _complete_handler;

	// Force Keyframce
	ov::PreciseTimer _force_keyframe_timer;
	// 0: no force keyframe,  > 0: force keyframe by sum of duration
	int64_t _force_keyframe_by_time_interval;
	// -1: force keyframe
	int64_t _accumulate_frame_duration;

	AVCodecContext *_codec_context = nullptr;
	AVPacket *_packet = nullptr;
	AVFrame *_frame = nullptr;
};
