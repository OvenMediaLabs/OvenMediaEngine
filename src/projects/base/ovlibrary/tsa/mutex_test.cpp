//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================

// Unit tests for the `ov::` thread-safety analysis wrapper header
// (`base/ovlibrary/tsa/mutex.h`).
// Locks down the immediate-acquire scoped-guard contract that both the OFF
// (inline-forwarder wrapper) and ON (capability-annotated wrapper) builds expose: the common
// guards (`LockGuard`, `SharedLockGuard`, `ScopedLock`) are pure RAII - the constructor
// acquires and the destructor releases, with no manual lock/unlock API.
//
// For those common guards the API intentionally does **not** mirror the full
// `std::unique_lock`/`std::shared_lock` API: there are no `defer_lock`/`try_to_lock` /
// `adopt_lock` constructors, no `release()`, no `owns_lock()`, no move support,
// no `mutex()` accessor.
// That set is locked down by static_assert rows in `mutex_negative_test.cpp`
// (built under `-Xclang -verify`).
//
// `ReleasableLockGuard`/`ReleasableSharedLockGuard` are the deliberate exception: a separate
// opt-in pair that adds an explicit early `Release()`
// (and only that) on top of the pure-RAII shape.
// They have their own tests below.

#include <base/ovlibrary/tsa/mutex.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

// ---------------------------------------------------------------------------
// 1. Type/layout invariants
// ---------------------------------------------------------------------------

// The wrapper types must remain layout-compatible with the underlying std types in both modes.
// `static_assert` here catches any future field addition at compile time.
static_assert(sizeof(ov::Mutex) == sizeof(std::mutex),
			  "ov::Mutex must match std::mutex size");
static_assert(alignof(ov::Mutex) == alignof(std::mutex),
			  "ov::Mutex must match std::mutex alignment");
static_assert(sizeof(ov::RecursiveMutex) == sizeof(std::recursive_mutex),
			  "ov::RecursiveMutex must match std::recursive_mutex size");
static_assert(alignof(ov::RecursiveMutex) == alignof(std::recursive_mutex),
			  "ov::RecursiveMutex must match std::recursive_mutex alignment");
static_assert(sizeof(ov::SharedMutex) == sizeof(std::shared_mutex),
			  "ov::SharedMutex must match std::shared_mutex size");
static_assert(alignof(ov::SharedMutex) == alignof(std::shared_mutex),
			  "ov::SharedMutex must match std::shared_mutex alignment");

// Both modes wrap the std mutex types into distinct PascalCase wrapper classes;
// the OFF mode body is a thin inline forwarder over the std equivalent so the runtime cost
// is the same as calling std directly.
// ON mode adds Clang TSA capability tags on top.
static_assert((std::is_same_v<ov::Mutex, std::mutex>) == false,
			  "ov::Mutex must wrap std::mutex into a distinct class");
static_assert((std::is_same_v<ov::SharedMutex, std::shared_mutex>) == false,
			  "ov::SharedMutex must wrap std::shared_mutex into a distinct class");
static_assert((std::is_same_v<ov::RecursiveMutex, std::recursive_mutex>) == false,
			  "ov::RecursiveMutex must wrap std::recursive_mutex into a distinct class");

// `ov::ConditionVariable` is a distinct PascalCase wrapper class in both modes.
// It wraps `std::condition_variable` and the wait path is intentionally narrowed
// to `ov::LockGuard<ov::Mutex>`, matching the immediate-acquire guard API.
static_assert((std::is_same_v<ov::ConditionVariable, std::condition_variable>) == false,
			  "ov::ConditionVariable must wrap, not alias, std::condition_variable");

TEST(TsaTypes, SizeAndAlignmentMatchStdEquivalents)
{
	EXPECT_EQ(sizeof(ov::Mutex), sizeof(std::mutex));
	EXPECT_EQ(alignof(ov::Mutex), alignof(std::mutex));
	EXPECT_EQ(sizeof(ov::SharedMutex), sizeof(std::shared_mutex));
	EXPECT_EQ(alignof(ov::SharedMutex), alignof(std::shared_mutex));
	EXPECT_EQ(sizeof(ov::RecursiveMutex), sizeof(std::recursive_mutex));
	EXPECT_EQ(alignof(ov::RecursiveMutex), alignof(std::recursive_mutex));
}

