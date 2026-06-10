//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================

// Compile-time regression guard for the Clang Thread Safety Analysis
// (TSA) annotations on `base/ovlibrary/tsa/mutex.h`.
//
// Why a separate file: GTest tests only observe runtime behavior,
// so a silently dropped `OV_RELEASE` or a wrongly widened
// `OV_NO_THREAD_SAFETY_ANALYSIS` would still let `mutex_test.cpp` pass.
//
// How it works: this file deliberately writes INCORRECT lock usage
// (e.g. touching `OV_GUARDED_BY` data with no lock held) and marks each such line with a Clang
// verify marker naming the diagnostic that must fire there.
// Built under `-Xclang -verify -Wthread-safety`,
// the compile passes only when every marked diagnostic fires as expected AND nothing else does.
// Drop or weaken an annotation in `mutex.h` and the build fails - that is the guard.
// (The marker syntax is omitted from this header so it is only recognized on its real line.)
//
// Compiled in ON mode only (`OME_THREAD_SAFETY`);
// in OFF mode the annotations vanish, so the file is excluded from OFF builds.
//
// Taxonomy of pins
//
//   The common guards (`LockGuard`, `SharedLockGuard`, `ScopedLock`) expose only
//   the immediate-acquire scoped shape: explicit (M&) ctor + dtor, and nothing
//   else - no manual Lock/Unlock, no defer/try/adopt ctors, no release(), no
//   owns_lock(), no move. (`ReleasableLockGuard`/`ReleasableSharedLockGuard` are
//   the one opt-in exception, adding an explicit `Release()`.)
//
//   Each pin is indexed by a letter. The letters are a logical index, NOT the
//   physical order in the file: the static_assert block deliberately comes first,
//   so on a top-to-bottom read the letters appear as E, B, C, then A, D, F, G, H, I.
//   The pins fall into two blocks:
//
//   1. Compile-time `static_assert` pins (resolved during compilation, before the
//      `-verify` pass even runs):
//        (E) Guard API-absence lockdown - the shrunk guard API must not regrow a
//            defer/try/adopt ctor, move support, a default ctor, or
//            release()/owns_lock()/try_lock()/mutex(); the Releasable pair must
//            keep `Release()`. Each is pinned by a negative `is_constructible_v` /
//            member-detection trait.                                  [policy pin]
//        (B) Friend-only `NativeHandle()` lockdown - `NativeHandle()` must stay
//            `private` (friend-only) on every wrapper mutex, so non-friend code
//            cannot reach the underlying `std::` handle.               [policy pin]
//        (C) `ConditionVariable` wait-API narrowing - all six wait overloads must
//            accept `ov::LockGuard<ov::Mutex>` and reject every other lock type.
//                                                                      [policy pin]
//
//   2. Clang `-verify` diagnostic pins (each marks an intentionally-incorrect
//      access with the diagnostic that must fire on that line; the compile passes
//      only when every marked diagnostic fires AND nothing else does):
//        (A) Immediate-acquire happy path + baseline (exclusive and shared):
//            guarded access under the guard is accepted; access with no lock is
//            diagnosed.                                           [correctness pin]
//        (D) CV wait + `OV_REQUIRES` predicate lambda: no diagnostic (verifies the
//            lambda-requires annotation).                         [correctness pin]
//        (F) Held-set hygiene on the explicit raw-mutex method effects (the
//            pure-RAII guards have no manual methods). Rows F1-F8 cover the 12
//            manual methods - `ov::Mutex`/`ov::RecursiveMutex`/`ov::SharedMutex`
//            `Lock`/`Unlock`/`TryLock`, plus `ov::SharedMutex`
//            `LockShared`/`UnlockShared`/`TryLockShared` - so sabotaging any
//            `OV_ACQUIRE`/`OV_RELEASE`/`OV_TRY_ACQUIRE` effect trips at least one
//            row.                                                 [correctness pin]
//        (G) `ov::ScopedLock` held-set coverage (rows G1-G3).      [correctness pin]
//        (H) `ov::LockGuard` held-set coverage.                    [correctness pin]
//        (I) Releasable guards held-set coverage.                  [correctness pin]
//
//   What a failure means
//       - Correctness pins (A, D, F, G, H, I): a TSA annotation
//         (`OV_ACQUIRE`/`OV_RELEASE`/`OV_GUARDED_BY`/scoped-capability) was dropped
//         or widened, so code with a real data race would now compile clean.
//         A failure here is a bug.
//       - Policy pins (B, C, E): the wrapper API grew or shrank - a deliberate
//         design decision to revisit, not a latent race. A failure here flags a
//         policy change for review, not a correctness regression.
//
// Diagnostic messages are written to match Clang's exact text.
// If Clang ever rewords them the test correctly fails - that is the signal to refresh
// the expectations alongside the upstream change.

#include <base/ovlibrary/tsa/mutex.h>

#include <mutex>
#include <type_traits>

