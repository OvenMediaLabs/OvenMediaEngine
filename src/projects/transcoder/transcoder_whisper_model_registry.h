//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <whisper.h>

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <base/ovlibrary/ovlibrary.h>

class WhisperModelRegistry : public ov::Singleton<WhisperModelRegistry>
{
public:
// Eagerly load the given models. Optional — call at server start to preload.
        // Each entry is a (resolved_path, cuda_device_ids) pair.
        // An empty device_ids list means "load on all available CUDA devices".
        bool Preload(const std::vector<std::pair<ov::String, std::vector<int32_t>>> &models);

	// Release all loaded models. Called at server stop.
	void Uninitialize();

// Return a shared_ptr to the whisper_context for the given model path and CUDA device.
        // If the model is not yet loaded on that device it will be loaded on-demand and cached.
        std::shared_ptr<whisper_context> GetModelContext(const ov::String &model_path, int32_t cuda_device_id = 0);

        // Allocate a per-encoder whisper_state for the given model and CUDA device.
        // Checks GPU memory availability before allocation to prevent ggml crash.
        // Returns nullptr if the model is not loaded or GPU memory is insufficient.
        whisper_state *NewState(const ov::String &model_path, int32_t cuda_device_id = 0);

        // Free a whisper_state previously returned by NewState.
        void DeleteState(whisper_state *state);

private:
        // Load a single model on the specified CUDA device and cache it. Caller must hold _mutex.
        void LoadModel(const ov::String &model_path, int32_t cuda_device_id = 0);

	std::mutex _mutex;
	std::unordered_map<std::string, std::shared_ptr<whisper_context>> _models;
	// GPU memory (bytes) consumed by one whisper_state for each model.
	// Populated during GPU warmup; 0 means CPU model (no pre-check needed).
	std::unordered_map<std::string, size_t> _state_memory_bytes;
};
