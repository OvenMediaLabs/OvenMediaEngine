//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

#include <base/ovlibrary/stop_watch.h>

#include "transcoder_whisper_model_registry.h"
#include "transcoder_private.h"

namespace
{
	size_t GetFileSizeBytes(const ov::String &path)
	{
		struct stat st{};
		return (::stat(path.CStr(), &st) == 0) ? static_cast<size_t>(st.st_size) : 0;
	}

	// Reads the leading number from a /proc or /sys file. Returns false when the
	// file is missing or does not start with a number (e.g. cgroup's "max").
	bool ReadSizeFile(const std::string &path, size_t *value)
	{
		std::ifstream in(path);
		if (in.is_open() == false)
		{
			return false;
		}

		std::string token;
		in >> token;
		if (token.empty())
		{
			return false;
		}

		char *end					   = nullptr;
		const unsigned long long parsed = ::strtoull(token.c_str(), &end, 10);
		if (end == token.c_str())
		{
			return false;
		}

		*value = static_cast<size_t>(parsed);
		return true;
	}

	// Memory the host kernel estimates can be allocated without swapping.
	// Returns 0 when it cannot be determined.
	size_t GetHostAvailableMemoryBytes()
	{
		auto *fp = ::fopen("/proc/meminfo", "r");
		if (fp != nullptr)
		{
			char line[256];
			size_t available_kb = 0;
			while (::fgets(line, sizeof(line), fp) != nullptr)
			{
				if (::sscanf(line, "MemAvailable: %zu kB", &available_kb) == 1)
				{
					break;
				}
			}
			::fclose(fp);

			if (available_kb > 0)
			{
				return available_kb * 1024;
			}
		}

		const long pages	 = ::sysconf(_SC_AVPHYS_PAGES);
		const long page_size = ::sysconf(_SC_PAGESIZE);
		if ((pages > 0) && (page_size > 0))
		{
			return static_cast<size_t>(pages) * static_cast<size_t>(page_size);
		}

		return 0;
	}

	// The cgroup v2 path of this process (the "0::/path" line), or "" when unavailable.
	std::string GetOwnCgroupV2Path()
	{
		std::ifstream in("/proc/self/cgroup");
		std::string line;
		while (std::getline(in, line))
		{
			if (line.rfind("0::", 0) == 0)
			{
				return line.substr(3);
			}
		}
		return "";
	}

	// Headroom left under the memory limit of this process's own cgroup, or
	// SIZE_MAX when it is not limited. Containers are OME's main deployment and
	// /proc/meminfo describes the host there, not the container. Best effort:
	// a limit set on a parent cgroup is not seen.
	size_t GetCgroupAvailableMemoryBytes()
	{
		size_t limit = 0, usage = 0;

		// cgroup v2: the process's own group first, then the namespace root
		// (what a container with its own cgroup namespace sees).
		const std::string own_path = GetOwnCgroupV2Path();
		for (const std::string &base : {std::string("/sys/fs/cgroup") + own_path, std::string("/sys/fs/cgroup")})
		{
			if (ReadSizeFile(base + "/memory.max", &limit) && ReadSizeFile(base + "/memory.current", &usage))
			{
				return (limit > usage) ? (limit - usage) : 0;
			}
		}

		// cgroup v1. An unlimited group reports a sentinel near PAGE_COUNTER_MAX.
		if (ReadSizeFile("/sys/fs/cgroup/memory/memory.limit_in_bytes", &limit) && ReadSizeFile("/sys/fs/cgroup/memory/memory.usage_in_bytes", &usage))
		{
			if (limit >= (static_cast<size_t>(1) << 60))
			{
				return SIZE_MAX;
			}
			return (limit > usage) ? (limit - usage) : 0;
		}

		return SIZE_MAX;
	}

	// The smaller of what the host and the cgroup allow. 0 when unknown.
	size_t GetAvailableMemoryBytes()
	{
		const size_t host	= GetHostAvailableMemoryBytes();
		const size_t cgroup = GetCgroupAvailableMemoryBytes();

		if (host == 0)
		{
			return (cgroup == SIZE_MAX) ? 0 : cgroup;
		}
		return std::min(host, cgroup);
	}

	// Resident set size of this process, or 0 when it cannot be determined.
	// Process-wide, so a delta taken around an allocation is only approximate
	// while the rest of the server is working.
	size_t GetProcessRssBytes()
	{
		auto *fp = ::fopen("/proc/self/statm", "r");
		if (fp == nullptr)
		{
			return 0;
		}

		size_t total_pages = 0, rss_pages = 0;
		const int matched = ::fscanf(fp, "%zu %zu", &total_pages, &rss_pages);
		::fclose(fp);

		if (matched != 2)
		{
			return 0;
		}

		const long page_size = ::sysconf(_SC_PAGESIZE);
		return (page_size > 0) ? (rss_pages * static_cast<size_t>(page_size)) : 0;
	}