// ----------------------------------------------------------------------------
// (E) API-absence lockdown [policy pin: API-shrink choice, not correctness]
//
// Each `static_assert(!is_constructible_v<...>)` row pins that a specific entry point
// on the shrunk wrapper does not exist.
// If a future change re-adds (for example) the `defer_lock` constructor
// on `ov::LockGuard<ov::Mutex>`, the matching row fails immediately with a `static assertion
// failed` error.
// The `is_constructible_v` row checks the constructor signature;
// the move-trait rows pin the absence of move support;
// `is_default_constructible_v` pins the absence of the empty-state constructor.
//
// Sabotage check: temporarily restore one of the removed ctors
// (for example `LockGuard(M&, std::defer_lock_t)`) in `mutex.h` and the matching row below
// changes outcome (the negation flips), which fails the static_assert.
// That confirms the lockdown actually catches a regression.
// ----------------------------------------------------------------------------

// Exclusive guard.
static_assert(std::is_constructible_v<ov::LockGuard<ov::Mutex>, ov::Mutex &>,
			  "ov::LockGuard must keep the (M&) immediate-acquire constructor");
static_assert(std::is_default_constructible_v<ov::LockGuard<ov::Mutex>> == false,
			  "ov::LockGuard must not be default-constructible "
			  "(scoped guard - no empty state)");
static_assert(std::is_constructible_v<ov::LockGuard<ov::Mutex>,
									  ov::Mutex &, std::defer_lock_t> == false,
			  "ov::LockGuard must not expose the (M&, std::defer_lock_t) ctor");
static_assert(std::is_constructible_v<ov::LockGuard<ov::Mutex>,
									  ov::Mutex &, std::try_to_lock_t> == false,
			  "ov::LockGuard must not expose the (M&, std::try_to_lock_t) ctor");
static_assert(std::is_constructible_v<ov::LockGuard<ov::Mutex>,
									  ov::Mutex &, std::adopt_lock_t> == false,
			  "ov::LockGuard must not expose the (M&, std::adopt_lock_t) ctor");
static_assert(std::is_move_constructible_v<ov::LockGuard<ov::Mutex>> == false,
			  "ov::LockGuard must not be move-constructible");
static_assert(std::is_move_assignable_v<ov::LockGuard<ov::Mutex>> == false,
			  "ov::LockGuard must not be move-assignable");
static_assert(std::is_copy_constructible_v<ov::LockGuard<ov::Mutex>> == false,
			  "ov::LockGuard must not be copy-constructible");
static_assert(std::is_copy_assignable_v<ov::LockGuard<ov::Mutex>> == false,
			  "ov::LockGuard must not be copy-assignable");

// Shared guard.
static_assert(std::is_constructible_v<ov::SharedLockGuard<ov::SharedMutex>,
									  ov::SharedMutex &>,
			  "ov::SharedLockGuard must keep the (M&) immediate-acquire constructor");
static_assert(std::is_default_constructible_v<ov::SharedLockGuard<ov::SharedMutex>> == false,
			  "ov::SharedLockGuard must not be default-constructible");
static_assert(std::is_constructible_v<ov::SharedLockGuard<ov::SharedMutex>,
									  ov::SharedMutex &, std::defer_lock_t> == false,
			  "ov::SharedLockGuard must not expose the (M&, std::defer_lock_t) ctor");
static_assert(std::is_constructible_v<ov::SharedLockGuard<ov::SharedMutex>,
									  ov::SharedMutex &, std::try_to_lock_t> == false,
			  "ov::SharedLockGuard must not expose the (M&, std::try_to_lock_t) ctor");
static_assert(std::is_constructible_v<ov::SharedLockGuard<ov::SharedMutex>,
									  ov::SharedMutex &, std::adopt_lock_t> == false,
			  "ov::SharedLockGuard must not expose the (M&, std::adopt_lock_t) ctor");
static_assert(std::is_move_constructible_v<ov::SharedLockGuard<ov::SharedMutex>> == false,
			  "ov::SharedLockGuard must not be move-constructible");
static_assert(std::is_move_assignable_v<ov::SharedLockGuard<ov::SharedMutex>> == false,
			  "ov::SharedLockGuard must not be move-assignable");
static_assert(std::is_copy_constructible_v<ov::SharedLockGuard<ov::SharedMutex>> == false,
			  "ov::SharedLockGuard must not be copy-constructible");
static_assert(std::is_copy_assignable_v<ov::SharedLockGuard<ov::SharedMutex>> == false,
			  "ov::SharedLockGuard must not be copy-assignable");

// Releasable guards: same shrunk API as the plain guards (only the
// (M&) immediate-acquire ctor;
// no defer/try/adopt ctor, no move/copy/default).
// The only intentional addition is `Release()` (pinned below).
static_assert(std::is_constructible_v<ov::ReleasableLockGuard<ov::Mutex>, ov::Mutex &>,
			  "ov::ReleasableLockGuard must keep the (M&) immediate-acquire constructor");
static_assert(std::is_default_constructible_v<ov::ReleasableLockGuard<ov::Mutex>> == false,
			  "ov::ReleasableLockGuard must not be default-constructible");
static_assert(std::is_constructible_v<ov::ReleasableLockGuard<ov::Mutex>, ov::Mutex &, std::defer_lock_t> == false,
			  "ov::ReleasableLockGuard must not expose the (M&, std::defer_lock_t) ctor");
static_assert(std::is_constructible_v<ov::ReleasableLockGuard<ov::Mutex>, ov::Mutex &, std::try_to_lock_t> == false,
			  "ov::ReleasableLockGuard must not expose the (M&, std::try_to_lock_t) ctor");
