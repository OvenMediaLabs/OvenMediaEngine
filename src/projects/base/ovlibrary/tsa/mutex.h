//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#pragma once

// `ov::` mutex/lock-guard wrappers for Clang Thread Safety Analysis (TSA).
//
// 1. API (scoped guards)
//    The scoped guards (`LockGuard`, `SharedLockGuard`, `ScopedLock`) are pure RAII:
//    they acquire in the constructor and release in the destructor,
//    and expose nothing else - no manual `Lock()`/`Unlock()`,
//    no `defer_lock`/`try_to_lock`/`adopt_lock` constructor,
//    no `release()`, no `owns_lock()`/`operator bool()`, no move support.
//    Explicit lock/unlock outside the RAII pattern is done on the mutex types themselves
//    (`ov::Mutex::Lock()`/`Unlock()`, which model `BasicLockable`/`Lockable`/`SharedLockable`),
//    not through the guards.
//    Bypassing a live guard by touching the underlying mutex is the caller's responsibility,
//    exactly as with `std::unique_lock` + `mutex()`.
//
// 2. `OME_THREAD_SAFETY` toggle behavior
//    There is a single set of wrapper classes; the macro only toggles the annotations.
//    With it undefined (default `OFF`) the `OV_*` macros expand to nothing, so each `ov::`
//    type is a plain thin wrapper around the corresponding `std::` type.
//    Every wrapper method is an inline forwarder over the std equivalent,
//    so there is no runtime cost compared with calling `std::` directly
//    (verified with gcc/clang at `-O2`: zero `ov::Mutex::Lock`/`Unlock`/`TryLock` symbols,
//    all calls inlined to the std equivalent).
//    With the macro defined (`ON`) the same classes carry Clang TSA capability annotations.
//    Enabling it also requires Clang and `-Wthread-safety` to actually catch any violations;
//    on other compilers the annotations expand to nothing.
//
// 3. Internal `std::` bridge via `NativeHandle()`
//    Each wrapper mutex holds a `NativeHandle()` accessor for the underlying
//    `std::mutex` (or recursive/shared variant), but it is `private` and reachable
//    only by the two friends that need it - `ov::ScopedLock`
//    (to drive the `std::lock` deadlock-avoiding algorithm)
//    and `ov::ConditionVariable` (a separate class that each mutex friends, to drive
//    `std::condition_variable::wait`, bridging the guard's `std::mutex`
//    via `std::unique_lock` + `adopt_lock`/`release`).
//    There is no public raw-handle escape hatch: application code never sees the `std::` type
//    and interacts only through the PascalCase API.
//
// 4. External template contracts (e.g. a third-party `using Mutex = std::mutex`) stay untouched.
//    Third-party libraries instantiate their own mutex types and own the lifecycle -
//    this header only provides `ov::` types for OvenMediaEngine code.
//
// 5. Usage example
//    ```cpp
//    #include <base/ovlibrary/ovlibrary.h>
//
//    class Sample
//    {
//    public:
//        int Read() const
//        {
//            ov::SharedLockGuard lock(_mutex);
//            return _value;
//        }
//
//        void Write(int value)
//        {
//            ov::LockGuard lock(_mutex);
//            _value = value;
//        }
//
//    private:
//        mutable ov::SharedMutex _mutex;
//        int _value OV_GUARDED_BY(_mutex) = 0;
//    };
//    ```
//
// 6. Accepted design decisions
//    (intentional trade-offs - reviewed and deliberate; NOT defects, please do not re-report them as issues)
//
//    (a) The `ConditionVariable::Wait*` precondition (the user lock must actually hold its mutex on entry)
//        is NOT statically enforced.
//        The only way to violate it is to reach around the guard and call `Mutex::Unlock()` manually
//        before waiting - undefined behavior, and the exact same un-diagnosed sharp edge as
//        `std::condition_variable` (`lk.unlock(); cv.wait(lk);`).
//        Clang TSA cannot express "a function taking the guard requires the guard's wrapped capability";
//        enforcing it would require dropping the public `Mutex::Unlock()` or
//        making `Wait` take `Mutex&` - both move AWAY from the minimal API.
//        Accepted as a by-design limitation.
//
//    (b) `ReleasableLockGuard`/`ReleasableSharedLockGuard` add an explicit early `Release()`
//        and therefore hold a nullable pointer (small state) rather than being pure ctor/dtor guards.
//        This is a DELIBERATE, opt-in extension kept in SEPARATE types
//        (the common `LockGuard`/`SharedLockGuard` stay pure RAII).
//        The early unlock is the one dynamic-ownership effect Clang TSA tracks cleanly (`Release()` = `OV_RELEASE()`);
//        their narrow API (+`Release()` only) is pinned in `mutex_negative_test.cpp`.
//        Accepted; not a regression of the pure-RAII default.

