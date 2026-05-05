# OvenMediaEngine CMake Build System

> **Note**: OvenMediaEngine is currently migrating from the legacy `make` build system to CMake.
> The CMake build is under active development and may not yet cover all edge cases.
> The legacy `make` build (`src/Makefile`) remains available during the transition period.

---

## Quick Start

### Debug Build (default)

```bash
cmake -B build/Debug -G Ninja -DOME_DEP_PREFIX=/opt/ovenmediaengine_getroot
cmake --build build/Debug
```

> External libraries are automatically installed if missing or incorrect version —
> no need to run `InstallPrerequisites.cmake` manually before building.

### Release Build

```bash
cmake -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/Release
```

---

## Build Options

| Option | Default | Description |
|---|---|---|
| `CMAKE_BUILD_TYPE` | `Debug` | `Debug` / `Release` |
| `OME_USE_CLANG` | ON | Use Clang/Clang++ compiler. Set to OFF to use the system default (GCC) |
| `OME_DEP_PREFIX` | `/opt/ovenmediaengine` | Installation prefix for external dependencies |
| `OME_SANITIZE_THREAD` | OFF | Enable ThreadSanitizer (TSan). Debug builds only |
| `OME_SKIP_DEPENDENCY_CHECK` | OFF | Skip auto-install of missing/wrong-version packages. Useful for CI or offline builds |
| `OME_HWACCEL_NVIDIA` | OFF | Enable NVIDIA GPU acceleration. |
| `OME_HWACCEL_QSV` | OFF | Enable Intel QSV acceleration. Requires Intel driver (`libmfx`) installed separately |
| `OME_HWACCEL_XMA` | OFF | Enable Xilinx XMA acceleration. |
| `OME_HWACCEL_NILOGAN` | OFF | Enable Netint NiLogan acceleration. Requires `OME_NILOGAN_PATCH_PATH` |
| `OME_NILOGAN_PATCH_PATH` | `""` | Path to the NiLogan FFmpeg patch file. Required when `OME_HWACCEL_NILOGAN=ON` |
| `OME_NILOGAN_XCODER_COMPILE_PATH` | `""` | Path to `xcoder_logan` source directory to compile (optional) |
| `OME_ENABLE_X264` | ON | Enable libx264 encoder support |
| `OME_ENABLE_JEMALLOC` | OFF/ON | Enable jemalloc allocator. Always ON in Release, OFF by default in Debug |
| `OME_USE_JEMALLOC_PROFILE` | OFF | Enable jemalloc heap profiling (`OME_USE_JEMALLOC_PROFILE` compile definition). Requires `OME_ENABLE_JEMALLOC=ON` |
| `OME_BUILD_TESTS` | OFF | Build unit tests (requires internet access to fetch GTest v1.14.0) |

---

## Install

```bash
# Install to system (requires sudo)
sudo cmake --install build/Release
```

| Path | Description |
|---|---|
| `/usr/share/ovenmediaengine/OvenMediaEngine` | Binary (symbols stripped for Release) |
| `/usr/bin/OvenMediaEngine` | Symlink |
| `/usr/share/ovenmediaengine/conf/` | Config files (not overwritten if already present) |
| `/lib/systemd/system/ovenmediaengine.service` | systemd service |

```bash
sudo systemctl daemon-reload
sudo systemctl enable ovenmediaengine
sudo systemctl start ovenmediaengine
```

---

## Prerequisites Manual Install

Normally not needed — the configure step handles this automatically.

```bash
# Install all
cmake -P cmake/InstallPrerequisites.cmake

# Install a specific library only
cmake -DTARGET=<name> -P cmake/InstallPrerequisites.cmake
```

Available targets:

```
nasm        openssl     libsrtp     libsrt      libopus     libopenh264
libvpx      libwebp     fdk_aac     libx264     ffmpeg      stubs
jemalloc    libpcre2    hiredis     spdlog      whisper     ffnvcodec
```

Available `-D` options:

| Option | Default | Description |
|---|---|---|
| `OME_DEP_PREFIX` | `/opt/ovenmediaengine` | Installation prefix |
| `TARGET` | *(all)* | Install a single target only (e.g. `ffmpeg`, `openssl`) |
| `OME_ENABLE_X264` | `ON` | Include libx264 |
| `OME_HWACCEL_NVIDIA` | `OFF` | Include NVIDIA codec headers, build FFmpeg/Whisper with CUDA/NVENC/NVDEC |
| `OME_HWACCEL_QSV` | `OFF` | Build FFmpeg with Intel QSV support (`libmfx` must be pre-installed) |
| `OME_HWACCEL_XMA` | `OFF` | Build FFmpeg with Xilinx XMA support (Xilinx XRT must be pre-installed) |
| `OME_HWACCEL_NILOGAN` | `OFF` | Build FFmpeg with Netint NiLogan support. Requires `OME_NILOGAN_PATCH_PATH` |
| `OME_NILOGAN_PATCH_PATH` | `""` | Path to the NiLogan FFmpeg patch file |
| `OME_NILOGAN_XCODER_COMPILE_PATH` | `""` | Path to `xcoder_logan` source directory to compile (optional) |
| `OME_USE_CLANG` | `ON` | Install `clang`/`lld` OS packages and use Clang as the compiler. Set `OFF` to skip and keep GCC |