	constexpr double ToMiB(size_t bytes)
	{
		return static_cast<double>(bytes) / (1024.0 * 1024.0);
	}
}  // namespace

std::string WhisperModelRegistry::MakeModelKey(const ov::String &model_path, int32_t device_id)
{
	// Whisper runs on the CPU, so one loaded context serves every encoder
	// regardless of the device id the configuration asked for.
	(void)device_id;

	return model_path.CStr();
}

whisper_context_params WhisperModelRegistry::BuildContextParams(int32_t device_id)
{
	(void)device_id;

	struct whisper_context_params cparams = whisper_context_default_params();
	cparams.flash_attn = true;
	cparams.use_gpu	   = false;

	return cparams;
}

bool WhisperModelRegistry::CheckMemoryAvailable(const ov::String &model_path, size_t required_bytes, const char *what)
{
	if (required_bytes == 0)
	{
		// Nothing to compare against (model size or state cost unknown).
		return true;
	}

	const size_t available_bytes = GetAvailableMemoryBytes();
	if (available_bytes == 0)
	{
		// Could not read the available memory; do not block the allocation.
		logtw("Could not determine available memory before allocating the Whisper %s. Proceeding. path=%s", what, model_path.CStr());
		return true;
	}

	if (available_bytes < required_bytes)
	{
		logte("Not enough memory for the Whisper %s (available=%.1f MiB, required≈%.1f MiB). path=%s",
			  what, ToMiB(available_bytes), ToMiB(required_bytes), model_path.CStr());
		return false;
	}

	return true;
}

int32_t WhisperModelRegistry::GetHardwareThreads()
{
	return static_cast<int32_t>(std::max(1u, std::thread::hardware_concurrency()));
}

int32_t WhisperModelRegistry::GetDefaultThreadCount()
{
	const int32_t hardware_threads = GetHardwareThreads();

	// Whisper scales poorly past ~8 threads and shares the machine with the
	// transcoder, so take a quarter of the machine and cap it at 8.
	return std::min(std::clamp(hardware_threads / 4, 2, 8), hardware_threads);
}

void WhisperModelRegistry::SetMaxThreads(int32_t max_threads)
{
	_max_threads.store(std::max(0, max_threads), std::memory_order_relaxed);
}

int32_t WhisperModelRegistry::GetThreadShare(int32_t requested_threads) const
{
	if (requested_threads <= 0)
	{
		requested_threads = GetDefaultThreadCount();
	}

	const int32_t max_threads = _max_threads.load(std::memory_order_relaxed);
	const int32_t budget	  = (max_threads > 0) ? max_threads : GetHardwareThreads();
	const int32_t live_states = std::max(1, _live_states.load(std::memory_order_relaxed));

	// Every live track keeps at least one thread, so with more tracks than
	// budget the total exceeds it; the alternative is a track that never
	// transcribes at all.
	return std::max(1, std::min(requested_threads, budget / live_states));
}

bool WhisperModelRegistry::Preload(const std::vector<std::pair<ov::String, std::vector<int32_t>>> &models)
{
	// Largest first, so the biggest model claims memory before smaller ones
	// fill in whatever remains. stat() each file once rather than per comparison.
	std::vector<std::pair<size_t, ov::String>> sorted_models;
	sorted_models.reserve(models.size());
	for (const auto &[path, device_ids] : models)
	{
		(void)device_ids;
		sorted_models.emplace_back(GetFileSizeBytes(path), path);
	}
	std::sort(sorted_models.begin(), sorted_models.end(), [](const auto &a, const auto &b) {
		return a.first > b.first;  // descending
	});

	for (const auto &[size, path] : sorted_models)
	{
		(void)size;
		GetModelContext(path);
	}

	return true;
}

std::shared_ptr<whisper_context> WhisperModelRegistry::GetModelContext(const ov::String &model_path, int32_t device_id)
{
	const std::string key = MakeModelKey(model_path, device_id);
	int32_t warmup_threads = 1;

	{
		ov::LockGuard<ov::Mutex> lock(_mutex);

		// Another caller may be loading this very model; wait for it rather
		// than reading the file twice.
		_load_done.Wait(lock, [this, &key]() OV_REQUIRES(_mutex) -> bool {
			return _loading.count(key) == 0;
		});

		auto it = _models.find(key);
		if (it != _models.end())
		{
			return it->second;
		}

		_loading.insert(key);
		warmup_threads = GetThreadShare(0);
	}

	// Reading the file and running the warmup take seconds on the CPU, so they
	// happen without the lock: other encoders keep allocating and freeing
	// states, and only callers of this same model wait.
	logti("Loading Whisper model. path=%s", model_path.CStr());

	size_t state_memory_bytes = 0;
	auto ctx				  = LoadModel(model_path, device_id, warmup_threads, &state_memory_bytes);

	{
		ov::LockGuard<ov::Mutex> lock(_mutex);

		if (ctx != nullptr)
		{
			_models[key]			 = ctx;
			_state_memory_bytes[key] = state_memory_bytes;
		}

		_loading.erase(key);
		_load_done.NotifyAll();
	}

	return ctx;
}