static_assert(std::is_constructible_v<ov::ReleasableLockGuard<ov::Mutex>, ov::Mutex &, std::adopt_lock_t> == false,
			  "ov::ReleasableLockGuard must not expose the (M&, std::adopt_lock_t) ctor");
static_assert(std::is_move_constructible_v<ov::ReleasableLockGuard<ov::Mutex>> == false,
			  "ov::ReleasableLockGuard must not be move-constructible");
static_assert(std::is_move_assignable_v<ov::ReleasableLockGuard<ov::Mutex>> == false,
			  "ov::ReleasableLockGuard must not be move-assignable");
static_assert(std::is_copy_constructible_v<ov::ReleasableLockGuard<ov::Mutex>> == false,
			  "ov::ReleasableLockGuard must not be copy-constructible");
static_assert(std::is_copy_assignable_v<ov::ReleasableLockGuard<ov::Mutex>> == false,
			  "ov::ReleasableLockGuard must not be copy-assignable");

static_assert(std::is_constructible_v<ov::ReleasableSharedLockGuard<ov::SharedMutex>, ov::SharedMutex &>,
			  "ov::ReleasableSharedLockGuard must keep the (M&) immediate-acquire constructor");
static_assert(std::is_default_constructible_v<ov::ReleasableSharedLockGuard<ov::SharedMutex>> == false,
			  "ov::ReleasableSharedLockGuard must not be default-constructible");
static_assert(std::is_constructible_v<ov::ReleasableSharedLockGuard<ov::SharedMutex>, ov::SharedMutex &, std::defer_lock_t> == false,
			  "ov::ReleasableSharedLockGuard must not expose the (M&, std::defer_lock_t) ctor");
static_assert(std::is_constructible_v<ov::ReleasableSharedLockGuard<ov::SharedMutex>, ov::SharedMutex &, std::try_to_lock_t> == false,
			  "ov::ReleasableSharedLockGuard must not expose the (M&, std::try_to_lock_t) ctor");
static_assert(std::is_constructible_v<ov::ReleasableSharedLockGuard<ov::SharedMutex>, ov::SharedMutex &, std::adopt_lock_t> == false,
			  "ov::ReleasableSharedLockGuard must not expose the (M&, std::adopt_lock_t) ctor");
static_assert(std::is_move_constructible_v<ov::ReleasableSharedLockGuard<ov::SharedMutex>> == false,
			  "ov::ReleasableSharedLockGuard must not be move-constructible");
static_assert(std::is_move_assignable_v<ov::ReleasableSharedLockGuard<ov::SharedMutex>> == false,
			  "ov::ReleasableSharedLockGuard must not be move-assignable");
static_assert(std::is_copy_constructible_v<ov::ReleasableSharedLockGuard<ov::SharedMutex>> == false,
			  "ov::ReleasableSharedLockGuard must not be copy-constructible");
static_assert(std::is_copy_assignable_v<ov::ReleasableSharedLockGuard<ov::SharedMutex>> == false,
			  "ov::ReleasableSharedLockGuard must not be copy-assignable");

// Detection idiom: probe whether the guard exposes a `release()`/`owns_lock()` /
// `try_lock()` member.
// The wrapper must not have any of them.
// We use SFINAE via `std::void_t`: the primary template resolves to `std::false_type`;
// a specialization that succeeds only when the member exists resolves to `std::true_type`.
namespace
{
	template <class, class = void>
	struct has_release : std::false_type
	{
	};
	template <class T>
	struct has_release<T, std::void_t<decltype(std::declval<T &>().release())>>
		: std::true_type
	{
	};

	template <class, class = void>
	struct has_owns_lock : std::false_type
	{
	};
	template <class T>
	struct has_owns_lock<T, std::void_t<decltype(std::declval<T &>().owns_lock())>>
		: std::true_type
	{
	};

	template <class, class = void>
	struct has_try_lock : std::false_type
	{
	};
	template <class T>
	struct has_try_lock<T, std::void_t<decltype(std::declval<T &>().try_lock())>>
		: std::true_type
	{
	};

	template <class, class = void>
	struct has_mutex_accessor : std::false_type
	{
	};
	template <class T>
	struct has_mutex_accessor<T, std::void_t<decltype(std::declval<T &>().mutex())>>
		: std::true_type
	{
	};

	// Probe whether `NativeHandle()` is reachable from non-friend code.
	// The mutex wrappers keep `NativeHandle()` `private` with only `ScopedLock` /
	// `ConditionVariable` as friends, so this struct
	// (a non-friend) must see substitution failure - access control is part of the immediate
	// SFINAE context in C++17 - and resolve to `false_type`.
	// If a future change makes `NativeHandle()` public again,
	// the probe flips to `true_type` and the reject asserts below fail,
	// re-closing the raw-handle escape hatch.
	template <class, class = void>
	struct has_native_handle : std::false_type
	{
	};
	template <class T>
	struct has_native_handle<T, std::void_t<decltype(std::declval<T &>().NativeHandle())>>
		: std::true_type
	{
	};

	// Probe for the PascalCase `Release()` (the releasable guards' one intentional extra method).
	// Used to pin that it stays present.
	template <class, class = void>
	struct has_release_method : std::false_type
	{
	};
	template <class T>
	struct has_release_method<T, std::void_t<decltype(std::declval<T &>().Release())>>
		: std::true_type
	{
	};