#include <condition_variable>
#include <mutex>
#include <shared_mutex>
#include <tuple>

#include "annotations.h"

namespace ov::tsa_detail
{
	// Exception-safe ownership hand-back for the `ConditionVariable` wait bridge.
	// Each `Wait*` adopts the caller `LockGuard`'s already-locked `std::mutex`
	// into a transient `std::unique_lock` to drive the std cv,
	// then must `release()` it (detach WITHOUT unlocking) so the mutex stays locked
	// and the caller's guard remains its sole owner.
	// Doing that only on the normal return path is a bug: if the cv call exits
	// via an exception (e.g. a throwing predicate),
	// the std contract guarantees the mutex is re-locked on exception exit,
	// but a skipped `release()` lets the transient `std::unique_lock` unlock
	// in its destructor, and then the caller's guard unlocks the same mutex again ->
	// double-unlock UB.
	// Releasing from a destructor runs on every exit path (normal and exception),
	// so the unlock happens exactly once, in the caller's guard.
	class AdoptedLockReleaser
	{
	public:
		explicit AdoptedLockReleaser(std::unique_lock<std::mutex> &lock)
			: _lock(lock)
		{
		}
		~AdoptedLockReleaser()
		{
			_lock.release();
		}

		AdoptedLockReleaser(const AdoptedLockReleaser &)			= delete;
		AdoptedLockReleaser &operator=(const AdoptedLockReleaser &) = delete;

	private:
		std::unique_lock<std::mutex> &_lock;
	};
}  // namespace ov::tsa_detail

// ----------------------------------------------------------------------------
// Mutex/guard wrappers.
//
// Each mutex variant gets its own class so it can carry an `OV_LOCKABLE` tag.
// The class holds a single std equivalent and forwards every method one-to-one.
// The capability annotations are live only when `OME_THREAD_SAFETY` is defined on Clang;
// otherwise the `OV_*` macros expand to nothing, leaving plain thin forwarders.
// `static_assert`s at the end of the namespace pin `sizeof` and `alignof` to match the std
// types so any future field additions become a compile-time error.
// ----------------------------------------------------------------------------

namespace ov
{
	class ConditionVariable;

	template <class Tmutex>
	class LockGuard;

	template <class... Tmutex>
	class ScopedLock;

	// Exclusive mutex.
	// Models `BasicLockable` and `Lockable`.
	class OV_LOCKABLE Mutex
	{
	public:
		Mutex()							= default;
		~Mutex()						= default;

		Mutex(const Mutex &)			= delete;
		Mutex &operator=(const Mutex &) = delete;

		void Lock() OV_ACQUIRE()
		{
			_mutex.lock();
		}
		void Unlock() OV_RELEASE()
		{
			_mutex.unlock();
		}
		bool TryLock() OV_TRY_ACQUIRE(true)
		{
			return _mutex.try_lock();
		}

	private:
		// Raw std handle - friend-only, not a public escape.
		// `ov::ScopedLock` uses it to drive `std::lock`;
		// `ov::ConditionVariable` reaches it via the `ov::LockGuard` it waits on.
		std::mutex &NativeHandle()
		{
			return _mutex;
		}

		friend class ConditionVariable;
		template <class... Tmutex>
		friend class ScopedLock;

		std::mutex _mutex;
	};

	// Recursive mutex.
	// Same capability tag - the analyzer does not differentiate recursive from non-recursive ownership.
	class OV_LOCKABLE RecursiveMutex
	{
	public:
		RecursiveMutex()								  = default;
		~RecursiveMutex()								  = default;

		RecursiveMutex(const RecursiveMutex &)			  = delete;
		RecursiveMutex &operator=(const RecursiveMutex &) = delete;

