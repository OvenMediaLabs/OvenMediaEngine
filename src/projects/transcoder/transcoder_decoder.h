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

class TranscodeDecoder : public TranscodeBase<MediaPacket, MediaFrame>
{
public:
	typedef std::function<void(TranscodeResult, int32_t, std::shared_ptr<MediaFrame>)> CompleteHandler;

	static std::shared_ptr<std::vector<std::shared_ptr<info::CodecCandidate>>> GetCandidates(bool hwaccels_enable, ov::String hwaccles_moduels, std::shared_ptr<MediaTrack> track);
	static std::shared_ptr<TranscodeDecoder> Create(int32_t decoder_id, std::shared_ptr<info::Stream> info, std::shared_ptr<MediaTrack> track, std::shared_ptr<std::vector<std::shared_ptr<info::CodecCandidate>>> candidates, CompleteHandler complete_handler);

	TranscodeDecoder(info::Stream stream_info);
	~TranscodeDecoder() override;

	bool Configure(std::shared_ptr<MediaTrack> track) override;
	void SetDecoderId(int32_t decoder_id);

	std::shared_ptr<MediaTrack> &GetRefTrack();
	cmn::Timebase GetTimebase();

public:
	void SendBuffer(std::shared_ptr<const MediaPacket> packet) override;
	void SetCompleteHandler(CompleteHandler complete_handler);
	void Complete(TranscodeResult result, std::shared_ptr<MediaFrame> frame);

	virtual void CodecThread() = 0;
	virtual void Stop();

protected:
	int32_t _decoder_id = -1;

	info::Stream _stream_info;
	std::shared_ptr<MediaTrack> _track;
	CompleteHandler _complete_handler;

	ov::Future _codec_init_event;

	bool _change_format = false;

	std::atomic<bool> _kill_flag{false};
	std::thread _codec_thread;

	//TODO(Keukhan): Use MediaBuffer instead of AVPacket
	AVCodecContext *_codec_context = nullptr;
	AVCodecParserContext *_parser = nullptr;
	AVPacket *_pkt;
	AVFrame *_frame;
};