	// `ov::ConditionVariable` exposes six wait overloads,
	// each taking the lock by the concrete `ov::LockGuard<ov::Mutex>` type.
	// A separate SFINAE probe per overload is required: probing only `Wait(lock)` would leave
	// the predicate/timeout overloads free to be widened back to a permissive `template
	// <class M> (LockGuard<M> &, ...)`
	// form without tripping any guard, silently re-opening the recursive/shared mutex
	// regression on those paths.
	//
	// The probes feed concrete predicate/duration/time_point arguments so each overload
	// is exercised on its real signature;
	// the lock type `T` is the only varying axis.
	template <class, class = void>
	struct cv_wait_accepts : std::false_type
	{
	};
	template <class T>
	struct cv_wait_accepts<T, std::void_t<decltype(std::declval<ov::ConditionVariable &>().Wait(std::declval<T &>()))>>
		: std::true_type
	{
	};

	template <class, class = void>
	struct cv_wait_pred_accepts : std::false_type
	{
	};
	template <class T>
	struct cv_wait_pred_accepts<T, std::void_t<decltype(std::declval<ov::ConditionVariable &>().Wait(std::declval<T &>(), std::declval<bool (*)()>()))>>
		: std::true_type
	{
	};

	template <class, class = void>
	struct cv_wait_for_accepts : std::false_type
	{
	};
	template <class T>
	struct cv_wait_for_accepts<T, std::void_t<decltype(std::declval<ov::ConditionVariable &>().WaitFor(std::declval<T &>(), std::declval<std::chrono::milliseconds>()))>>
		: std::true_type
	{
	};

	template <class, class = void>
	struct cv_wait_for_pred_accepts : std::false_type
	{
	};
	template <class T>
	struct cv_wait_for_pred_accepts<T, std::void_t<decltype(std::declval<ov::ConditionVariable &>().WaitFor(std::declval<T &>(), std::declval<std::chrono::milliseconds>(), std::declval<bool (*)()>()))>>
		: std::true_type
	{
	};

	template <class, class = void>
	struct cv_wait_until_accepts : std::false_type
	{
	};
	template <class T>
	struct cv_wait_until_accepts<T, std::void_t<decltype(std::declval<ov::ConditionVariable &>().WaitUntil(std::declval<T &>(), std::declval<std::chrono::steady_clock::time_point>()))>>
		: std::true_type
	{
	};

	template <class, class = void>
	struct cv_wait_until_pred_accepts : std::false_type
	{
	};
	template <class T>
	struct cv_wait_until_pred_accepts<T, std::void_t<decltype(std::declval<ov::ConditionVariable &>().WaitUntil(std::declval<T &>(), std::declval<std::chrono::steady_clock::time_point>(), std::declval<bool (*)()>()))>>
		: std::true_type
	{
	};

	// `true` only when every one of the six wait overloads accepts `T`.
	template <class T>
	inline constexpr bool cv_all_wait_overloads_accept =
		cv_wait_accepts<T>::value &&
		cv_wait_pred_accepts<T>::value &&
		cv_wait_for_accepts<T>::value &&
		cv_wait_for_pred_accepts<T>::value &&
		cv_wait_until_accepts<T>::value &&
		cv_wait_until_pred_accepts<T>::value;

	// `true` when any of the six wait overloads accepts `T`.
	// A rejected lock type must make this `false` - i.e. no overload may sneak the type through.
	template <class T>
	inline constexpr bool cv_any_wait_overload_accepts =
		cv_wait_accepts<T>::value ||
		cv_wait_pred_accepts<T>::value ||
		cv_wait_for_accepts<T>::value ||
		cv_wait_for_pred_accepts<T>::value ||
		cv_wait_until_accepts<T>::value ||
		cv_wait_until_pred_accepts<T>::value;
}  // namespace

static_assert(has_release<ov::LockGuard<ov::Mutex>>::value == false,
			  "ov::LockGuard must not expose release()");
static_assert(has_owns_lock<ov::LockGuard<ov::Mutex>>::value == false,
			  "ov::LockGuard must not expose owns_lock()");
static_assert(has_try_lock<ov::LockGuard<ov::Mutex>>::value == false,
			  "ov::LockGuard must not expose try_lock()");
static_assert(has_mutex_accessor<ov::LockGuard<ov::Mutex>>::value == false,
			  "ov::LockGuard must not expose mutex()");

static_assert(has_release<ov::SharedLockGuard<ov::SharedMutex>>::value == false,
			  "ov::SharedLockGuard must not expose release()");
static_assert(has_owns_lock<ov::SharedLockGuard<ov::SharedMutex>>::value == false,
			  "ov::SharedLockGuard must not expose owns_lock()");
static_assert(has_try_lock<ov::SharedLockGuard<ov::SharedMutex>>::value == false,
			  "ov::SharedLockGuard must not expose try_lock()");
static_assert(has_mutex_accessor<ov::SharedLockGuard<ov::SharedMutex>>::value == false,
			  "ov::SharedLockGuard must not expose mutex()");