		void Lock() OV_ACQUIRE()
		{
			_mutex.lock();
		}
		void Unlock() OV_RELEASE()
		{
			_mutex.unlock();
		}
		bool TryLock() OV_TRY_ACQUIRE(true)
		{
			return _mutex.try_lock();
		}

	private:
		// Raw std handle - friend-only (used by `ov::ScopedLock`).
		std::recursive_mutex &NativeHandle()
		{
			return _mutex;
		}

		template <class... Tmutex>
		friend class ScopedLock;

		std::recursive_mutex _mutex;
	};

	// Shared (reader/writer) mutex.
	// Adds the shared-mode trio so it satisfies the `SharedLockable` named requirement.
	class OV_LOCKABLE SharedMutex
	{
	public:
		SharedMutex()								= default;
		~SharedMutex()								= default;

		SharedMutex(const SharedMutex &)			= delete;
		SharedMutex &operator=(const SharedMutex &) = delete;

		void Lock() OV_ACQUIRE()
		{
			_mutex.lock();
		}
		void Unlock() OV_RELEASE()
		{
			_mutex.unlock();
		}
		bool TryLock() OV_TRY_ACQUIRE(true)
		{
			return _mutex.try_lock();
		}

		void LockShared() OV_ACQUIRE_SHARED()
		{
			_mutex.lock_shared();
		}
		void UnlockShared() OV_RELEASE_SHARED()
		{
			_mutex.unlock_shared();
		}
		bool TryLockShared() OV_TRY_ACQUIRE_SHARED(true)
		{
			return _mutex.try_lock_shared();
		}

	private:
		// Raw std handle - friend-only (used by `ov::ScopedLock`).
		std::shared_mutex &NativeHandle()
		{
			return _mutex;
		}

		template <class... Tmutex>
		friend class ScopedLock;

		std::shared_mutex _mutex;
	};

	// `ov::ConditionVariable` - PascalCase wrapper over `std::condition_variable`.
	// Each `Wait*` overload bridges the user lock's `NativeHandle()` so the std cv operates
	// on the underlying std mutex while the user-visible OME API stays uniformly PascalCase.
	// A future self-hosted implementation can replace the std backing without changing
	// that public API.
	//
	// Clang Thread Safety Analysis cannot follow capability state through a lambda body.
	// When the bound predicate of `Wait(lock, pred)` accesses an `OV_GUARDED_BY(mutex)`
	// member, the analyzer cannot tell - at the lambda's definition site -
	// that `Wait` invokes the predicate with `mutex` re-acquired.
	// The resulting "requires holding mutex" warning is a false positive in the std contract
	// sense but a true Clang TSA limitation in practice.
	// To suppress it the predicate lambda must declare what it requires:
	//
	// ```cpp
	// ov::LockGuard lock(_mutex);
	// _cv.Wait(lock, [this]() OV_REQUIRES(_mutex) { return _ready; });
	//                         ^^^^^^^^^^^^^^^^^^^ - required in `ON` mode
	// ```
	//
	// In OFF mode `OV_REQUIRES` expands to nothing, so the predicate stays a plain lambda
	// and the source is identical between modes.
	class ConditionVariable
	{
	public:
		ConditionVariable()										= default;
		~ConditionVariable()									= default;

		ConditionVariable(const ConditionVariable &)			= delete;
		ConditionVariable &operator=(const ConditionVariable &) = delete;

		// Each `Wait*` bridges the guard's `std::mutex`
		// (via the `ov::Mutex` friend's `NativeHandle()`) into a transient `std::unique_lock`
		// with `std::adopt_lock`, drives the contained `std::condition_variable`,
		// then `release()`s it so ownership returns to the user guard.
		// The std mutex reference is outside the capability tracker,
		// so the analyzer sees no held-set confusion.
		// Narrow by design: only `ov::LockGuard<ov::Mutex>` participates in wait/relock cycles.
		//
		// Precondition (READ BEFORE USING): every `Wait*` requires `user_lock` to hold
		// its mutex on entry - the standard `std::condition_variable` precondition.
		// Normal use satisfies it automatically:
		//
		//     ov::LockGuard lock(_mutex);
		//     _cv.Wait(lock, [this]() OV_REQUIRES(_mutex) { return _ready; });   // lock held - OK
		//
		// The only way to break it is to unlock the mutex behind the guard's back
		// (`_mutex.Unlock()` while the guard is alive) and then wait - UB,
		// exactly as with bare `std::condition_variable`.
		// TSA cannot catch this (see accepted-decision 6(a) at the top of this header);
		// do not do it.
		void Wait(LockGuard<Mutex> &user_lock);

