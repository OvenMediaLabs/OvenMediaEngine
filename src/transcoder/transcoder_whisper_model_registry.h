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

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <base/ovlibrary/ovlibrary.h>

// Whisper inference runs on the CPU. The device_id parameters below are kept so
// that the configuration schema (<Devices>, <Modules>nv:N) and the call sites
// stay source-compatible, but they do not select a compute device.
class WhisperModelRegistry : public ov::Singleton<WhisperModelRegistry>
{
public:
	// Eagerly load the given models. Optional — call at server start to preload.
	// Each entry is a (resolved_path, device_ids) pair. The device list is
	// accepted for configuration compatibility and is not used.
	bool Preload(const std::vector<std::pair<ov::String, std::vector<int32_t>>> &models);

	// Release all loaded models. Called at server stop; loads in flight are
	// drained first and no new load is accepted afterwards.
	void Uninitialize();

	// Total number of inference threads Whisper may use across every STT track.
	// 0 (the default) means every hardware thread.
	void SetMaxThreads(int32_t max_threads);

	// Hardware threads on this machine, never less than 1.
	static int32_t GetHardwareThreads();

	// Threads to give one STT track when <Threads> is omitted.
	static int32_t GetDefaultThreadCount();

	// Threads an STT track should use for its next inference: its <Threads>
	// request (0 = default) capped by an equal share of the budget among the
	// states currently alive, and never less than 1. Re-evaluated on every
	// call so shares follow tracks as they start and stop.
	int32_t GetThreadShare(int32_t requested_threads) const;

	// Return a shared_ptr to the whisper_context for the given model path.
	// If the model is not yet loaded it will be loaded on-demand and cached.
	// Loading runs outside the registry lock so other encoders keep allocating
	// and freeing states while a model is read and warmed up.
	std::shared_ptr<whisper_context> GetModelContext(const ov::String &model_path, int32_t device_id = 0);

	// Allocate a per-encoder whisper_state for the given model.
	// Checks available memory before allocation. Returns nullptr if the model
	// is not loaded or memory is insufficient.
	whisper_state *NewState(const ov::String &model_path, int32_t device_id = 0);

	// Free a whisper_state previously returned by NewState.
	void DeleteState(whisper_state *state);

	// Tell the registry that <state> has completed its first inference. Its
	// compute buffers are resident from then on and MemAvailable accounts for
	// them, so the admission reservation NewState() took for it is released.
	void MarkStateResident(whisper_state *state);

private:
	// Read and warm up a model. Runs under _load_mutex but without _mutex, and
	// reserves the model's memory in _reserved_bytes for the duration. Returns
	// nullptr on failure (including a failed warmup); on success
	// *state_memory_bytes receives the measured cost of one whisper_state, or a
	// conservative estimate when it could not be measured.
	std::shared_ptr<whisper_context> LoadModel(const ov::String &model_path, int32_t device_id, int32_t warmup_threads, size_t *state_memory_bytes) OV_REQUIRES(_load_mutex);

	// Cache key for a loaded model. One CPU context is shared by every encoder,
	// so the device id does not take part in the key.
	static std::string MakeModelKey(const ov::String &model_path, int32_t device_id);

	// Context parameters used to load a model.
	static whisper_context_params BuildContextParams(int32_t device_id);

	// True when <required_bytes> fits in what is available after discounting
	// <reserved_bytes> (loads in flight whose memory is not resident yet). Linux
	// overcommits, so a too-large allocation usually ends in the OOM killer
	// rather than a nullptr; checking up front is the only way to refuse a model
	// gracefully.
	static bool CheckMemoryAvailable(const ov::String &model_path, size_t required_bytes, size_t reserved_bytes, const char *what);

	ov::Mutex _mutex;
	ov::ConditionVariable _load_done;
	// Serializes model loads with each other (never held together with _mutex
	// for longer than a field update). Two loads interleaving would both pass
	// the memory check before either committed its memory, and their RSS
	// deltas would pollute each other.
	ov::Mutex _load_mutex;
	// Bytes a load in progress has claimed but not yet made resident; NewState()
	// discounts them so it does not hand out the same memory twice.
	size_t _reserved_bytes OV_GUARDED_BY(_mutex) = 0;
	// Admission reservation for each live state whose buffers are not resident
	// yet (allocated, no inference run). Released by MarkStateResident() or
	// DeleteState(); _pending_state_bytes is the running sum.
	std::unordered_map<whisper_state *, size_t> _pending_states OV_GUARDED_BY(_mutex);
	size_t _pending_state_bytes OV_GUARDED_BY(_mutex) = 0;

	// Everything claimed but not yet visible to MemAvailable.
	size_t ReservedBytesLocked() const OV_REQUIRES(_mutex)
	{
		return _reserved_bytes + _pending_state_bytes;
	}
	std::unordered_map<std::string, std::shared_ptr<whisper_context>> _models OV_GUARDED_BY(_mutex);
	// Models being loaded outside the lock right now, so a second caller waits
	// for the first instead of reading the same file twice.
	std::unordered_set<std::string> _loading OV_GUARDED_BY(_mutex);
	// Set when Uninitialize() starts; GetModelContext() refuses to begin a load
	// afterwards. The registry is torn down once, at server shutdown, and is
	// not reused.
	bool _shutting_down OV_GUARDED_BY(_mutex) = false;
	// Memory (bytes) consumed by one whisper_state for each model, measured
	// (or conservatively estimated) during warmup.
	std::unordered_map<std::string, size_t> _state_memory_bytes OV_GUARDED_BY(_mutex);

	// Thread budget (0 = every hardware thread) and the number of live states
	// sharing it. Atomics so GetThreadShare() needs no lock on the inference path.
	std::atomic<int32_t> _max_threads{0};
	std::atomic<int32_t> _live_states{0};
};
