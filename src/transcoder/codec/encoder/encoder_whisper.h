//==============================================================================
//
//  Whisper encoder for speech recognition and subtitle generation.
//
//  Created by Getroot
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <whisper.h>

#include <chrono>
#include <memory>

#include <base/provider/stream.h>
#include "../../transcoder_encoder.h"

class EncoderWhisper : public TranscodeEncoder
{
public:
	EncoderWhisper(const info::Stream &stream_info);
	~EncoderWhisper() override;
	
	// ----- Codec info -----
	cmn::MediaCodecId GetCodecID() const noexcept override
	{
		return cmn::MediaCodecId::Whisper;
	}

	cmn::MediaCodecModuleId GetModuleID() const noexcept override
	{
		return cmn::MediaCodecModuleId::NVENC;
	}

	cmn::MediaType GetMediaType() const noexcept override
	{
		return cmn::MediaType::Audio;
	}

	bool IsHWAccel() const noexcept override
	{
		return true;
	}

	// ----- Supported formats -----
	cmn::AudioSample::Format GetSupportAudioFormat() const noexcept override
	{
		return cmn::AudioSample::Format::S16;
	}

	cmn::VideoPixelFormatId GetSupportVideoFormat() const noexcept override 
	{
		return cmn::VideoPixelFormatId::None;
	}

	cmn::BitstreamFormat GetBitstreamFormat() const noexcept override
	{
		return cmn::BitstreamFormat::WebVTT;
	}

	bool IsInputOnly() const noexcept override
	{
		return true;
	}
	
	// ----- Encoder interface -----
	bool Configure(std::shared_ptr<MediaTrack> context) override;
	bool Initialize() override;
	void ThreadLoop() override;

	// Flip the mute flag and wake the codec thread (which may be blocked in
	// _input_buffer.Dequeue()) so it acts on the transition without waiting for audio.
	void Pause()  override { _audio_muted.store(true,  std::memory_order_relaxed); _input_buffer.InjectWakeup(); }
	void Resume() override { _audio_muted.store(false, std::memory_order_relaxed); _input_buffer.InjectWakeup(); }
	bool IsPaused() const override { return _audio_muted.load(std::memory_order_relaxed); }

	EncoderInfo GetInfo() const override
	{
		EncoderInfo info = TranscodeEncoder::GetInfo();
		info.properties["label"]       = _output_track_label;
		info.properties["model"]       = _track ? _track->GetModel() : "";
		info.properties["language"]    = _source_language;
		info.properties["translation"] = _translate ? "true" : "false";
		return info;
	}

private:
	// ----- Internal helpers -----
	bool SetCodecParams();	
	ov::String ToTimeString(int64_t ten_ms);
	bool SendVttToProvider(const ov::String &text);
	bool SendLangDetectionEvent(const ov::String &label, const ov::String &language);

	// The per-instance whisper_state is allocated lazily when STT is enabled and
	// released when disabled, so STT-disabled streams hold no GPU state memory.
	bool AllocWhisperState();
	void FreeWhisperState();

	// ----- Members -----
	std::atomic<bool> _audio_muted{false};

	int32_t _step_ms = 2000;
	int32_t _length_ms = 10000;
	int32_t _keep_ms = 1500;
        // Shared model weights: owned by WhisperModelRegistry, reference-counted here.
        std::shared_ptr<whisper_context> _whisper_ctx;
        // Per-instance inference state: isolates all mutable buffers from other instances.
        // Allocated lazily on enable, released on disable (see CodecThread).
        struct whisper_state * _whisper_state = nullptr;
        // CUDA device index resolved in InitCodec, reused for lazy state allocation.
        int32_t _cuda_id = 0;
        // Throttles whisper_state allocation retries after a failure (e.g. GPU OOM).
        std::chrono::steady_clock::time_point _last_state_alloc_fail_ts;

        int32_t _n_samples_step = 0;
        int32_t _n_samples_length = 0;
        int32_t _n_samples_keep = 0;

	ov::String _source_language = "auto";
	bool _translate = false;
	ov::String _output_track_label;

	std::shared_ptr<pvd::Stream> _parent_stream = nullptr;
};