// ---------------------------------------------------------------------------
// 2. Compile-time guard-trait invariants
// ---------------------------------------------------------------------------
//
// The guards expose only the immediate-acquire scoped-lockable shape.
// They are not copyable and not movable;
// the only constructor takes the mutex by reference.
// The negative test file pins the absence of the removed constructors
// via `static_assert(!is_constructible_v<...>)` rows under `-Xclang -verify`;
// these traits here pin the positive invariants.

static_assert(std::is_copy_constructible_v<ov::LockGuard<ov::Mutex>> == false,
			  "ov::LockGuard must not be copy-constructible");
static_assert(std::is_copy_assignable_v<ov::LockGuard<ov::Mutex>> == false,
			  "ov::LockGuard must not be copy-assignable");
static_assert(std::is_move_constructible_v<ov::LockGuard<ov::Mutex>> == false,
			  "ov::LockGuard must not be move-constructible "
			  "(scoped guard)");
static_assert(std::is_move_assignable_v<ov::LockGuard<ov::Mutex>> == false,
			  "ov::LockGuard must not be move-assignable");
static_assert(std::is_default_constructible_v<ov::LockGuard<ov::Mutex>> == false,
			  "ov::LockGuard must not be default-constructible");
static_assert(std::is_constructible_v<ov::LockGuard<ov::Mutex>, ov::Mutex &>,
			  "ov::LockGuard must be constructible from M&");

static_assert(std::is_copy_constructible_v<ov::SharedLockGuard<ov::SharedMutex>> == false,
			  "ov::SharedLockGuard must not be copy-constructible");
static_assert(std::is_copy_assignable_v<ov::SharedLockGuard<ov::SharedMutex>> == false,
			  "ov::SharedLockGuard must not be copy-assignable");
static_assert(std::is_move_constructible_v<ov::SharedLockGuard<ov::SharedMutex>> == false,
			  "ov::SharedLockGuard must not be move-constructible");
static_assert(std::is_move_assignable_v<ov::SharedLockGuard<ov::SharedMutex>> == false,
			  "ov::SharedLockGuard must not be move-assignable");
static_assert(std::is_default_constructible_v<ov::SharedLockGuard<ov::SharedMutex>> == false,
			  "ov::SharedLockGuard must not be default-constructible");
static_assert(std::is_constructible_v<ov::SharedLockGuard<ov::SharedMutex>, ov::SharedMutex &>,
			  "ov::SharedLockGuard must be constructible from M&");

// Releasable guards: same scoped-lockable shape (no copy/move/default,
// immediate-acquire ctor) plus an explicit `Release()`.
static_assert(std::is_copy_constructible_v<ov::ReleasableLockGuard<ov::Mutex>> == false,
			  "ov::ReleasableLockGuard must not be copy-constructible");
static_assert(std::is_move_constructible_v<ov::ReleasableLockGuard<ov::Mutex>> == false,
			  "ov::ReleasableLockGuard must not be move-constructible");
static_assert(std::is_default_constructible_v<ov::ReleasableLockGuard<ov::Mutex>> == false,
			  "ov::ReleasableLockGuard must not be default-constructible");
static_assert(std::is_constructible_v<ov::ReleasableLockGuard<ov::Mutex>, ov::Mutex &>,
			  "ov::ReleasableLockGuard must be constructible from M&");
static_assert(std::is_copy_constructible_v<ov::ReleasableSharedLockGuard<ov::SharedMutex>> == false,
			  "ov::ReleasableSharedLockGuard must not be copy-constructible");
static_assert(std::is_move_constructible_v<ov::ReleasableSharedLockGuard<ov::SharedMutex>> == false,
			  "ov::ReleasableSharedLockGuard must not be move-constructible");
