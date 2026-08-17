//==============================================================================
//
//  Whisper encoder for speech recognition and subtitle generation.
//
//  Created by Getroot
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include <orchestrator/orchestrator.h>
#include <base/modules/data_format/webvtt/webvtt_frame.h>
#include <base/event/command/commands.h>

#include "encoder_whisper.h"
#include "../../transcoder_private.h"
#include "../../transcoder_whisper_model_registry.h"
#include "../../transcoder_gpu.h"

EncoderWhisper::EncoderWhisper(const info::Stream &stream_info)
	: TranscodeEncoder(stream_info)
{
}

EncoderWhisper::~EncoderWhisper()
{
}

bool EncoderWhisper::SetCodecParams()
{
	return true;
}

bool EncoderWhisper::Configure(std::shared_ptr<MediaTrack> context)
{
#ifdef HWACCELS_NVIDIA_ENABLED
	if (TranscodeGPU::GetInstance()->GetDeviceCount(cmn::MediaCodecModuleId::NVENC) == 0)
	{
		logtw("Whisper STT requires an NVIDIA GPU, but no NVIDIA device is available. STT track will be disabled.");
		return false;
	}
#else
	logtw("Whisper STT requires an NVIDIA GPU, but this build does not include NVIDIA support. STT track will be disabled.");
	return false;
#endif

	auto parent_stream_info = _stream_info.GetLinkedInputStream();
	if (parent_stream_info == nullptr)
	{
		logte("Whisper encoder requires an input stream.");
		return false;
	}

	_parent_stream = ocst::Orchestrator::GetInstance()->GetProviderStream(parent_stream_info);
	if (_parent_stream == nullptr)
	{
		logte("Whisper encoder requires an input stream. (%s/%s)", parent_stream_info->GetApplicationName(), parent_stream_info->GetName().CStr());
		return false;
	}

	_step_ms = context->GetStepMs();
	_length_ms = context->GetLengthMs();
	_keep_ms = context->GetKeepMs();

	_n_samples_step = (static_cast<double>(_step_ms) / 1000.0) * WHISPER_SAMPLE_RATE;
	_n_samples_length = (static_cast<double>(_length_ms) / 1000.0) * WHISPER_SAMPLE_RATE;
	_n_samples_keep = (static_cast<double>(_keep_ms) / 1000.0) * WHISPER_SAMPLE_RATE;
	
	_source_language = context->GetSourceLanguage();
	_translate = context->ShouldTranslate();
	_output_track_label = context->GetOutputTrackLabel();

	// Initialize muted state from config (can be changed at runtime via API).
	_audio_muted.store(!context->IsSttEnabled(), std::memory_order_relaxed);

	// The base Configure starts the encoding thread (ThreadLoop) and waits for init.
	return TranscodeEncoder::Configure(context, _n_samples_step);
}

// Called by CodecThread
bool EncoderWhisper::Initialize()
{
	// check sample rate and channel
	if (_track->GetSampleRate() != WHISPER_SAMPLE_RATE)
	{
		logte("Whisper encoder only supports 16kHz sample rate. sample_rate=%d", _track->GetSampleRate());
		return false;
	}

	if (_track->GetChannel().GetCounts() != 1)
	{
		logte("Whisper encoder only supports mono channel. channels=%d", _track->GetChannel().GetCounts());
		return false;
	}

	// Resolve and cache the CUDA device index for this encoder instance.
	// _track->GetCodecDeviceId() returns the OME device index (from <Modules>nv:N</Modules>).
	// TranscodeGPU maps it to the actual CUDA device index.
	_cuda_id = TranscodeGPU::GetInstance()->GetExternalDeviceId(cmn::MediaCodecModuleId::NVENC, _track->GetCodecDeviceId());
	if (_cuda_id < 0)
	{
		_cuda_id = 0;
	}

	// Acquire the shared model context from the registry. The model stays loaded for
	// the encoder's lifetime; only the per-instance whisper_state is allocated dynamically.
	_whisper_ctx = WhisperModelRegistry::GetInstance()->GetModelContext(_track->GetModel(), _cuda_id);
	if (_whisper_ctx == nullptr)
	{
		// Error already logged by the registry.
		return false;
	}

	// If STT is enabled in the configuration (<STT><Enable>true</Enable>), allocate the
	// per-instance whisper_state eagerly so the encoder is ready to transcribe the moment
	// the stream starts. If it starts disabled, the state is allocated later when enabled
	// via the API (see CodecThread / AllocWhisperState / FreeWhisperState).
	if (_audio_muted.load(std::memory_order_relaxed) == false)
	{
		if (AllocWhisperState() == false)
		{
			_whisper_ctx.reset();
			return false;
		}
	}

	return true;
}

