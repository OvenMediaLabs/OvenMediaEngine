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
#include <optional>
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

	// Memory the host kernel estimates can be allocated without swapping, or
	// nullopt when it cannot be determined. A reading of 0 is a real reading
	// and must not be mistaken for "unknown".
	std::optional<size_t> GetHostAvailableMemoryBytes()
	{
		auto *fp = ::fopen("/proc/meminfo", "r");
		if (fp != nullptr)
		{
			char line[256];
			size_t available_kb = 0;
			bool matched		= false;
			while (::fgets(line, sizeof(line), fp) != nullptr)
			{
				if (::sscanf(line, "MemAvailable: %zu kB", &available_kb) == 1)
				{
					matched = true;
					break;
				}
			}
			::fclose(fp);

			if (matched)
			{
				return available_kb * 1024;
			}
		}

		const long pages	 = ::sysconf(_SC_AVPHYS_PAGES);
		const long page_size = ::sysconf(_SC_PAGESIZE);
		if ((pages >= 0) && (page_size > 0))
		{
			return static_cast<size_t>(pages) * static_cast<size_t>(page_size);
		}

		return std::nullopt;
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

	// The cgroup v1 memory-controller path of this process (the
	// "N:<controllers>:/path" line whose controller list has "memory"), or ""
	// when unavailable.
	std::string GetOwnCgroupV1MemoryPath()
	{
		std::ifstream in("/proc/self/cgroup");
		std::string line;
		while (std::getline(in, line))
		{
			const auto first  = line.find(':');
			const auto second = (first == std::string::npos) ? std::string::npos : line.find(':', first + 1);
			if (second == std::string::npos)
			{
				continue;
			}

			const std::string controllers = "," + line.substr(first + 1, second - first - 1) + ",";
			if (controllers.find(",memory,") != std::string::npos)
			{
				return line.substr(second + 1);
			}
		}
		return "";
	}

	// Smallest (limit - usage) found walking from <path> up to the root of
	// <base>, or SIZE_MAX when no level carries a numeric limit. The effective
	// limit can sit on any ancestor: a systemd slice above the service, a pod
	// above the container. Levels whose files are missing or read "max" are
	// skipped; with <v1_sentinel> the near-PAGE_COUNTER_MAX value cgroup v1
	// uses for "unlimited" is skipped as well.
	size_t MinCgroupHeadroomBytes(const std::string &base, std::string path, const char *limit_file, const char *usage_file, bool v1_sentinel)
	{
		size_t headroom = SIZE_MAX;

		// "/" and "" both mean the root; drop trailing slashes so the joins below stay clean.
		while ((path.empty() == false) && (path.back() == '/'))
		{
			path.pop_back();
		}

		while (true)
		{
			size_t limit = 0, usage = 0;
			if (ReadSizeFile(base + path + "/" + limit_file, &limit) && ReadSizeFile(base + path + "/" + usage_file, &usage))
			{
				const bool unlimited = v1_sentinel && (limit >= (static_cast<size_t>(1) << 60));
				if (unlimited == false)
				{
					headroom = std::min(headroom, (limit > usage) ? (limit - usage) : static_cast<size_t>(0));
				}
			}

			if (path.empty())
			{
				break;
			}
			const auto slash = path.rfind('/');
			path			 = (slash == std::string::npos) ? std::string() : path.substr(0, slash);
		}

		return headroom;
	}

	// Headroom left under the tightest memory limit of this process's cgroup
	// and its ancestors, or nullopt when no limit is set. Containers are OME's
	// main deployment and /proc/meminfo describes the host there, not the
	// container. 0 means the group is at its limit, which is a real reading.
	std::optional<size_t> GetCgroupAvailableMemoryBytes()
	{
		// cgroup v2. Inside a container with its own cgroup namespace the
		// process path is "/" and the root is the container's group.
		const size_t v2 = MinCgroupHeadroomBytes("/sys/fs/cgroup", GetOwnCgroupV2Path(), "memory.max", "memory.current", false);
		if (v2 != SIZE_MAX)
		{
			return v2;
		}

		// cgroup v1: one directory tree per controller.
		const size_t v1 = MinCgroupHeadroomBytes("/sys/fs/cgroup/memory", GetOwnCgroupV1MemoryPath(), "memory.limit_in_bytes", "memory.usage_in_bytes", true);
		if (v1 != SIZE_MAX)
		{
			return v1;
		}

		return std::nullopt;
	}

	// The smaller of what the host and the cgroup allow, or nullopt when
	// neither could be read. A known 0 stays 0 so the caller rejects.
	std::optional<size_t> GetAvailableMemoryBytes()
	{
		const auto host	  = GetHostAvailableMemoryBytes();
		const auto cgroup = GetCgroupAvailableMemoryBytes();

		if (host.has_value() == false)
		{
			return cgroup;
		}
		if (cgroup.has_value() == false)
		{
			return host;
		}
		return std::min(*host, *cgroup);
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

bool WhisperModelRegistry::CheckMemoryAvailable(const ov::String &model_path, size_t required_bytes, size_t reserved_bytes, const char *what)
{
	if (required_bytes == 0)
	{
		// Nothing to compare against (model size or state cost unknown).
		return true;
	}

	const auto available = GetAvailableMemoryBytes();
	if (available.has_value() == false)
	{
		// Could not read the available memory; do not block the allocation.
		logtw("Could not determine available memory before allocating the Whisper %s. Proceeding. path=%s", what, model_path.CStr());
		return true;
	}
	const size_t available_bytes = *available;

	// Memory a load in flight or a freshly allocated state has claimed is not
	// resident yet, so MemAvailable still counts it as free; take it off the top.
	const size_t headroom_bytes = (available_bytes > reserved_bytes) ? (available_bytes - reserved_bytes) : 0;
	if (headroom_bytes < required_bytes)
	{
		logte("Not enough memory for the Whisper %s (available=%.1f MiB, reserved by loads and new states=%.1f MiB, required≈%.1f MiB). path=%s",
			  what, ToMiB(available_bytes), ToMiB(reserved_bytes), ToMiB(required_bytes), model_path.CStr());
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
	// happen without _mutex: other encoders keep allocating and freeing states,
	// and only callers of this same model wait. Loads are still one at a time
	// under _load_mutex so their memory checks and RSS measurements do not
	// interleave.
	logti("Loading Whisper model. path=%s", model_path.CStr());

	size_t state_memory_bytes = 0;
	std::shared_ptr<whisper_context> ctx;
	{
		ov::LockGuard<ov::Mutex> load_lock(_load_mutex);
		ctx = LoadModel(model_path, device_id, warmup_threads, &state_memory_bytes);
	}

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

	// Required: model file size * 2 (weights + compute buffers). Claim it up
	// front so a NewState() racing with this load sees the memory as taken
	// before the weights are actually resident.
	const size_t required_bytes = GetFileSizeBytes(path) * 2;
	{
		ov::LockGuard<ov::Mutex> lock(_mutex);
		if (CheckMemoryAvailable(path, required_bytes, ReservedBytesLocked(), "model") == false)
		{
			return nullptr;
		}
		_reserved_bytes += required_bytes;
	}

	// Give the reservation back on every exit from here on; by then the memory
	// is either resident (and visible to MemAvailable) or was never taken.
	auto release_reservation = [this, required_bytes]() {
		ov::LockGuard<ov::Mutex> lock(_mutex);
		_reserved_bytes = (_reserved_bytes > required_bytes) ? (_reserved_bytes - required_bytes) : 0;
	};

	auto raw_ctx = whisper_init_from_file_with_params(path.CStr(), BuildContextParams(device_id));
	if (raw_ctx == nullptr)
	{
		logte("Failed to load Whisper model. path=%s", path.CStr());
		release_reservation();
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

	release_reservation();
	logti("Whisper model loaded successfully. path=%s", path.CStr());

	return ctx;
}

void WhisperModelRegistry::Uninitialize()
{
	ov::LockGuard<ov::Mutex> lock(_mutex);

	_models.clear();
	_state_memory_bytes.clear();
	_pending_states.clear();
	_pending_state_bytes = 0;
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
	const size_t state_cost = (_state_memory_bytes.count(key) > 0) ? _state_memory_bytes.at(key) : 0;
	if (CheckMemoryAvailable(model_path, state_cost, ReservedBytesLocked(), "state") == false)
	{
		return nullptr;
	}

	auto *state = whisper_init_state(model_it->second.get());
	if (state == nullptr)
	{
		logte("whisper_init_state failed. path=%s", model_path.CStr());
		return nullptr;
	}

	// The state's compute buffers only become resident on its first inference,
	// so until then MemAvailable does not show them. Hold the measured cost as
	// a reservation so the next NewState() does not hand the same memory out
	// again; the encoder releases it via MarkStateResident().
	if (state_cost > 0)
	{
		_pending_states[state] = state_cost;
		_pending_state_bytes += state_cost;
	}

	_live_states.fetch_add(1, std::memory_order_relaxed);

	return state;
}

void WhisperModelRegistry::MarkStateResident(whisper_state *state)
{
	ov::LockGuard<ov::Mutex> lock(_mutex);

	auto it = _pending_states.find(state);
	if (it != _pending_states.end())
	{
		_pending_state_bytes = (_pending_state_bytes > it->second) ? (_pending_state_bytes - it->second) : 0;
		_pending_states.erase(it);
	}
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

	auto pending_it = _pending_states.find(state);
	if (pending_it != _pending_states.end())
	{
		_pending_state_bytes = (_pending_state_bytes > pending_it->second) ? (_pending_state_bytes - pending_it->second) : 0;
		_pending_states.erase(pending_it);
	}

	if (_live_states.fetch_sub(1, std::memory_order_relaxed) <= 0)
	{
		// Alloc/free are paired by the encoder; clamp rather than go negative.
		_live_states.store(0, std::memory_order_relaxed);
	}
}
