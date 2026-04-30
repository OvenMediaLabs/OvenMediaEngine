#pragma once

#ifdef __APPLE__
#include <pthread.h>

// On macOS, pthread_setname_np(const char*) only sets the *current* thread's name.
// The Linux two-argument form pthread_setname_np(pthread_t, const char*) does not exist.
// Provide an overload so the codebase compiles unchanged; the call becomes a no-op.
inline int pthread_setname_np(pthread_t, const char*) { return 0; }

// macOS <uuid/uuid.h> does not define UUID_STR_LEN (a Linux e2fsprogs constant).
// A UUID string is "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" (36 chars) + NUL = 37.
#ifndef UUID_STR_LEN
#define UUID_STR_LEN 37
#endif

// macOS does not define HOST_NAME_MAX; use MAXHOSTNAMELEN (256) or POSIX minimum (255).
#ifndef HOST_NAME_MAX
#ifdef MAXHOSTNAMELEN
#define HOST_NAME_MAX MAXHOSTNAMELEN
#else
#define HOST_NAME_MAX 255
#endif
#endif

#endif  // __APPLE__