		template <class Tpredicate>
		void Wait(LockGuard<Mutex> &user_lock, Tpredicate &&pred);

		template <class Trep, class Tperiod>
		std::cv_status WaitFor(LockGuard<Mutex> &user_lock,
							   const std::chrono::duration<Trep, Tperiod> &dur);

		template <class Trep, class Tperiod, class Tpredicate>
		bool WaitFor(LockGuard<Mutex> &user_lock,
					 const std::chrono::duration<Trep, Tperiod> &dur,
					 Tpredicate &&pred);

		template <class Tclock, class Tduration>
		std::cv_status WaitUntil(LockGuard<Mutex> &user_lock,
								 const std::chrono::time_point<Tclock, Tduration> &tp);

		template <class Tclock, class Tduration, class Tpredicate>
		bool WaitUntil(LockGuard<Mutex> &user_lock,
					   const std::chrono::time_point<Tclock, Tduration> &tp,
					   Tpredicate &&pred);

		void NotifyOne() noexcept
		{
			_cv.notify_one();
		}
		void NotifyAll() noexcept
		{
			_cv.notify_all();
		}

	private:
		std::condition_variable _cv;
	};

	// ------------------------------------------------------------------------
	// RAII guards.
	// Each one is `OV_SCOPED_CAPABILITY`, so Clang tracks the acquire/release pair across
	// the guard's lifetime.
	//
	// API is intentionally narrow: immediate-acquire constructor and destructor only - pure RAII.
	// No manual lock/unlock, no `defer_lock`/`try_to_lock`/`adopt_lock` constructors,
	// no `release()`, no `owns_lock()`, no move support, no `mutex()` accessor.
	// ------------------------------------------------------------------------

	template <class Tmutex>
	class OV_SCOPED_CAPABILITY LockGuard
	{
	public:
		explicit LockGuard(Tmutex &m) OV_ACQUIRE(m)
			: _mutex(m)
		{
			_mutex.Lock();
		}

		~LockGuard() OV_RELEASE()
		{
			_mutex.Unlock();
		}

		LockGuard(const LockGuard &)			= delete;
		LockGuard &operator=(const LockGuard &) = delete;

	private:
		// `ov::ConditionVariable` (friend) adopts `_mutex`'s std handle for its waits;
		// only `LockGuard` is accepted by the CV.
		friend class ConditionVariable;

		Tmutex &_mutex;
	};

	template <class Tmutex>
	LockGuard(Tmutex &) -> LockGuard<Tmutex>;

	// Shared-mode counterpart of `LockGuard`: immediate shared acquire in the constructor
	// (`OV_ACQUIRE_SHARED`), release in the destructor
	// (`OV_RELEASE`) through the `OV_SCOPED_CAPABILITY` model, nothing else.
	// The `ov::ConditionVariable::Wait` path remains `ov::LockGuard`.
	template <class Tmutex>
	class OV_SCOPED_CAPABILITY SharedLockGuard
	{
	public:
		explicit SharedLockGuard(Tmutex &m) OV_ACQUIRE_SHARED(m)
			: _mutex(m)
		{
			_mutex.LockShared();
		}

		~SharedLockGuard() OV_RELEASE()
		{
			_mutex.UnlockShared();
		}

		SharedLockGuard(const SharedLockGuard &)			= delete;
		SharedLockGuard &operator=(const SharedLockGuard &) = delete;

	private:
		Tmutex &_mutex;
	};

	template <class Tmutex>
	SharedLockGuard(Tmutex &) -> SharedLockGuard<Tmutex>;

	// Like `LockGuard` but adds an explicit early `Release()`.
	// Separate type so `LockGuard` stays a pure reference guard;
	// holds a nullable pointer so it can be cleared on release.
	// `Release()` carries `OV_RELEASE()` so Clang TSA tracks the early unlock
	// (access after `Release()` is diagnosed),
	// and the destructor becomes a no-op once released (no double-release diagnostic).
	template <class Tmutex>
	class OV_SCOPED_CAPABILITY ReleasableLockGuard
	{
	public:
		explicit ReleasableLockGuard(Tmutex &m) OV_ACQUIRE(m)
			: _mutex(&m)
		{
			_mutex->Lock();
		}

