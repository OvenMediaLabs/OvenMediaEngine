# Building OvenMediaEngine on macOS (Apple Silicon)

> **Experimental:** macOS support is still in early stages. Some features may not work correctly.
> Pull requests are welcome.

Tested on: **macOS 15, Apple Silicon (arm64)**. Other macOS versions and Intel (x86_64) may also work but have not been tested.

---

## 1. System Prerequisites

### Xcode Command Line Tools

Required for the compiler (`clang`) and SDK headers. If you have Xcode installed this is already present; otherwise install the standalone CLI tools:

```bash
xcode-select --install
```

Verify:
```bash
xcode-select -p   # should print /Library/Developer/CommandLineTools or similar
clang --version   # should print Apple clang 15+
```

### Homebrew

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

After install, follow the printed instructions to add Homebrew to your PATH (typically `eval "$(/opt/homebrew/bin/brew shellenv)"`).

### Homebrew packages

```bash
brew install cmake ninja pkg-config nasm automake libtool xz
```

| Package | Purpose |
|---------|---------|
| `cmake` | Build system (3.24+ required) |
| `ninja` | Fast build backend |
| `pkg-config` | Library discovery |
| `nasm` | Assembler for FFmpeg/OpenSSL |
| `automake` / `libtool` | Required to build several C libraries from source |
| `xz` | Archive decompression during dependency build |

---

## 2. Configure & Build

External dependencies are installed automatically at configure time if missing.
For full build options and details, see [README.md](README.md).

```bash
# Debug build
cmake -B build/Debug -G Ninja

# Release build
cmake -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release
```

> **Do not** pass `-DOME_HWACCEL_NVIDIA=ON` on macOS — CUDA is not available on Apple Silicon.

```bash
cmake --build build/Debug --parallel $(sysctl -n hw.ncpu)
```

---

## 3. Run

```bash
# Copy example configs (first time only)
mkdir -p build/Debug/bin/conf
cp misc/conf_examples/Server.xml build/Debug/bin/conf/
cp misc/conf_examples/Logger.xml build/Debug/bin/conf/

# Run
build/Debug/bin/OvenMediaEngine
```

---

## 4. Known Differences from Linux

| Area | Linux | macOS |
|------|-------|-------|
| Linker | GNU ld (`--start-group`) | Apple ld64 (`-all_load`) |
| epoll | `epoll_*` syscalls | kqueue wrapper (`epoll_wrapper.h`) |
| `sem_init` | unnamed semaphores supported | not supported → `condition_variable` |
| `pthread_setname_np` | `(pthread_t, name)` | `(name)` — current thread only |
| `high_resolution_clock` | alias for `system_clock` | alias for `steady_clock` |
| `TCP_QUICKACK` / `SOL_TCP` | available | not available — guarded |
| `SIOCINQ` / `SIOCOUTQ` | available | not available — guarded |
| `malloc_trim` | glibc only | not available — guarded |
| Hardware acceleration | NVIDIA / QSV / XMA | not supported |
| Whisper (STT) | supported with hardware acceleration | **not supported** — OvenMediaEngine requires hardware acceleration to use Whisper, which is not available on macOS |