---

## Library Version Management

All external library versions are defined in [`cmake/Versions.cmake`](Versions.cmake).
To upgrade a dependency, update the version there and re-run `cmake -B ...` —
only the changed package will be reinstalled automatically.

---

## Unit Tests

> Unit test coverage is in early stages. Most modules currently have placeholder test files only. Contributions are welcome: see [Adding tests for a new module](#adding-tests-for-a-new-module) below.

Unit tests are opt-in. Enable with `-DOME_BUILD_TESTS=ON`:

```bash
cmake -B build/Debug -S . -DCMAKE_BUILD_TYPE=Debug -DOME_BUILD_TESTS=ON -G Ninja
cmake --build build/Debug --target tests  # skip OvenMediaEngine binary, build only test binaries
ctest --test-dir build/Debug
```

### Test binaries and labels

Each module group compiles into a separate binary with a ctest label:

| Label | Binary | Source path |
|---|---|---|
| `base` | `ome_test_base` | `src/projects/base/` |
| `modules` | `ome_test_modules` | `src/projects/modules/` |
| `config` | `ome_test_config` | `src/projects/config/` |
| `mediarouter` | `ome_test_mediarouter` | `src/projects/mediarouter/` |
| `transcoder` | `ome_test_transcoder` | `src/projects/transcoder/` |
| `orchestrator` | `ome_test_orchestrator` | `src/projects/orchestrator/` |
| `providers_rtmp` | `ome_test_providers_rtmp` | `src/projects/providers/rtmp/` |
| `providers_mpegts` | `ome_test_providers_mpegts` | `src/projects/providers/mpegts/` |
| `providers_multiplex` | `ome_test_providers_multiplex` | `src/projects/providers/multiplex/` |
| `providers_ovt` | `ome_test_providers_ovt` | `src/projects/providers/ovt/` |
| `providers_file` | `ome_test_providers_file` | `src/projects/providers/file/` |
| `providers_rtspc` | `ome_test_providers_rtspc` | `src/projects/providers/rtspc/` |
| `providers_scheduled` | `ome_test_providers_scheduled` | `src/projects/providers/scheduled/` |
| `providers_srt` | `ome_test_providers_srt` | `src/projects/providers/srt/` |
| `providers_webrtc` | `ome_test_providers_webrtc` | `src/projects/providers/webrtc/` |
| `publishers_hls` | `ome_test_publishers_hls` | `src/projects/publishers/hls/` |
| `publishers_llhls` | `ome_test_publishers_llhls` | `src/projects/publishers/llhls/` |
| `publishers_push` | `ome_test_publishers_push` | `src/projects/publishers/push/` |
| `publishers_thumbnail` | `ome_test_publishers_thumbnail` | `src/projects/publishers/thumbnail/` |
| `publishers_file` | `ome_test_publishers_file` | `src/projects/publishers/file/` |
| `publishers_ovt` | `ome_test_publishers_ovt` | `src/projects/publishers/ovt/` |
| `publishers_webrtc` | `ome_test_publishers_webrtc` | `src/projects/publishers/webrtc/` |
| `publishers_srt` | `ome_test_publishers_srt` | `src/projects/publishers/srt/` |

### Filtering tests

```bash
# List all tests
ctest --test-dir build/Debug -N

# Filter by label (binary/label name)
ctest --test-dir build/Debug -L base
ctest --test-dir build/Debug -L modules

# Filter by suite name (regex)
ctest --test-dir build/Debug -R "UrlTest"

# Run a single test case
ctest --test-dir build/Debug -R "UrlTest.ParseValidUrl"

# Run directly via gtest binary (supports --gtest_filter)
./build/Debug/bin/ome_test_base --gtest_filter="UrlTest.*"
./build/Debug/bin/ome_test_base --gtest_filter="UrlTest.ParseValidUrl"
```

### Adding tests for a new module

1. Create `*_test.cpp` files alongside the module source.
2. Add to the module's `CMakeLists.txt`:

```cmake
if(OME_BUILD_TESTS)
    file(GLOB _srcs "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
    ome_add_tests(ome_test_<label>
        SRCS ${_srcs}
    )
endif()
```

Use the same `ome_test_<label>` name as an existing group to merge sources into one binary, or use a new name to create a new binary with a new label.