// Allocates the per-instance whisper_state via the registry.
// The registry serializes allocation and pre-checks GPU memory to prevent a ggml crash.
bool EncoderWhisper::AllocWhisperState()
{
	if (_whisper_state != nullptr)
	{
		return true;
	}

	_whisper_state = WhisperModelRegistry::GetInstance()->NewState(_track->GetModel(), _cuda_id);
	if (_whisper_state == nullptr)
	{
		logte("Failed to create whisper state. stream=%s, track_id=%d, label=%s, model=%s",
			  _stream_info.GetName().CStr(), _track->GetId(), _output_track_label.CStr(), _track->GetModel().CStr());
		return false;
	}

	logti("Whisper state created. stream=%s, track_id=%d, label=%s, model=%s",
		  _stream_info.GetName().CStr(), _track->GetId(), _output_track_label.CStr(), _track->GetModel().CStr());

	return true;
}

// Releases the per-instance whisper_state and returns its GPU memory to the device.
void EncoderWhisper::FreeWhisperState()
{
	if (_whisper_state != nullptr)
	{
		WhisperModelRegistry::GetInstance()->DeleteState(_whisper_state);
		_whisper_state = nullptr;

		logti("Whisper state released. stream=%s, track_id=%d, label=%s",
			  _stream_info.GetName().CStr(), _track->GetId(), _output_track_label.CStr());
	}
}