		~ReleasableLockGuard() OV_RELEASE()
		{
			if (_mutex != nullptr)
			{
				_mutex->Unlock();
			}
		}

		void Release() OV_RELEASE()
		{
			if (_mutex != nullptr)
			{
				_mutex->Unlock();
				_mutex = nullptr;
			}
		}

		ReleasableLockGuard(const ReleasableLockGuard &)			= delete;
		ReleasableLockGuard &operator=(const ReleasableLockGuard &) = delete;

	private:
		Tmutex *_mutex;
	};

	template <class Tmutex>
	ReleasableLockGuard(Tmutex &) -> ReleasableLockGuard<Tmutex>;

	// Shared-mode counterpart.
	// Acquires shared (`OV_ACQUIRE_SHARED`);
	// like `SharedLockGuard`, the release effect is the generic `OV_RELEASE()` -
	// a scoped capability releases the same way whether it was acquired shared
	// or exclusive (using `OV_RELEASE_SHARED()` here would be a mismatch).
	template <class Tmutex>
	class OV_SCOPED_CAPABILITY ReleasableSharedLockGuard
	{
	public:
		explicit ReleasableSharedLockGuard(Tmutex &m) OV_ACQUIRE_SHARED(m)
			: _mutex(&m)
		{
			_mutex->LockShared();
		}

		~ReleasableSharedLockGuard() OV_RELEASE()
		{
			if (_mutex != nullptr)
			{
				_mutex->UnlockShared();
			}
		}

		void Release() OV_RELEASE()
		{
			if (_mutex != nullptr)
			{
				_mutex->UnlockShared();
				_mutex = nullptr;
			}
		}

		ReleasableSharedLockGuard(const ReleasableSharedLockGuard &)			= delete;
		ReleasableSharedLockGuard &operator=(const ReleasableSharedLockGuard &) = delete;

	private:
		Tmutex *_mutex;
	};

	template <class Tmutex>
	ReleasableSharedLockGuard(Tmutex &) -> ReleasableSharedLockGuard<Tmutex>;

	inline void ConditionVariable::Wait(LockGuard<Mutex> &user_lock)
	{
		std::unique_lock ul(user_lock._mutex.NativeHandle(), std::adopt_lock);
		tsa_detail::AdoptedLockReleaser releaser(ul);
		_cv.wait(ul);
	}

	template <class Tpredicate>
	void ConditionVariable::Wait(LockGuard<Mutex> &user_lock, Tpredicate &&pred)
	{
		std::unique_lock ul(user_lock._mutex.NativeHandle(), std::adopt_lock);
		tsa_detail::AdoptedLockReleaser releaser(ul);
		_cv.wait(ul, std::forward<Tpredicate>(pred));
	}

	template <class Trep, class Tperiod>
	std::cv_status ConditionVariable::WaitFor(
		LockGuard<Mutex> &user_lock,
		const std::chrono::duration<Trep, Tperiod> &dur)
	{
		std::unique_lock ul(user_lock._mutex.NativeHandle(), std::adopt_lock);
		tsa_detail::AdoptedLockReleaser releaser(ul);
		return _cv.wait_for(ul, dur);
	}

	template <class Trep, class Tperiod, class Tpredicate>
	bool ConditionVariable::WaitFor(
		LockGuard<Mutex> &user_lock,
		const std::chrono::duration<Trep, Tperiod> &dur,
		Tpredicate &&pred)
	{
		std::unique_lock ul(user_lock._mutex.NativeHandle(), std::adopt_lock);
		tsa_detail::AdoptedLockReleaser releaser(ul);
		return _cv.wait_for(ul, dur, std::forward<Tpredicate>(pred));
	}

	template <class Tclock, class Tduration>
	std::cv_status ConditionVariable::WaitUntil(
		LockGuard<Mutex> &user_lock,
		const std::chrono::time_point<Tclock, Tduration> &tp)
	{
		std::unique_lock ul(user_lock._mutex.NativeHandle(), std::adopt_lock);
		tsa_detail::AdoptedLockReleaser releaser(ul);
		return _cv.wait_until(ul, tp);
	}

