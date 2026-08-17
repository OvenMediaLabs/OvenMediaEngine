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

struct DecodeResult
{
	// The result carries its own "no output" state, so no separate flag is needed:
	//   Again         			: no output this step — Complete() is not called
	//                   		  (more input needed, packet dropped, codec returned EAGAIN, no frame yet)
	//   NoData        			: invalid input data; optionally carries an empty placeholder frame
	//   DataError     			: a decode error occurred
	//   DataReadyFormatChanged : a frame was decoded
	TranscodeResult result = TranscodeResult::Again;
	std::shared_ptr<MediaFrame> frame = nullptr;
	ov::String error = "";

	static DecodeResult NoOutput()
	{
		return {TranscodeResult::Again, nullptr};
	}

	static DecodeResult Decoded(std::shared_ptr<MediaFrame> frame, bool format_changed)
	{
		return {format_changed ? TranscodeResult::FormatChanged : TranscodeResult::DataReady, std::move(frame)};
	}

	static DecodeResult InvalidData(std::shared_ptr<MediaFrame> frame = nullptr)
	{
		return {TranscodeResult::NoData, std::move(frame)};
	}

	// A decode error occurred; no frame.
	static DecodeResult Error(ov::String error = "")
	{
		return {TranscodeResult::DataError, nullptr, error};
	}
};

class TranscodeDecoder : public TranscodeBase<MediaPacket, MediaFrame>
{
public:
	static std::shared_ptr<std::vector<std::shared_ptr<info::CodecCandidate>>> GetCandidates(bool hwaccels_enable, ov::String hwaccles_moduels, std::shared_ptr<MediaTrack> track);

	typedef std::function<void(TranscodeResult, int32_t, std::shared_ptr<MediaFrame>)> CompleteHandler;
	
	static std::shared_ptr<TranscodeDecoder> Create(
		int32_t decoder_id,
		std::shared_ptr<info::Stream> info,
		std::shared_ptr<MediaTrack> track,
		std::shared_ptr<std::vector<std::shared_ptr<info::CodecCandidate>>> candidates,
		CompleteHandler complete_handler);

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

	// Implemented by each per-codec decoder.
	virtual bool Initialize() = 0;
	virtual void Stop();

protected:
	void ThreadLoop();

	virtual std::shared_ptr<MediaPacket> GetFramedPacket() { return nullptr; }
	virtual DecodeResult SendPacket(const std::shared_ptr<MediaPacket> &packet) { (void)packet; return DecodeResult::NoOutput(); }
	virtual DecodeResult ReceiveFrame() { return DecodeResult::NoOutput(); }
	virtual void Uninitialize() {}

	int32_t _decoder_id = -1;

	info::Stream _stream_info;
	std::shared_ptr<MediaTrack> _track;
	CompleteHandler _complete_handler;

	ov::Future _codec_init_event;

	std::atomic<bool> _kill_flag{false};
	std::thread _codec_thread;
};