// Releasable guards: no std-style dynamic-ownership API either,
// but the PascalCase `Release()` MUST stay present (that is their whole reason to exist).
static_assert(has_release<ov::ReleasableLockGuard<ov::Mutex>>::value == false,
			  "ov::ReleasableLockGuard must not expose std-style release()");
static_assert(has_owns_lock<ov::ReleasableLockGuard<ov::Mutex>>::value == false,
			  "ov::ReleasableLockGuard must not expose owns_lock()");
static_assert(has_try_lock<ov::ReleasableLockGuard<ov::Mutex>>::value == false,
			  "ov::ReleasableLockGuard must not expose try_lock()");
static_assert(has_mutex_accessor<ov::ReleasableLockGuard<ov::Mutex>>::value == false,
			  "ov::ReleasableLockGuard must not expose mutex()");
static_assert(has_release_method<ov::ReleasableLockGuard<ov::Mutex>>::value,
			  "ov::ReleasableLockGuard must keep Release()");
static_assert(has_release<ov::ReleasableSharedLockGuard<ov::SharedMutex>>::value == false,
			  "ov::ReleasableSharedLockGuard must not expose std-style release()");
static_assert(has_owns_lock<ov::ReleasableSharedLockGuard<ov::SharedMutex>>::value == false,
			  "ov::ReleasableSharedLockGuard must not expose owns_lock()");
static_assert(has_try_lock<ov::ReleasableSharedLockGuard<ov::SharedMutex>>::value == false,
			  "ov::ReleasableSharedLockGuard must not expose try_lock()");
static_assert(has_mutex_accessor<ov::ReleasableSharedLockGuard<ov::SharedMutex>>::value == false,
			  "ov::ReleasableSharedLockGuard must not expose mutex()");
static_assert(has_release_method<ov::ReleasableSharedLockGuard<ov::SharedMutex>>::value,
			  "ov::ReleasableSharedLockGuard must keep Release()");

// ----------------------------------------------------------------------------
// (B) Friend-only `NativeHandle()` lockdown. [policy pin]
//
// `NativeHandle()` must stay `private` (friend-only) on every wrapper mutex, so
// non-friend code cannot reach the underlying `std::` mutex (the raw-handle
// escape hatch). Without this, re-exposing `NativeHandle()` as public would
// silently re-open the escape and no other assert here would notice.
// ----------------------------------------------------------------------------
static_assert(has_native_handle<ov::Mutex>::value == false,
			  "ov::Mutex::NativeHandle() must not be reachable by non-friend code");
static_assert(has_native_handle<ov::RecursiveMutex>::value == false,
			  "ov::RecursiveMutex::NativeHandle() must not be reachable by non-friend code");
static_assert(has_native_handle<ov::SharedMutex>::value == false,
			  "ov::SharedMutex::NativeHandle() must not be reachable by non-friend code");
// ----------------------------------------------------------------------------
// (C) `ov::ConditionVariable` wait-API narrowing lockdown. [policy pin]
//
// The CV intentionally narrows the lock parameter of all six wait overloads
// (`Wait`, `Wait` + predicate, `WaitFor` with/without predicate,
// `WaitUntil` with/without predicate) to the concrete `ov::LockGuard<ov::Mutex>` type only.
//
// The accept assertion requires every overload to accept `ov::LockGuard<ov::Mutex>`;
// the reject assertions require that no overload accepts any other lock type.
// Probing all six overloads (not just `Wait(lock)`) closes the gap where a future change could
// widen only the predicate/timeout overloads and silently re-admit some other lock type
// on those paths.
static_assert(cv_all_wait_overloads_accept<ov::LockGuard<ov::Mutex>>,
			  "every ov::ConditionVariable wait overload must accept ov::LockGuard<ov::Mutex>");
// Every other lock type must be rejected.
// `ov::LockGuard` is the generic exclusive guard,
// so `ov::LockGuard<ov::RecursiveMutex>`/`<ov::SharedMutex>` ARE instantiable - but the CV
// wait overloads bind the concrete `ov::LockGuard<ov::Mutex>` parameter,
// so those distinct instantiations (and the shared guard,
// and the std lock types) must not bind to any overload.
// If a future change widened a wait overload back to a permissive `template <class M>
// (LockGuard<M> &, ...)`
// form, one of these reject rows would flip to `true` and fail.
static_assert(cv_any_wait_overload_accepts<ov::LockGuard<ov::RecursiveMutex>> == false,
			  "no ov::ConditionVariable wait overload may accept ov::LockGuard<ov::RecursiveMutex>");
static_assert(cv_any_wait_overload_accepts<ov::LockGuard<ov::SharedMutex>> == false,
			  "no ov::ConditionVariable wait overload may accept ov::LockGuard<ov::SharedMutex>");
static_assert(cv_any_wait_overload_accepts<ov::SharedLockGuard<ov::SharedMutex>> == false,
			  "no ov::ConditionVariable wait overload may accept ov::SharedLockGuard");
static_assert(cv_any_wait_overload_accepts<std::unique_lock<std::mutex>> == false,
			  "no ov::ConditionVariable wait overload may accept std::unique_lock");
static_assert(cv_any_wait_overload_accepts<std::lock_guard<std::mutex>> == false,
			  "no ov::ConditionVariable wait overload may accept std::lock_guard");

// ----------------------------------------------------------------------------
// (A) Immediate-acquire happy path + baseline (exclusive)
// ----------------------------------------------------------------------------