void EncoderWhisper::ThreadLoop()
{
	ov::logger::ThreadHelper thread_helper;

	// Initialize the codec and notify the main thread.
	if (_codec_init_event.Submit(Initialize()) == false)
	{
		return;
	}

	int32_t n_samples_30s = (static_cast<double>(30000.0) / 1000.0) * WHISPER_SAMPLE_RATE;

	// PCM float32 buffer for 30 seconds
	std::vector<float> pcmf32_buffer;
	std::vector<float> pcmf32_buffer_old;
	std::vector<float> pcmf32_buffer_new;

	pcmf32_buffer.reserve(n_samples_30s);
	pcmf32_buffer_old.reserve(n_samples_30s);
	pcmf32_buffer_new.reserve(n_samples_30s);

	std::vector<whisper_token> prompt_tokens;

	int64_t new_buffer_start_cs = 0;
	int64_t new_buffer_end_cs = 0;
	int64_t last_commit_end_cs = 0;

	int32_t n_iter = 0;
	int32_t n_new_lines = std::max(1, (_length_ms / _step_ms) - 1);
	while (!_kill_flag)
	{
		// Collect audio samples until n_samples_step is reached.
		new_buffer_start_cs = 0;
		new_buffer_end_cs = 0;
		pcmf32_buffer_new.clear();
		while (!_kill_flag)
		{
			// React to STT enable/disable transitions before waiting for audio.
			// Pause()/Resume() flip _audio_muted and call _input_buffer.InjectWakeup(),
			// which wakes the Dequeue() below so this runs promptly even when no
			// audio is flowing.
			if (_audio_muted.load(std::memory_order_relaxed))
			{
				// Disabled: release the per-instance whisper_state to free GPU
				// memory and discard the rolling-window so a later resume is clean.
				if (_whisper_state != nullptr)
				{
					FreeWhisperState();

					pcmf32_buffer_old.clear();
					pcmf32_buffer_new.clear();
					prompt_tokens.clear();
					new_buffer_start_cs = 0;
					new_buffer_end_cs = 0;
					last_commit_end_cs = 0;
					n_iter = 0;
				}
			}
			else if (_whisper_state == nullptr)
			{
				// Enabled but no state yet: lazily (re)allocate it. Allocation can
				// fail under GPU memory pressure, so retries are throttled.
				auto now = std::chrono::steady_clock::now();
				if (now - _last_state_alloc_fail_ts >= std::chrono::seconds(5))
				{
					if (AllocWhisperState() == false)
					{
						_last_state_alloc_fail_ts = now;
					}
				}
			}

			auto obj = _input_buffer.Dequeue();
			if (obj.has_value() == false)
			{
				// Woken by InjectWakeup() (state transition) or stopped; re-evaluate.
				continue;
			}

			// Drop the frame without inference while muted, or until the
			// whisper_state has been (re)allocated after a failed attempt.
			if (_audio_muted.load(std::memory_order_relaxed) || _whisper_state == nullptr)
			{
				continue;
			}

			auto media_frame = std::move(obj.value());
			OV_ASSERT2(media_frame != nullptr);

			int64_t start_ts = media_frame->GetPts();
			int64_t end_ts = start_ts + media_frame->GetDuration();

			start_ts = ov::Converter::Rescale(start_ts, 100, _track->GetTimeBase().GetDen());
			end_ts = ov::Converter::Rescale(end_ts, 100, _track->GetTimeBase().GetDen());

			auto av_frame = static_cast<AVFrame *>(media_frame->GetPrivData());
			if (!av_frame)
			{
				logte("Could not allocate the frame data");
				break;
			}

			auto format = media_frame->GetFormat<cmn::AudioSample::Format>();
			auto channel_count = media_frame->GetChannelCount();

			if (format != cmn::AudioSample::Format::Flt || channel_count != 1)
			{
				logtw("Unsupported audio format for Whisper encoder. Only mono Flt is supported. pts=%" PRId64 ", format=%s, channels=%d, sample_rate=%d, nb_samples=%d",
					media_frame->GetPts(),
					format == cmn::AudioSample::Format::None ? "none" : format == cmn::AudioSample::Format::S16 ? "s16" : "other",
					channel_count,
					media_frame->GetSampleRate(),
					media_frame->GetNbSamples());
				continue;
			}

			// Mono float32
			const float* pcmf32_data = reinterpret_cast<const float*>(av_frame->data[0]);
			const int nb_samples = av_frame->nb_samples;

			// Append the new samples to the buffer.
			if (pcmf32_data != nullptr && nb_samples > 0)
			{
				pcmf32_buffer_new.insert(pcmf32_buffer_new.end(), pcmf32_data, pcmf32_data + nb_samples);
			}

			if (new_buffer_start_cs == 0 || start_ts < new_buffer_start_cs)
			{
				new_buffer_start_cs = start_ts;
			}

			if (end_ts > new_buffer_end_cs)
			{
				new_buffer_end_cs = end_ts;
			}

			// If we have enough samples, break the loop.
			if (pcmf32_buffer_new.size() >= static_cast<size_t>(_n_samples_step))
			{
				logtt("Collected %zu samples for Whisper processing. pts=%" PRId64 "", pcmf32_buffer_new.size(), media_frame->GetPts());
				break;
			}
		}

		// The inner loop also exits on _kill_flag (Stop). Skip inference then;
		// with STT disabled _whisper_state is null and would be dereferenced below.
		if (_kill_flag || _audio_muted.load(std::memory_order_relaxed) || _whisper_state == nullptr)
		{
			continue;
		}

		const int n_samples_new = static_cast<int>(pcmf32_buffer_new.size());
		const int n_samples_old_keep = std::min(static_cast<int>(pcmf32_buffer_old.size()), std::max(0, _n_samples_keep + _n_samples_length - n_samples_new));

		pcmf32_buffer.resize(n_samples_new + n_samples_old_keep);

		// Copy old samples to the beginning of the buffer.
		for (int i = 0; i < n_samples_old_keep; i++)
		{
			pcmf32_buffer[i] = pcmf32_buffer_old[pcmf32_buffer_old.size() - n_samples_old_keep + i];
		}

		logtt("Processing Whisper with %d old samples and %d new samples (total %zu samples)", n_samples_old_keep, n_samples_new, pcmf32_buffer.size());

		// Copy new samples to the buffer.
		memcpy(pcmf32_buffer.data() + n_samples_old_keep, pcmf32_buffer_new.data(), n_samples_new * sizeof(float));
		pcmf32_buffer_old = pcmf32_buffer;

		int64_t buffer_start_cs = new_buffer_start_cs - (static_cast<double>(n_samples_old_keep) / WHISPER_SAMPLE_RATE * 100);
		int64_t buffer_end_cs = new_buffer_end_cs;


		// Auto detect language if needed.
		if (_source_language == "auto" && _translate == false)
		{
			if (whisper_pcm_to_mel_with_state(_whisper_ctx.get(), _whisper_state, pcmf32_buffer.data(), pcmf32_buffer.size(), 4) != 0)
			{
				logte("Failed to process audio samples for language detection with Whisper");
				continue;
			}

			std::vector<float> probs(whisper_lang_max_id() + 1, 0.0f);
			const auto lang_id = whisper_lang_auto_detect_with_state(_whisper_ctx.get(), _whisper_state, 0, 4, probs.data());
			if (lang_id < 0)
			{
				logte("Failed to detect language with Whisper");
				continue;
			}

			auto lang_str = whisper_lang_str(lang_id);
			auto lang_prob = probs[lang_id];

			if (lang_prob > 0.9f)
			{
				// _source_language = lang_str;
				logti("Set source language [label : %s] to %s with high confidence (id=%d) with probabilities:[%f]", _track->GetOutputTrackLabel().CStr(), lang_str, lang_id, lang_prob);

				SendLangDetectionEvent(_track->GetOutputTrackLabel(), lang_str);
			}
			else
			{
				logtw("Detected language [label : %s] is not confident enough. Detected %s (id=%d) with probabilities:[%f]. Keep auto-detection. Please consider setting source_language manually.", _track->GetOutputTrackLabel().CStr(), lang_str, lang_id, lang_prob);
			}
		}
		else if (_source_language == "auto" && _translate == true)
		{
			SendLangDetectionEvent(_track->GetOutputTrackLabel(), "en");
			logti("Translation enabled. Set source language [label : %s] to English for Whisper processing.", _track->GetOutputTrackLabel().CStr());
		}

		// Size the encoder audio context to the actual window instead of the full
		// 30 s, so the encoder does not run over the zero-padding. Each context
		// position spans 320 samples (20 ms); a margin avoids truncating the
		// window tail. audio_ctx 0 means the full default context.
		int32_t audio_ctx = static_cast<int32_t>((pcmf32_buffer.size() + 319) / 320) + 64;
		if (audio_ctx >= 1500)
		{
			audio_ctx = 0;
		}

		whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
		wparams.print_progress   = false;
		wparams.print_special    = false;
		wparams.print_realtime   = false;
		wparams.print_timestamps = true;
		wparams.translate        = _translate;
		wparams.single_segment   = false;
		wparams.max_tokens       = 0; 
		wparams.language         = _translate ? "en" : _source_language.CStr();
		wparams.n_threads        = std::min(4, (int32_t) std::thread::hardware_concurrency());
		wparams.beam_search.beam_size = -1; // disable beam search
		wparams.greedy.best_of = 1; // disable best_of
		wparams.temperature_inc = 0.0f;
		wparams.audio_ctx        = audio_ctx;
		wparams.tdrz_enable      = false;
		wparams.prompt_tokens    = prompt_tokens.data();
		wparams.prompt_n_tokens  = static_cast<int>(prompt_tokens.size());
		wparams.token_timestamps = false;
		wparams.split_on_word	 = true;
		wparams.thold_pt		 = 0.01f;
		wparams.thold_ptsum		 = 0.01f;
		wparams.max_len			 = 0;

		logtt("Starting Whisper processing with %d samples", static_cast<int>(pcmf32_buffer.size()));
		logtt("Audio buffer time range for Whisper: %" PRId64 " ~ %" PRId64 " (last_commit_end_cs=%" PRId64 ")",
			buffer_start_cs, buffer_end_cs, last_commit_end_cs);
		if (whisper_full_with_state(_whisper_ctx.get(), _whisper_state, wparams, pcmf32_buffer.data(), pcmf32_buffer.size()) != 0)
		{
			logte("Failed to process audio samples with Whisper");
			continue;
		}
		logtt("Whisper processing completed");

		ov::String result_text;
		
		const int n_segments = whisper_full_n_segments_from_state(_whisper_state);
		for (int i = 0; i < n_segments; ++i) 
		{
			const char *text = whisper_full_get_segment_text_from_state(_whisper_state, i);

			// How to output subtitles without overlapping : but the quality is low - commented out for now.
			// int64_t t0 = whisper_full_get_segment_t0(_whisper_ctx, i);
			// int64_t t1 = whisper_full_get_segment_t1(_whisper_ctx, i);

			// t0 += buffer_start_cs;
			// t1 += buffer_start_cs;

			// // sometimes, t1 is larger than the audio buffer end time due to bug?
			// if (t1 > buffer_end_cs)
			// {
			// 	t1 = buffer_end_cs;
			// }

			// logti("[%d] t0 : %" PRId64 ", t1 : %" PRId64 ", last_commit_end_cs : %" PRId64 ", text : %s", i, t0, t1, last_commit_end_cs, text);

			// if (t1 <= last_commit_end_cs)
			// {
			// 	// This segment has already been committed.
			// 	logtw("Ignoring Segment: t0=%" PRId64 ", t1=%" PRId64 ", last_commit_end_cs=%" PRId64 "", t0, t1, last_commit_end_cs);
			// 	continue;
			// }
			// else if (t0 < last_commit_end_cs)
			// {
			// 	// token
			// 	int nt = whisper_full_n_tokens(_whisper_ctx, i);
			// 	for (int it = 0; it < nt; ++it)
			// 	{
			// 		auto td = whisper_full_get_token_data(_whisper_ctx, i, it);
			// 		int64_t tt0 = td.t0 + buffer_start_cs;
			// 		int64_t tt1 = td.t1 + buffer_start_cs;

			// 		if (tt0 < 0 || tt1 < 0 || tt1 <= tt0)
			// 		{
			// 			continue;
			// 		}
			// 		if (tt1 <= last_commit_end_cs)
			// 		{
			// 			// This token has already been committed.
			// 			//logtw("Ignoring Token: t0=%" PRId64 ", t1=%" PRId64 ", last_commit_end_cs=%" PRId64 "", tt0, tt1, last_commit_end_cs);
			// 			continue;
			// 		}
			// 		else 
			// 		{
			// 			auto token_text = whisper_token_to_str(_whisper_ctx, td.id);
			// 			if (token_text && *token_text)
			// 			{
			// 				logti("\t\tToken Added: t0=%" PRId64 ", t1=%" PRId64 ", last_commit_end_cs=%" PRId64 ", text=%s", tt0, tt1, last_commit_end_cs, token_text);
			// 				result_text.Append(token_text);
			// 			}
			// 		}
			// 	}
			// }
			// else
			{
				result_text.Append(text);	
			}
		}

		if (last_commit_end_cs < buffer_end_cs)
		{
			last_commit_end_cs = buffer_end_cs;
		}

		logtt("[%" PRId64 "] : %s", last_commit_end_cs, result_text.CStr());

		SendVttToProvider(result_text);

		result_text.Clear();

		n_iter++;
		if (n_iter % n_new_lines == 0)
		{
			pcmf32_buffer_old = std::vector<float>(pcmf32_buffer.end() - _n_samples_keep, pcmf32_buffer.end());
			prompt_tokens.clear();

			const int n_segments = whisper_full_n_segments_from_state(_whisper_state);
			for (int i = 0; i < n_segments; ++i)
			{
				const int nt = whisper_full_n_tokens_from_state(_whisper_state, i);
				for (int it = 0; it < nt; ++it)
				{
					prompt_tokens.push_back(whisper_full_get_token_id_from_state(_whisper_state, i, it));
				}
			}
		}
	}

	FreeWhisperState();
	_whisper_ctx.reset();
}

