//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#include <algorithm>
#include <sys/stat.h>

#ifdef HWACCELS_NVIDIA_ENABLED
#include <cuda_runtime.h>
#endif

#include "transcoder_whisper_model_registry.h"
#include "transcoder_private.h"

bool WhisperModelRegistry::Preload(const std::vector<ov::String> &model_paths)
{
	std::lock_guard<std::mutex> lock(_mutex);

#ifdef HWACCELS_NVIDIA_ENABLED
	int device_count = 0;
	if (cudaGetDeviceCount(&device_count) != cudaSuccess || device_count == 0)
	{
		logtw("Whisper STT requires an NVIDIA GPU, but no NVIDIA device is available. Whisper models will not be loaded.");
		return false;
	}
#else // HWACCELS_NVIDIA_ENABLED
	logtw("Whisper requires NVIDIA GPU support (rebuild with OME_HWACCEL_NVIDIA=ON). Whisper models will not be loaded.");
	return false;
#endif // HWACCELS_NVIDIA_ENABLED

	// Sort by file size descending so larger models are loaded first.
	// This maximizes GPU utilization: the biggest model claims GPU memory first,
	// and smaller models fill whatever remains.
	auto sorted_paths = model_paths;
	std::sort(sorted_paths.begin(), sorted_paths.end(), [](const ov::String &a, const ov::String &b) {
		struct stat sa{}, sb{};
		off_t size_a = (::stat(a.CStr(), &sa) == 0) ? sa.st_size : 0;
		off_t size_b = (::stat(b.CStr(), &sb) == 0) ? sb.st_size : 0;
		return size_a > size_b;  // descending
	});

	for (const auto &path : sorted_paths)
	{
		LoadModel(path);
	}

	return true;
}

// Caller must hold _mutex.
void WhisperModelRegistry::LoadModel(const ov::String &path)
{
	const std::string key = path.CStr();

	if (_models.count(key) > 0)
	{
		logtw("Whisper model already loaded, skipping duplicate. path=%s", path.CStr());
		return;
	}

	// Whisper requires a GPU for real-time live transcription.
	// CPU inference is too slow (several times slower than real-time) and is not supported.
#ifndef HWACCELS_NVIDIA_ENABLED
	logte("Whisper requires NVIDIA GPU support. Rebuild with OME_HWACCEL_NVIDIA=ON. path=%s", path.CStr());
	return;
#endif

	struct whisper_context_params cparams = whisper_context_default_params();
	cparams.flash_attn = true;

	// Check free GPU memory before calling whisper_init_from_file_with_params().
	// ggml calls abort() on CUDA OOM instead of returning an error, so we must
	// pre-flight check. Required: model file size * 2 (weights + kv cache + compute buffers).
#ifdef HWACCELS_NVIDIA_ENABLED
	{
		struct stat model_stat{};
		size_t model_file_bytes = 0;
		if (::stat(path.CStr(), &model_stat) == 0)
		{
			model_file_bytes = static_cast<size_t>(model_stat.st_size);
		}

		size_t required_bytes = model_file_bytes * 2;
		size_t free_mem = 0, total_mem = 0;
		cudaError_t cuda_err = cudaMemGetInfo(&free_mem, &total_mem);
		if (cuda_err != cudaSuccess)
		{
			logte("Failed to query GPU memory for Whisper model (CUDA: %s). path=%s", cudaGetErrorString(cuda_err), path.CStr());
			return;
		}

		if (free_mem < required_bytes)
		{
			logte("Not enough GPU memory for Whisper model (free=%.1f MiB, required≈%.1f MiB). path=%s",
				static_cast<double>(free_mem) / (1024.0 * 1024.0),
				static_cast<double>(required_bytes) / (1024.0 * 1024.0),
				path.CStr());
			return;
		}

		logti("Whisper GPU init: free=%.1f MiB, required≈%.1f MiB. path=%s",
			static_cast<double>(free_mem) / (1024.0 * 1024.0),
			static_cast<double>(required_bytes) / (1024.0 * 1024.0),
			path.CStr());
	}
#endif

	cparams.use_gpu = true;

	// libcublas lazy-initializes its global state in two phases without thread safety.
	// Phase 1: whisper_init_from_file_with_params() → cublasCreate().
	// Phase 2: first GEMM call → pthread_rwlock_init (triggered by warmup below).
	// All loads happen sequentially under _mutex, so races are prevented.
	auto raw_ctx = whisper_init_from_file_with_params(path.CStr(), cparams);
	if (raw_ctx == nullptr)
	{
		logte("Failed to load Whisper model. path=%s", path.CStr());
		return;
	}

	// Wrap in shared_ptr with whisper_free as custom deleter.
	auto ctx = std::shared_ptr<whisper_context>(raw_ctx, [](whisper_context *c) {
		whisper_free(c);
	});

	// Warmup: GPU only — force phase-2 libcublas init (first GEMM call) by running
	// a full inference pass on 1 second of silence via a temporary state.
	// Also measures GPU memory consumed by one state for later OOM prevention.
	// Warmup: force phase-2 libcublas init (first GEMM call) by running a full inference
	// pass on 1 second of silence. Also measures GPU memory consumed by one state
	// for OOM pre-checks in NewState().
#ifdef HWACCELS_NVIDIA_ENABLED
	{
		size_t free_before = 0, free_after = 0, total = 0;
		cudaMemGetInfo(&free_before, &total);
		auto warmup_state = whisper_init_state(ctx.get());
		if (warmup_state != nullptr)
		{
			cudaMemGetInfo(&free_after, &total);
			size_t state_cost = (free_before > free_after) ? (free_before - free_after) : 0;
			_state_memory_bytes[key] = state_cost;
			logti("Whisper state memory cost: %.1f MiB per instance. path=%s",
				static_cast<double>(state_cost) / (1024.0 * 1024.0), path.CStr());

			std::vector<float> silence(WHISPER_SAMPLE_RATE, 0.0f);
			whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
			wparams.print_progress   = false;
			wparams.print_special    = false;
			wparams.print_realtime   = false;
			wparams.print_timestamps = false;
			wparams.n_threads        = 1;
			wparams.language         = "en";
			wparams.no_context       = true;
			whisper_full_with_state(ctx.get(), warmup_state, wparams, silence.data(), static_cast<int>(silence.size()));
			whisper_free_state(warmup_state);
		}
	}
#endif

	_models[key] = std::move(ctx);
	logti("Whisper model loaded successfully. path=%s", path.CStr());
}