	template <class Tclock, class Tduration, class Tpredicate>
	bool ConditionVariable::WaitUntil(
		LockGuard<Mutex> &user_lock,
		const std::chrono::time_point<Tclock, Tduration> &tp,
		Tpredicate &&pred)
	{
		std::unique_lock ul(user_lock._mutex.NativeHandle(), std::adopt_lock);
		tsa_detail::AdoptedLockReleaser releaser(ul);
		return _cv.wait_until(ul, tp, std::forward<Tpredicate>(pred));
	}

	// `ScopedLock` is the variadic guard equivalent to `std::scoped_lock`.
	// Acquisition delegates to `std::lock` over each mutex's underlying std handle,
	// inheriting the standard deadlock-avoiding algorithm and its strong exception guarantee
	// (on a throw mid-acquire, every already acquired lock is released before rethrowing).
	// The acquire/release effects flow through the constructor `OV_ACQUIRE(ms...)`
	// and destructor `OV_RELEASE()`, keeping the analyzer's held-set consistent
	// at the caller's scope.
	// The `std::lock` call operates on the `NativeHandle()` escape,
	// which Clang TSA does not track, so the analyzer-visible net effect is exactly
	// the `OV_ACQUIRE(ms...)` declaration.
	// A single mutex cannot go through `std::lock`
	// (it requires two or more) and is locked directly;
	// zero mutexes is rejected at compile time.
	template <class... Tmutex>
	class OV_SCOPED_CAPABILITY ScopedLock
	{
	public:
		static_assert(sizeof...(Tmutex) >= 1,
					  "ov::ScopedLock requires at least one mutex");

		explicit ScopedLock(Tmutex &...ms) OV_ACQUIRE(ms...)
			: _ms{&ms...}
		{
			LockAll(std::index_sequence_for<Tmutex...>{});
		}

		~ScopedLock() OV_RELEASE()
		{
			UnlockAll(std::index_sequence_for<Tmutex...>{});
		}

		ScopedLock(const ScopedLock &)			  = delete;
		ScopedLock &operator=(const ScopedLock &) = delete;

	private:
		template <std::size_t... Is>
		void LockAll(std::index_sequence<Is...>)
		{
			if constexpr (sizeof...(Tmutex) == 1)
			{
				(std::get<Is>(_ms)->NativeHandle().lock(), ...);
			}
			else
			{
				std::lock(std::get<Is>(_ms)->NativeHandle()...);
			}
		}

		template <std::size_t... Is>
		void UnlockAll(std::index_sequence<Is...>)
		{
			(std::get<sizeof...(Tmutex) - 1 - Is>(_ms)->NativeHandle().unlock(), ...);
		}

		std::tuple<Tmutex *...> _ms;
	};

	template <class... Tmutex>
	ScopedLock(Tmutex &...) -> ScopedLock<Tmutex...>;

	// ------------------------------------------------------------------------
	// Layout invariants.
	//
	// The wrapper mutex classes intentionally hold a single std equivalent and nothing else,
	// so they must keep the same `sizeof`/`alignof` as the std types.
	// Any future change that adds a field will fail these asserts immediately instead
	// of silently breaking ABI compatibility.
	// ------------------------------------------------------------------------

	static_assert(sizeof(Mutex) == sizeof(std::mutex),
				  "ov::Mutex must stay the same size as std::mutex");
	static_assert(alignof(Mutex) == alignof(std::mutex),
				  "ov::Mutex must keep std::mutex alignment");

	static_assert(sizeof(RecursiveMutex) == sizeof(std::recursive_mutex),
				  "ov::RecursiveMutex must stay the same size as std::recursive_mutex");
	static_assert(alignof(RecursiveMutex) == alignof(std::recursive_mutex),
				  "ov::RecursiveMutex must keep std::recursive_mutex alignment");

	static_assert(sizeof(SharedMutex) == sizeof(std::shared_mutex),
				  "ov::SharedMutex must stay the same size as std::shared_mutex");
	static_assert(alignof(SharedMutex) == alignof(std::shared_mutex),
				  "ov::SharedMutex must keep std::shared_mutex alignment");
}  // namespace ov
