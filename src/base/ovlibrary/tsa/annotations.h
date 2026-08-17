//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#pragma once

// Clang Thread Safety Analysis (TSA) annotation macros.
//
// These macros expand to GCC/Clang `__attribute__((...))`
// only when both conditions hold:
//
//   1. The compiler is Clang (`__clang__`).
//   2. The build has opted into TSA via `-DOME_THREAD_SAFETY`.
//
// In every other case (GCC, MSVC, Clang without the opt-in macro) every macro expands to nothing.
// That way the same source compiles on any supported toolchain and the annotations cost
// literally zero at runtime.
//
// Reference:
//   https://clang.llvm.org/docs/ThreadSafetyAnalysis.html
//
// All macro names use the `OV_` prefix so they cannot collide with the underscored attribute
// names that Clang itself documents.

#if defined(__clang__) && defined(OME_THREAD_SAFETY)
// Internal dispatcher.
// Wrapping `__attribute__((x))` in a single macro keeps each public macro on one line
// and limits the conditional logic to one spot.
// Do not use `OV_TS_ATTR` outside this header.
#	define OV_TS_ATTR(x) __attribute__((x))
#else
#	define OV_TS_ATTR(x)
#endif

// ----------------------------------------------------------------------------
// Capability declaration
// ----------------------------------------------------------------------------

// Marks a class as a capability.
// The string argument is only the user-visible kind name shown in diagnostics
// (e.g. "requires holding mutex 'm'");
// it does not affect the analysis itself.
// Use this directly only for a non-mutex capability that wants a different diagnostic label
// (e.g. a thread "role");
// for the common mutex-like case use `OV_LOCKABLE`.
#define OV_CAPABILITY(x) OV_TS_ATTR(capability(x))

// Convenience for the common case: a mutex-like lockable type.
// Equivalent to `OV_CAPABILITY("mutex")`, so diagnostics read "...
// mutex 'm' ...".
// Goes on the mutex type itself;
// the RAII guard uses `OV_SCOPED_CAPABILITY`.
#define OV_LOCKABLE OV_CAPABILITY("mutex")

// Marks an RAII guard class that acquires a capability in its constructor and releases
// it in its destructor.
#define OV_SCOPED_CAPABILITY OV_TS_ATTR(scoped_lockable)

// ----------------------------------------------------------------------------
// Data / capability binding
// ----------------------------------------------------------------------------

// The annotated data member may only be accessed while the named capability is held:
// reading requires the capability held in at least shared mode, writing requires it
// held in exclusive mode. (For a non-shared mutex every hold is exclusive, so both
// reads and writes need the lock.)
#define OV_GUARDED_BY(x) OV_TS_ATTR(guarded_by(x))

// Same as `OV_GUARDED_BY` but applies to the pointee of a pointer member - the pointer itself
// is unprotected, the data it points to is guarded.
#define OV_PT_GUARDED_BY(x) OV_TS_ATTR(pt_guarded_by(x))

// ----------------------------------------------------------------------------
// Function preconditions
// ----------------------------------------------------------------------------

// The caller must hold the listed capabilities in exclusive mode before invoking the function.
#define OV_REQUIRES(...) OV_TS_ATTR(requires_capability(__VA_ARGS__))

// The caller must hold the listed capabilities in shared mode before invoking the function.
#define OV_REQUIRES_SHARED(...) OV_TS_ATTR(requires_shared_capability(__VA_ARGS__))

// The listed capabilities must NOT be held when invoking the function.
// Useful for guarding against reentrant calls on non-recursive mutexes.
#define OV_EXCLUDES(...) OV_TS_ATTR(locks_excluded(__VA_ARGS__))

// ----------------------------------------------------------------------------
// Capability state transitions
// ----------------------------------------------------------------------------

// The function acquires the listed capabilities in exclusive mode and they remain held
// when the function returns.
#define OV_ACQUIRE(...) OV_TS_ATTR(acquire_capability(__VA_ARGS__))

// The function acquires the listed capabilities in shared mode.
#define OV_ACQUIRE_SHARED(...) OV_TS_ATTR(acquire_shared_capability(__VA_ARGS__))

// The function releases the listed capabilities from exclusive mode.
#define OV_RELEASE(...) OV_TS_ATTR(release_capability(__VA_ARGS__))

// The function releases the listed capabilities from shared mode.
#define OV_RELEASE_SHARED(...) OV_TS_ATTR(release_shared_capability(__VA_ARGS__))

// The function attempts to acquire the listed capabilities in exclusive mode.
// The first argument is the success value (`true` or `false`);
// the remaining arguments are the capability expressions.
#define OV_TRY_ACQUIRE(...) OV_TS_ATTR(try_acquire_capability(__VA_ARGS__))

// Same as `OV_TRY_ACQUIRE` but for shared-mode acquisition.
#define OV_TRY_ACQUIRE_SHARED(...) OV_TS_ATTR(try_acquire_shared_capability(__VA_ARGS__))

// ----------------------------------------------------------------------------
// Runtime assertions about capability state
// ----------------------------------------------------------------------------

// Tells the analyzer that the named capability is held in exclusive mode at this point
// in the program.
// Useful inside helpers that assert lock state through some side channel
// (e.g. a thread-local check).
#define OV_ASSERT_CAPABILITY(x) OV_TS_ATTR(assert_capability(x))

// Same as `OV_ASSERT_CAPABILITY` but for shared-mode capability.
#define OV_ASSERT_SHARED_CAPABILITY(x) OV_TS_ATTR(assert_shared_capability(x))

// ----------------------------------------------------------------------------
// Escape hatch
// ----------------------------------------------------------------------------

// Disables thread-safety analysis on the annotated function.
// Use only when the analyzer cannot reason about a deliberately exotic locking pattern
// (e.g. lock handoff between threads) and document the reason inline.
#define OV_NO_THREAD_SAFETY_ANALYSIS OV_TS_ATTR(no_thread_safety_analysis)