namespace
{
	// Row 1: immediate lock + protected access - no diagnostic expected.
	class LockGuardImmediateOk
	{
	public:
		int Read()
		{
			ov::LockGuard lk(_mutex);
			return _value;
		}

	private:
		ov::Mutex _mutex;
		int _value OV_GUARDED_BY(_mutex) = 0;
	};

	// Row 3: no lock at all - the canonical TSA diagnostic.
	// Guards the baseline behavior against an accidental NTSA being widened to the whole guard class.
	class LockGuardNoLockBad
	{
	public:
		int Bad()
		{
			return _value;	// expected-warning {{reading variable '_value' requires holding mutex '_mutex'}}
		}

	private:
		ov::Mutex _mutex;
		int _value OV_GUARDED_BY(_mutex) = 0;
	};
}  // namespace

// ----------------------------------------------------------------------------
// (A) Immediate-acquire happy path + baseline (shared)
// ----------------------------------------------------------------------------

namespace
{
	// Shared row 1: immediate shared acquire + protected read.
	class SharedImmediateOk
	{
	public:
		int Read()
		{
			ov::SharedLockGuard lk(_mutex);
			return _value;
		}

	private:
		ov::SharedMutex _mutex;
		int _value OV_GUARDED_BY(_mutex) = 0;
	};

}  // namespace

// ----------------------------------------------------------------------------
// (D) `ov::ConditionVariable` regression - the transient `cv.Wait(lock)`
// unlock/re-lock must still leave the analyzer convinced the lock is held after the wait
// returns, and an `OV_REQUIRES` predicate lambda reading an `OV_GUARDED_BY` member must compile cleanly.
// ----------------------------------------------------------------------------

namespace
{
	class CvWaitOk
	{
	public:
		void Run()
		{
			ov::LockGuard lk(_mutex);
			_cv.Wait(lk, [&]() OV_REQUIRES(_mutex) { return (_ready); });
			_value = 1;
		}

	private:
		ov::Mutex _mutex;
		ov::ConditionVariable _cv;
		bool _ready OV_GUARDED_BY(_mutex) = false;
		int _value OV_GUARDED_BY(_mutex)  = 0;
	};
}  // namespace

// ----------------------------------------------------------------------------
// (F) Held-set hygiene on the explicit method effects. [correctness pin]
//
// The (A)/(G)/(H) rows only catch sabotage of the scoped-guard ctor/dtor effects
// (`LockGuard(M&) OV_ACQUIRE(m)` and `~LockGuard() OV_RELEASE()`).
// Dropping any of the 12 raw-mutex method-level effects listed in the matrix header would
// otherwise slip through: those rows would still see the held-set transition at the ctor /
// dtor, so they keep passing even when a raw mutex method has lost its annotation.
//
// Each class below interleaves a "happy" access
// (no expected diagnostic marker) with a "sabotage trip-wire" access
// (with an `expected-warning` marker).
// The pattern catches both directions of sabotage:
//
//   - Drop `OV_ACQUIRE` on `Lock()` -> the post-`Lock()` access
//     becomes unguarded -> a diagnostic fires on a line without a
//     marker -> `-verify` fails with "no expectation".
//   - Drop `OV_RELEASE` on `Unlock()` -> the post-`Unlock()` access
//     stays "held" in the analyzer's view -> the marker on that
//     line does not fire -> `-verify` fails with "expected
//     diagnostic not produced".
//   - Drop `OV_TRY_ACQUIRE(true)` on `TryLock()` -> the access
//     inside the `if (TryLock())` branch becomes unguarded -> a
//     diagnostic fires on a line without a marker -> `-verify`
//     fails with "no expectation".
//
// All `Probe` methods below are self-balanced
// (the analyzer's view of the held set is identical at entry and exit) so the function
// boundary stays consistent and the analyzer does not emit extra "mutex still held"
// diagnostics that would confuse `-verify`.
// ----------------------------------------------------------------------------

namespace
{
	// Row F1: raw `ov::Mutex::Lock()`/`Unlock()` sequence.
	// Catches sabotage of `Mutex::Lock()` `OV_ACQUIRE()` and `Mutex::Unlock()` `OV_RELEASE()`.
	// The sequence ends balanced (`Lock` paired with `Unlock`) so the function boundary stays clean.
	class RawMutexLockUnlockSequence
	{
	public:
		void Probe()
		{
			_mutex.Lock();
			_value = 1;	 // held by Lock() acquire - no diagnostic
			_mutex.Unlock();
			_value = 1;	 // expected-warning {{writing variable '_value' requires holding mutex '_mutex' exclusively}}
			_mutex.Lock();
			_value = 1;	 // re-acquired - no diagnostic
			_mutex.Unlock();
		}

	private:
		ov::Mutex _mutex;
		int _value OV_GUARDED_BY(_mutex) = 0;
	};

	// Row F2: raw `ov::Mutex::TryLock()` success path.
	// Catches sabotage of `Mutex::TryLock()` `OV_TRY_ACQUIRE(true)`.
	class RawMutexTryLockSequence
	{
	public:
		void Probe()
		{
			if (_mutex.TryLock())
			{
				_value = 1;	 // held by TryLock() success - no diagnostic
				_mutex.Unlock();
			}
		}