void WhisperModelRegistry::Uninitialize()
{
	std::lock_guard<std::mutex> lock(_mutex);
	_models.clear();
	_state_memory_bytes.clear();
	logti("Whisper model registry cleared.");
}

std::shared_ptr<whisper_context> WhisperModelRegistry::GetModelContext(const ov::String &model_path)
{
	std::lock_guard<std::mutex> lock(_mutex);

	const std::string key = model_path.CStr();

	auto it = _models.find(key);
	if (it != _models.end())
	{
		return it->second;
	}

	// Model not preloaded — load on-demand and cache it for future callers.
	logti("Whisper model not preloaded, loading on-demand. path=%s", model_path.CStr());
	LoadModel(model_path);

	it = _models.find(key);
	if (it == _models.end())
	{
		// Loading failed (error already logged inside _LoadModel).
		return nullptr;
	}

	return it->second;
}

whisper_state *WhisperModelRegistry::NewState(const ov::String &model_path)
{
	std::lock_guard<std::mutex> lock(_mutex);

	const std::string key = model_path.CStr();

	auto model_it = _models.find(key);
	if (model_it == _models.end())
	{
		logte("Cannot allocate whisper state: model not loaded. path=%s", model_path.CStr());
		return nullptr;
	}

#ifdef HWACCELS_NVIDIA_ENABLED
	// Check GPU memory before calling whisper_init_state to prevent ggml crash.
	// The mutex ensures serialize check+alloc across all encoder threads.
	auto mem_it = _state_memory_bytes.find(key);
	if (mem_it != _state_memory_bytes.end() && mem_it->second > 0)
	{
		size_t required = mem_it->second;
		size_t free_mem = 0, total_mem = 0;
		if (cudaMemGetInfo(&free_mem, &total_mem) == cudaSuccess && free_mem < required)
		{
			logte("Not enough GPU memory to create Whisper state (required=%.1f MiB, free=%.1f MiB). path=%s",
				  static_cast<double>(required) / (1024.0 * 1024.0),
				  static_cast<double>(free_mem) / (1024.0 * 1024.0),
				  model_path.CStr());
			return nullptr;
		}
	}
#endif

	auto *state = whisper_init_state(model_it->second.get());
	if (state == nullptr)
	{
		logte("whisper_init_state failed. path=%s", model_path.CStr());
	}
	return state;
}

void WhisperModelRegistry::DeleteState(whisper_state *state)
{
	if (state != nullptr)
	{
		whisper_free_state(state);
	}
}