static_assert(std::is_constructible_v<ov::ReleasableSharedLockGuard<ov::SharedMutex>, ov::SharedMutex &>,
			  "ov::ReleasableSharedLockGuard must be constructible from M&");

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{
	// Runtime probe for "is this exclusive mutex currently held?".
	// A background thread tries to acquire the lock;
	// if it succeeds the caller was not holding the lock.
	// The probe restores the original state by releasing the lock it grabbed.
	bool IsExclusivelyHeld(ov::Mutex &m)
	{
		auto result = std::async(std::launch::async, [&m]() {
			if (m.TryLock())
			{
				m.Unlock();
				return false;
			}
			return true;
		});
		return result.get();
	}

	// Probe for "would an exclusive (writer) acquire block right now?".
	// A background thread tries the exclusive `TryLock`; failure means the mutex
	// is unavailable to a writer, which covers BOTH the writer-held and the
	// reader(s)-held states - it does NOT imply exclusive ownership specifically.
	bool ExclusiveAcquireWouldBlock(ov::SharedMutex &m)
	{
		auto result = std::async(std::launch::async, [&m]() {
			if (m.TryLock())
			{
				m.Unlock();
				return false;
			}
			return true;
		});
		return result.get();
	}

	// Probe for "is there no exclusive writer currently holding this shared mutex?".
	// `TryLockShared` succeeds when no thread holds the mutex exclusively;
	// note that readers may still be holding it in shared mode (and would block a
	// writer), so a success means "no writer holds it now", NOT "a writer could acquire now".
	// This distinguishes the "writer-held" case from "free or reader-held".
	bool NoExclusiveWriterHeld(ov::SharedMutex &m)
	{
		auto result = std::async(std::launch::async, [&m]() {
			if (m.TryLockShared())
			{
				m.UnlockShared();
				return true;
			}
			return false;
		});
		return result.get();
	}
}  // namespace

// ---------------------------------------------------------------------------
// 3. ov::LockGuard - simplest RAII path
// ---------------------------------------------------------------------------

TEST(TsaLockGuard, ImmediateLockAndUnlockOnDestruction)
{
	ov::Mutex m;
	{
		ov::LockGuard lock(m);
		EXPECT_TRUE(IsExclusivelyHeld(m));
	}
	EXPECT_FALSE(IsExclusivelyHeld(m));
}

// ---------------------------------------------------------------------------
// 4. ov::SharedLockGuard - immediate-acquire shared scoped guard
// ---------------------------------------------------------------------------

TEST(TsaSharedLockGuard, ImmediateSharedAcquireReleasesOnDtor)
{
	ov::SharedMutex m;
	{
		ov::SharedLockGuard lk(m);
		// Held in shared mode, so no exclusive writer is present and another reader can acquire.
		EXPECT_TRUE(NoExclusiveWriterHeld(m));
		// But the outstanding shared lock still blocks a writer from acquiring exclusively.
		EXPECT_TRUE(ExclusiveAcquireWouldBlock(m));
	}
	EXPECT_FALSE(ExclusiveAcquireWouldBlock(m));
}

// ---------------------------------------------------------------------------
// 4b. ov::ReleasableLockGuard/ReleasableSharedLockGuard - early release
// ---------------------------------------------------------------------------

TEST(TsaReleasableLockGuard, EarlyReleaseUnlocksAndDtorIsNoOp)
{
	ov::Mutex m;
	{
		ov::ReleasableLockGuard lock(m);
		EXPECT_TRUE(IsExclusivelyHeld(m));
		lock.Release();						 // early unlock
		EXPECT_FALSE(IsExclusivelyHeld(m));	 // released immediately
	}  // dtor is a no-op (no double unlock)
	EXPECT_FALSE(IsExclusivelyHeld(m));

	// Without a Release() call the dtor performs the unlock.
	{
		ov::ReleasableLockGuard lock(m);
		EXPECT_TRUE(IsExclusivelyHeld(m));
	}
	EXPECT_FALSE(IsExclusivelyHeld(m));
}