std::shared_ptr<whisper_context> WhisperModelRegistry::LoadModel(const ov::String &path, int32_t device_id, int32_t warmup_threads, size_t *state_memory_bytes)
{
	*state_memory_bytes = 0;

	// Required: model file size * 2 (weights + compute buffers).
	const size_t model_file_bytes = GetFileSizeBytes(path);
	if (CheckMemoryAvailable(path, model_file_bytes * 2, "model") == false)
	{
		return nullptr;
	}

	auto raw_ctx = whisper_init_from_file_with_params(path.CStr(), BuildContextParams(device_id));
	if (raw_ctx == nullptr)
	{
		logte("Failed to load Whisper model. path=%s", path.CStr());
		return nullptr;
	}

	// Wrap in shared_ptr with whisper_free as custom deleter.
	auto ctx = std::shared_ptr<whisper_context>(raw_ctx, [](whisper_context *c) {
		whisper_free(c);
	});

	// Warmup: run a full inference pass over 1 second of silence through a
	// temporary state. The RSS delta spans both the state allocation and the
	// pass, because ggml's compute buffers only become resident once they are
	// written; measured this way the figure is what one real state costs, and
	// NewState() uses it to refuse a state that would not fit.
	{
		ov::StopWatch warmup_timer;
		warmup_timer.Start();

		const size_t rss_before = GetProcessRssBytes();

		auto warmup_state = whisper_init_state(ctx.get());
		if (warmup_state != nullptr)
		{
			std::vector<float> silence(WHISPER_SAMPLE_RATE, 0.0f);
			whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
			wparams.print_progress		= false;
			wparams.print_special		= false;
			wparams.print_realtime		= false;
			wparams.print_timestamps	= false;
			wparams.n_threads			= warmup_threads;
			wparams.language			= "en";
			wparams.no_context			= true;
			whisper_full_with_state(ctx.get(), warmup_state, wparams, silence.data(), static_cast<int>(silence.size()));

			const size_t rss_after = GetProcessRssBytes();
			*state_memory_bytes	   = (rss_after > rss_before) ? (rss_after - rss_before) : 0;

			whisper_free_state(warmup_state);

			if (*state_memory_bytes > 0)
			{
				logti("Whisper state memory cost: %.1f MiB per instance. path=%s", ToMiB(*state_memory_bytes), path.CStr());
			}
		}

		logtd("Whisper warmup took %" PRId64 " ms with %d threads. path=%s", warmup_timer.Elapsed(), warmup_threads, path.CStr());
	}

	logti("Whisper model loaded successfully. path=%s", path.CStr());

	return ctx;
}

void WhisperModelRegistry::Uninitialize()
{
	ov::LockGuard<ov::Mutex> lock(_mutex);

	_models.clear();
	_state_memory_bytes.clear();
	_live_states.store(0, std::memory_order_relaxed);

	logti("Whisper model registry cleared.");
}

whisper_state *WhisperModelRegistry::NewState(const ov::String &model_path, int32_t device_id)
{
	ov::LockGuard<ov::Mutex> lock(_mutex);

	const std::string key = MakeModelKey(model_path, device_id);

	auto model_it = _models.find(key);
	if (model_it == _models.end())
	{
		logte("Cannot allocate whisper state: model not loaded. path=%s", model_path.CStr());
		return nullptr;
	}

	// Check memory before whisper_init_state so an oversubscribed server
	// refuses the state instead of being OOM-killed on its first window.
	// The mutex serializes check+alloc across all encoder threads.
	auto mem_it = _state_memory_bytes.find(key);
	if ((mem_it != _state_memory_bytes.end()) && (CheckMemoryAvailable(model_path, mem_it->second, "state") == false))
	{
		return nullptr;
	}

	auto *state = whisper_init_state(model_it->second.get());
	if (state == nullptr)
	{
		logte("whisper_init_state failed. path=%s", model_path.CStr());
		return nullptr;
	}

	_live_states.fetch_add(1, std::memory_order_relaxed);

	return state;
}

void WhisperModelRegistry::DeleteState(whisper_state *state)
{
	// Serialized with NewState so whisper_free_state and whisper_init_state
	// never run concurrently on the shared whisper context.
	ov::LockGuard<ov::Mutex> lock(_mutex);

	if (state == nullptr)
	{
		return;
	}

	whisper_free_state(state);

	if (_live_states.fetch_sub(1, std::memory_order_relaxed) <= 0)
	{
		// Alloc/free are paired by the encoder; clamp rather than go negative.
		_live_states.store(0, std::memory_order_relaxed);
	}
}