	private:
		ov::Mutex _mutex;
		int _value OV_GUARDED_BY(_mutex) = 0;
	};

	// Row F3: raw `ov::RecursiveMutex::Lock()`/`Unlock()` sequence.
	// Catches sabotage of `RecursiveMutex::Lock()` `OV_ACQUIRE()`
	// and `RecursiveMutex::Unlock()` `OV_RELEASE()`.
	class RawRecursiveMutexLockUnlockSequence
	{
	public:
		void Probe()
		{
			_mutex.Lock();
			_value = 1;	 // held by Lock() acquire - no diagnostic
			_mutex.Unlock();
			_value = 1;	 // expected-warning {{writing variable '_value' requires holding mutex '_mutex' exclusively}}
			_mutex.Lock();
			_value = 1;	 // re-acquired - no diagnostic
			_mutex.Unlock();
		}

	private:
		ov::RecursiveMutex _mutex;
		int _value OV_GUARDED_BY(_mutex) = 0;
	};

	// Row F4: raw `ov::RecursiveMutex::TryLock()` success path.
	// Catches sabotage of `RecursiveMutex::TryLock()` `OV_TRY_ACQUIRE(true)`.
	class RawRecursiveMutexTryLockSequence
	{
	public:
		void Probe()
		{
			if (_mutex.TryLock())
			{
				_value = 1;	 // held by TryLock() success - no diagnostic
				_mutex.Unlock();
			}
		}

	private:
		ov::RecursiveMutex _mutex;
		int _value OV_GUARDED_BY(_mutex) = 0;
	};

	// Row F5: raw `ov::SharedMutex::Lock()`/`Unlock()` exclusive sequence.
	// Catches sabotage of `SharedMutex::Lock()` `OV_ACQUIRE()` and `SharedMutex::Unlock()` `OV_RELEASE()`.
	class RawSharedMutexLockUnlockSequence
	{
	public:
		void Probe()
		{
			_mutex.Lock();
			_value = 1;	 // held exclusively by Lock() - no diagnostic
			_mutex.Unlock();
			_value = 1;	 // expected-warning {{writing variable '_value' requires holding mutex '_mutex' exclusively}}
			_mutex.Lock();
			_value = 1;	 // re-acquired - no diagnostic
			_mutex.Unlock();
		}

	private:
		ov::SharedMutex _mutex;
		int _value OV_GUARDED_BY(_mutex) = 0;
	};

	// Row F6: raw `ov::SharedMutex::TryLock()` success path.
	// Catches sabotage of `SharedMutex::TryLock()` `OV_TRY_ACQUIRE(true)`.
	class RawSharedMutexTryLockSequence
	{
	public:
		void Probe()
		{
			if (_mutex.TryLock())
			{
				_value = 1;	 // held exclusively by TryLock() - no diagnostic
				_mutex.Unlock();
			}
		}

	private:
		ov::SharedMutex _mutex;
		int _value OV_GUARDED_BY(_mutex) = 0;
	};

	// Row F7: raw `ov::SharedMutex::LockShared()`/`UnlockShared()` sequence.
	// Catches sabotage of `SharedMutex::LockShared()` `OV_ACQUIRE_SHARED()`
	// and `SharedMutex::UnlockShared()` `OV_RELEASE_SHARED()`.
	class RawSharedMutexLockSharedSequence
	{
	public:
		void Probe()
		{
			_mutex.LockShared();
			int v = _value;	 // held in shared mode by LockShared() - no diagnostic
			(void)v;
			_mutex.UnlockShared();
			v = _value;	 // expected-warning {{reading variable '_value' requires holding mutex '_mutex'}}
			(void)v;
			_mutex.LockShared();
			v = _value;	 // re-acquired - no diagnostic
			(void)v;
			_mutex.UnlockShared();
		}

	private:
		ov::SharedMutex _mutex;
		int _value OV_GUARDED_BY(_mutex) = 0;
	};

	// Row F8: raw `ov::SharedMutex::TryLockShared()` success path.
	// Catches sabotage of `SharedMutex::TryLockShared()` `OV_TRY_ACQUIRE_SHARED(true)`.
	class RawSharedMutexTryLockSharedSequence
	{
	public:
		void Probe()
		{
			if (_mutex.TryLockShared())
			{
				int v = _value;	 // held in shared mode by TryLockShared() - no diagnostic
				(void)v;
				_mutex.UnlockShared();
			}
		}

	private:
		ov::SharedMutex _mutex;
		int _value OV_GUARDED_BY(_mutex) = 0;
	};
}  // namespace

// ----------------------------------------------------------------------------
// (G) ov::ScopedLock held-set coverage. [correctness pin]
//
// ScopedLock acquires its mutexes through std-API handles
// (`NativeHandle()`) that Clang TSA does not track;
// the analyzer-visible effect is the declared `OV_ACQUIRE(ms...)`
// on the constructor and `OV_RELEASE()` on the destructor.
// These rows pin that the declaration is honoured: an access guarded by a mutex the ScopedLock
// holds is accepted, and an access guarded by a mutex it does NOT hold is diagnosed.
// Without them the ScopedLock acquire effect could be dropped or narrowed without any
// `-verify` failure.
// ----------------------------------------------------------------------------