TEST(TsaReleasableSharedLockGuard, EarlyReleaseUnlocksAndDtorIsNoOp)
{
	ov::SharedMutex m;
	{
		ov::ReleasableSharedLockGuard lock(m);
		EXPECT_TRUE(ExclusiveAcquireWouldBlock(m));	 // shared held -> writer blocked
		lock.Release();
		EXPECT_FALSE(ExclusiveAcquireWouldBlock(m));  // released -> writer can proceed
	}
	EXPECT_FALSE(ExclusiveAcquireWouldBlock(m));
}

// ---------------------------------------------------------------------------
// 5. ov::ScopedLock - multiple mutex deadlock-avoidance variant
// ---------------------------------------------------------------------------

TEST(TsaScopedLock, SingleMutex)
{
	ov::Mutex m;
	{
		ov::ScopedLock lock(m);
		EXPECT_TRUE(IsExclusivelyHeld(m));
	}
	EXPECT_FALSE(IsExclusivelyHeld(m));
}

TEST(TsaScopedLock, TwoMutexes)
{
	ov::Mutex m1;
	ov::Mutex m2;
	{
		ov::ScopedLock lock(m1, m2);
		EXPECT_TRUE(IsExclusivelyHeld(m1));
		EXPECT_TRUE(IsExclusivelyHeld(m2));
	}
	EXPECT_FALSE(IsExclusivelyHeld(m1));
	EXPECT_FALSE(IsExclusivelyHeld(m2));
}

// Cross-thread reverse-order acquire stress: thread A grabs (m1, m2) while thread B grabs (m2, m1).
// Without the deadlock-avoiding back-off the two threads can deadlock instantly.
// With it, both must complete every round.
TEST(TsaScopedLock, NoDeadlockUnderCrossThreadReverseOrder)
{
	constexpr int ROUNDS = 200;

	ov::Mutex m1;
	ov::Mutex m2;
	std::atomic<int> a_done{0};
	std::atomic<int> b_done{0};

	std::thread a([&]() {
		for (auto i = 0; i < ROUNDS; ++i)
		{
			ov::ScopedLock lock(m1, m2);
			a_done++;
		}
	});
	std::thread b([&]() {
		for (auto i = 0; i < ROUNDS; ++i)
		{
			ov::ScopedLock lock(m2, m1);
			b_done++;
		}
	});

	a.join();
	b.join();
	EXPECT_EQ(a_done.load(), ROUNDS);
	EXPECT_EQ(b_done.load(), ROUNDS);
}

TEST(TsaScopedLock, ThreeMutexes)
{
	ov::Mutex m1;
	ov::Mutex m2;
	ov::Mutex m3;
	{
		ov::ScopedLock lock(m1, m2, m3);
		EXPECT_TRUE(IsExclusivelyHeld(m1));
		EXPECT_TRUE(IsExclusivelyHeld(m2));
		EXPECT_TRUE(IsExclusivelyHeld(m3));
	}
	EXPECT_FALSE(IsExclusivelyHeld(m1));
	EXPECT_FALSE(IsExclusivelyHeld(m2));
	EXPECT_FALSE(IsExclusivelyHeld(m3));
}

// ---------------------------------------------------------------------------
// 6. ov::ConditionVariable scenarios
// ---------------------------------------------------------------------------

TEST(TsaConditionVariable, WaitAndNotifyOne)
{
	ov::Mutex m;
	ov::ConditionVariable cv;
	bool ready	 = false;
	bool parked	 = false;

	std::thread waiter([&]() {
		ov::LockGuard lk(m);
		// Announce, under the lock, that we are about to park on the cv, then wake
		// the main thread's handshake. `cv.Wait` below atomically releases `m`.
		parked = true;
		cv.NotifyOne();
		// Drain spurious wakeups by re-checking the flag.
		while (ready == false)
		{
			cv.Wait(lk);
		}
	});

	{
		ov::LockGuard lk(m);
		// Deterministic handshake (no sleep): re-acquiring `m` here is only possible
		// once the waiter has set `parked` and entered `cv.Wait`, which releases `m`.
		// This guarantees the test exercises the real blocking-Wait + notify path.
		while (parked == false)
		{
			cv.Wait(lk);
		}
		ready = true;
	}
	cv.NotifyOne();
	waiter.join();
	SUCCEED();
}