bool EncoderWhisper::SendVttToProvider(const ov::String &text)
{
	if (_parent_stream == nullptr)
	{
		logte("[%s/%s] Whisper encoder requires an input stream.", _stream_info.GetApplicationName(), _stream_info.GetName().CStr());
		return false;
	}

	int64_t current_timestamp = _parent_stream->GetCurrentTimestampMs();
	int64_t start_timestamp = current_timestamp;
	int64_t end_timestamp = current_timestamp + _step_ms;
	ov::String settings = ov::String::FormatString("line:85%% size:75%%");

	auto vtt_frame = WebVTTFrame::Create(_output_track_label, start_timestamp, end_timestamp, settings, text);
	if (vtt_frame == nullptr)
	{
		logte("[%s/%s] Could not create VTT frame.", _stream_info.GetApplicationName(), _stream_info.GetName().CStr());
		return false;
	}

	if (_parent_stream->SendSubtitleFrame(_output_track_label, start_timestamp, _step_ms, cmn::BitstreamFormat::WebVTT, vtt_frame->Serialize(), true) == false)
	{
		logte("[%s/%s] Could not send VTT frame.", _stream_info.GetApplicationName(), _stream_info.GetName().CStr());
		return false;
	}

	return true;
}

bool EncoderWhisper::SendLangDetectionEvent(const ov::String &label, const ov::String &language)
{
	if (_parent_stream == nullptr)
	{
		logte("[%s/%s] Whisper encoder requires an input stream.", _stream_info.GetApplicationName(), _stream_info.GetName().CStr());
		return false;
	}

	if (_source_language == language)
	{
		// No change
		return true;
	}

	logti("Detected subtitle language: %s -> %s", _source_language.CStr(), language.CStr());

	_source_language = language;

	auto event = MediaEvent::BuildEvent(EventCommandUpdateLanguage::Create(label, language));
	event->SetHighPriority(true);
	if (_parent_stream->SendEvent(event) == false)
	{
		logte("[%s/%s] Could not send language detection event.", _stream_info.GetApplicationName(), _stream_info.GetName().CStr());
		return false;
	}

	return true;
}

ov::String EncoderWhisper::ToTimeString(int64_t ten_ms)
{
	int64_t total_seconds = ten_ms / 100;
	int64_t hours = total_seconds / 3600;
	int64_t minutes = (total_seconds % 3600) / 60;
	int64_t seconds = total_seconds % 60;
	int64_t milliseconds = (ten_ms % 100) * 10;

	return ov::String::FormatString("%02" PRId64 ":%02" PRId64 ":%02" PRId64 ".%03" PRId64 "", hours, minutes, seconds, milliseconds);
}