namespace
{
	// Row G1: ScopedLock over (_m1, _m2);
	// access to data guarded by either held mutex is accepted - no diagnostic.
	class ScopedLockCoversHeld
	{
	public:
		void Ok()
		{
			ov::ScopedLock lk(_m1, _m2);
			_v1 = 1;  // held by _m1 via ScopedLock - no diagnostic
			_v2 = 1;  // held by _m2 via ScopedLock - no diagnostic
		}

	private:
		ov::Mutex _m1;
		ov::Mutex _m2;
		int _v1 OV_GUARDED_BY(_m1) = 0;
		int _v2 OV_GUARDED_BY(_m2) = 0;
	};

	// Row G2: ScopedLock over (_m1, _m2);
	// access to data guarded by a third mutex it does NOT hold must diagnose.
	// If the ScopedLock `OV_ACQUIRE(ms...)`
	// effect is dropped or narrowed, this access stops diagnosing and `-verify` fails
	// with "expected diagnostic not produced".
	class ScopedLockUncoveredBad
	{
	public:
		void Bad()
		{
			ov::ScopedLock lk(_m1, _m2);
			_v3 = 1;  // expected-warning {{writing variable '_v3' requires holding mutex '_m3' exclusively}}
		}

	private:
		ov::Mutex _m1;
		ov::Mutex _m2;
		ov::Mutex _m3;
		int _v3 OV_GUARDED_BY(_m3) = 0;
	};

	// Row G3: ScopedLock releases on destruction.
	// A guarded access AFTER the guard's scope must diagnose again,
	// which pins `~ScopedLock() OV_RELEASE()`.
	// If the destructor's release effect is dropped or weakened,
	// `_m1` stays held in the analyzer's view and the post-scope access stops diagnosing,
	// failing `-verify` with "expected diagnostic not produced".
	class ScopedLockReleasesOnDtor
	{
	public:
		void Probe()
		{
			{
				ov::ScopedLock lk(_m1, _m2);
				_v1 = 1;  // held by ScopedLock - no diagnostic
			}
			// lk destroyed above;
			// _m1/_m2 no longer held.
			_v1 = 2;  // expected-warning {{writing variable '_v1' requires holding mutex '_m1' exclusively}}
		}

	private:
		ov::Mutex _m1;
		ov::Mutex _m2;
		int _v1 OV_GUARDED_BY(_m1) = 0;
	};
}  // namespace

// ----------------------------------------------------------------------------
// (H) ov::LockGuard held-set coverage. [correctness pin]
//
// LockGuard is a pure RAII guard (no manual Lock/Unlock), so the
// (A)/(F) rows - which exercise the explicit drop/re-acquire methods - never touch it.
// This row pins both of its scoped-capability effects in one function:
//   - ctor `OV_ACQUIRE(m)` dropped -> the in-scope access becomes unguarded ->
//     a diagnostic fires on a line WITHOUT a marker -> `-verify` fails with
//     "no expectation".
//   - dtor `OV_RELEASE()` dropped -> after the inner scope the analyzer still
//     considers `_mutex` held -> the marked post-scope access stops diagnosing ->
//     `-verify` fails with "expected diagnostic not produced".
// ----------------------------------------------------------------------------

namespace
{
	class LockGuardHeldSet
	{
	public:
		void Probe()
		{
			{
				ov::LockGuard lk(_mutex);
				_value = 1;	 // held by ctor acquire - no diagnostic
			}
			// lk destroyed above;
			// _mutex no longer held.
			_value = 2;	 // expected-warning {{writing variable '_value' requires holding mutex '_mutex' exclusively}}
		}

	private:
		ov::Mutex _mutex;
		int _value OV_GUARDED_BY(_mutex) = 0;
	};
}  // namespace

// ----------------------------------------------------------------------------
// (I) ov::ReleasableLockGuard/ReleasableSharedLockGuard held-set. [correctness pin]
//
// The releasable guards add an explicit `Release()` annotated `OV_RELEASE()`.
// These rows pin that the early release is tracked: access before `Release()` is accepted,
// access after it is diagnosed (lock no longer held),
// and the destructor after `Release()` does NOT emit a spurious "releasing mutex
// that was not held" diagnostic.
// If `Release()` lost its `OV_RELEASE()` effect the post-release access would stop diagnosing
// and `-verify` would fail.
// ----------------------------------------------------------------------------

namespace
{
	class ReleasableEarlyRelease
	{
	public:
		void Probe()
		{
			ov::ReleasableLockGuard lk(_mutex);
			_value = 1;	 // held by ctor acquire - no diagnostic
			lk.Release();
			_value = 2;	 // expected-warning {{writing variable '_value' requires holding mutex '_mutex' exclusively}}
		}

	private:
		ov::Mutex _mutex;
		int _value OV_GUARDED_BY(_mutex) = 0;
	};

	class ReleasableSharedEarlyRelease
	{
	public:
		int Probe()
		{
			ov::ReleasableSharedLockGuard lk(_mutex);
			int v = _value;	 // held in shared mode - no diagnostic
			lk.Release();
			return v + _value;	// expected-warning {{reading variable '_value' requires holding mutex '_mutex'}}
		}

	private:
		ov::SharedMutex _mutex;
		int _value OV_GUARDED_BY(_mutex) = 0;
	};
}  // namespace