TEST(TsaConditionVariable, WaitWithPredicate)
{
	ov::Mutex m;
	ov::ConditionVariable cv;
	int counter	 = 0;
	bool parked	 = false;

	std::thread waiter([&]() {
		ov::LockGuard lk(m);
		// Announce we are about to park, then wake the main thread's handshake.
		parked = true;
		cv.NotifyOne();
		cv.Wait(lk, [&]() { return counter >= 3; });
		EXPECT_GE(counter, 3);
	});

	{
		ov::LockGuard lk(m);
		// Deterministic handshake (no sleep): proceed only once the waiter is
		// parked inside `cv.Wait`, so the increments below exercise the real
		// predicate re-check + notify path rather than racing past it.
		while (parked == false)
		{
			cv.Wait(lk);
		}
	}

	for (auto i = 0; i < 3; ++i)
	{
		{
			ov::LockGuard lk(m);
			counter++;
		}
		cv.NotifyOne();
	}
	waiter.join();
}

// A predicate lambda annotated with `OV_REQUIRES(mutex)` lets the analyzer accept the access
// to an `OV_GUARDED_BY(mutex)` member in ON mode.
// The runtime semantics are identical to a bare predicate lambda;
// the annotation only suppresses the lambda-scope false positive on `wait(lock, pred)`.
namespace
{
	class CvPredicateProbe
	{
	public:
		void RunConsumerThenProducer()
		{
			std::thread waiter([this]() {
				ov::LockGuard lock(_mutex);
				// Announce we are about to park, then wake the producer's handshake.
				_parked = true;
				_cv.NotifyOne();
				_cv.Wait(lock, [&]() OV_REQUIRES(_mutex) { return (_ready); });
				EXPECT_TRUE(_ready);
			});

			{
				ov::LockGuard lock(_mutex);
				// Deterministic handshake (no sleep): proceed only once the consumer
				// is parked inside `_cv.Wait`, which releases `_mutex`.
				while (_parked == false)
				{
					_cv.Wait(lock);
				}
				_ready = true;
			}
			_cv.NotifyOne();
			waiter.join();
		}

	private:
		ov::Mutex _mutex;
		ov::ConditionVariable _cv;
		bool _ready OV_GUARDED_BY(_mutex)  = false;
		bool _parked OV_GUARDED_BY(_mutex) = false;
	};
}  // namespace

TEST(TsaConditionVariable, GuardedWaitMacroAccessesGuardedMember)
{
	CvPredicateProbe probe;
	probe.RunConsumerThenProducer();
}

// Exception-safety regression for the CV wait bridge.
// When the predicate throws, `std::condition_variable` exits with the mutex re-locked;
// the `ov::ConditionVariable` bridge must hand that single locked ownership back
// to the caller's `LockGuard` and must NOT also unlock it.
// A regression (the old "release() only on normal return" form) double-unlocks: the transient
// `std::unique_lock` unlocks on the exception, then the guard unlocks again,
// which ThreadSanitizer flags as "unlock of an unlocked mutex".
// This test pins that the mutex stays held after the throw and is freed exactly once.
TEST(TsaConditionVariable, ThrowingPredicateKeepsMutexConsistent)
{
	ov::Mutex m;
	ov::ConditionVariable cv;
	bool threw = false;
	{
		ov::LockGuard lk(m);
		try
		{
			cv.WaitFor(lk, std::chrono::milliseconds(1),
					   []() -> bool { throw std::runtime_error("boom"); });
		}
		catch (const std::runtime_error &)
		{
			threw = true;
		}
		EXPECT_TRUE(threw);
		// The exception left the mutex re-locked and owned by `lk`.
		EXPECT_TRUE(IsExclusivelyHeld(m));
	}
	// `lk` released exactly once here;
	// a double-release would have unlocked an already-unlocked mutex above.
	// The mutex must now be free and reusable.
	EXPECT_FALSE(IsExclusivelyHeld(m));
	{
		ov::LockGuard lk2(m);
		EXPECT_TRUE(IsExclusivelyHeld(m));
	}
	EXPECT_FALSE(IsExclusivelyHeld(m));
}

// ---------------------------------------------------------------------------
// 7. Multi-threaded correctness
// ---------------------------------------------------------------------------

TEST(TsaConcurrency, MutexProtectsCounter)
{
	constexpr int THREAD_COUNT			= 8;
	constexpr int INCREMENTS_PER_THREAD = 1000;

	ov::Mutex m;
	int counter = 0;

	std::vector<std::thread> threads;
	threads.reserve(THREAD_COUNT);
	for (auto i = 0; i < THREAD_COUNT; ++i)
	{
		threads.emplace_back([&]() {
			for (auto j = 0; j < INCREMENTS_PER_THREAD; ++j)
			{
				ov::LockGuard lock(m);
				counter++;
			}
		});
	}
	for (auto &t : threads)
	{
		t.join();
	}

	EXPECT_EQ(counter, THREAD_COUNT * INCREMENTS_PER_THREAD);
}

// Producer-consumer regression for `ov::ConditionVariable`.
// Drives N rounds of single-item handoff through the cv to confirm there is no deadlock
// or missed wake-up after the PascalCase rewrite.
// The counters are OV_GUARDED_BY members (not locals - guarded_by is ignored on locals)
// so the `[&]() OV_REQUIRES(_mutex) { return (_produced > _consumed); }` predicate actually reads guarded
// state: in ON mode this exercises the OV_REQUIRES predicate annotation on a realistic counter-based
// path (must compile warning-free), on top of the runtime deadlock/missed-wakeup check.
namespace
{
	class CvProducerConsumerProbe
	{
	public:
		void Run(int rounds)
		{
			std::thread consumer([this, rounds]() {
				for (auto i = 0; i < rounds; ++i)
				{
					ov::LockGuard lk(_mutex);
					_cv.Wait(lk, [&]() OV_REQUIRES(_mutex) { return (_produced > _consumed); });
					_consumed++;
				}
			});

			std::thread producer([this, rounds]() {
				for (auto i = 0; i < rounds; ++i)
				{
					{
						ov::LockGuard lk(_mutex);
						_produced++;
					}
					_cv.NotifyOne();
				}
			});

			producer.join();
			consumer.join();

			ov::LockGuard lk(_mutex);
			EXPECT_EQ(_produced, rounds);
			EXPECT_EQ(_consumed, rounds);
		}

	private:
		ov::Mutex _mutex;
		ov::ConditionVariable _cv;
		int _produced OV_GUARDED_BY(_mutex) = 0;
		int _consumed OV_GUARDED_BY(_mutex) = 0;
	};
}  // namespace

TEST(TsaConditionVariable, ProducerConsumerNoDeadlockOrMissedWakeup)
{
	CvProducerConsumerProbe probe;
	probe.Run(1000);
}

TEST(TsaConcurrency, SharedMutexReaderWriter)
{
	constexpr int READER_COUNT = 4;
	constexpr int WRITER_COUNT = 2;
	constexpr int ITERATIONS   = 200;

	ov::SharedMutex m;
	int value = 0;
	std::atomic<int> read_total{0};

	std::vector<std::thread> threads;
	threads.reserve(READER_COUNT + WRITER_COUNT);

	for (auto i = 0; i < READER_COUNT; ++i)
	{
		threads.emplace_back([&]() {
			for (auto j = 0; j < ITERATIONS; ++j)
			{
				ov::SharedLockGuard lock(m);
				// Plain accumulator checked only after join(); relaxed is enough.
				read_total.fetch_add(value, std::memory_order_relaxed);
			}
		});
	}
	for (auto i = 0; i < WRITER_COUNT; ++i)
	{
		threads.emplace_back([&]() {
			for (auto j = 0; j < ITERATIONS; ++j)
			{
				ov::LockGuard lock(m);
				value++;
			}
		});
	}
	for (auto &t : threads)
	{
		t.join();
	}

	EXPECT_EQ(value, WRITER_COUNT * ITERATIONS);
	// `read_total` value is non-deterministic but must be a non-negative integer;
	// the important property is the absence of any data race during the run
	// (caught by ThreadSanitizer in CI configurations that enable it).
	EXPECT_GE(read_total.load(), 0);
}
