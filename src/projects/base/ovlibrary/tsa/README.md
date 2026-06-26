# Thread Safety Analysis (TSA) Guide

This document is a guide to Thread Safety Analysis (TSA) for developers who want to contribute code to OvenMediaEngine (OME). It covers what TSA is and why it is used (background), what you must follow when writing new code (rules), examples for common situations, and how to deal with problems you will run into (troubleshooting).

All the related sources live under `src/projects/base/ovlibrary/tsa/`.

- `annotations.h` - TSA annotation macro definitions
- `mutex.h` - `ov::` mutex / guard / condition variable wrappers
- `mutex_test.cpp` - runtime behavior + layout regression tests (GTest)
- `mutex_negative_test.cpp` - TSA diagnostic regression test (Clang `-verify`)

## Table of Contents

1. [What TSA Is](#1-what-tsa-is)
2. [Quick Start](#2-quick-start)
3. [Provided Types and Macros](#3-provided-types-and-macros)
4. [Rules Contributors Must Follow](#4-rules-contributors-must-follow)
5. [Patterns by Example](#5-patterns-by-example)
6. [Migrating from std to ov](#6-migrating-from-std-to-ov)
7. [Limitations of TSA](#7-limitations-of-tsa)
8. [Troubleshooting](#8-troubleshooting)
9. [Design Background](#9-design-background)
10. [References](#10-references)

---

## 1. What TSA Is

### 1.1. In One Sentence

By annotating in code that "this variable is protected by this lock," you let Clang analyze every piece of code that touches that variable and **catch, at compile time, any path that accesses it without the lock**. It runs at compile time rather than runtime, so its runtime cost is zero.

### 1.2. Why It Matters

When multiple threads read or write the same variable concurrently without a lock, a data race occurs. In C++ a data race is undefined behavior (UB); it only breaks under specific timing, so it is hard to catch with tests. In the worst case it causes intermittent crashes in production.

```cpp
// Dangerous: accesses `_count` without a lock
class Counter
{
public:
    void Inc()
    {
        // BUG: writes without a lock -> data race
        _count++;
    }

    int Get()
    {
        std::lock_guard lock(_mutex);
        return _count;
    }

private:
    std::mutex _mutex;
    int _count = 0;
};
```

The bug in `Inc()` compiles and passes tests. TSA is a tool for detecting this kind of "forgot the lock" at build time.

### 1.3. How It Works

1. You declare on the variable to protect (member/global) that "this variable is protected by this mutex" (`OV_GUARDED_BY(_mutex)`).
2. While building with `-Wthread-safety`, Clang follows the control flow of each function and tracks the set of locks currently held (the held set).
3. At any point that accesses a protected variable, if the mutex is not in the held set, it emits a diagnostic (e.g. `reading variable '_count' requires holding mutex '_mutex'`).
4. A non-Clang compiler (GCC) ignores the annotations, so the build still passes (only the analysis is not performed).

The analysis is purely static, and the annotations expand to empty macros, so there is no runtime cost.

### 1.4. OFF / ON Modes

OME has a single set of `ov::` wrapper classes, and the CMake option `OME_THREAD_SAFETY` (default `OFF`) only toggles whether the annotations are active.

| Mode | Description |
| --- | --- |
| `OFF` (default) | All `OV_*` macros expand to nothing. Each `ov::` type is just a thin wrapper around the corresponding `std::` type. |
| `ON` | The same classes gain TSA capability annotations. They expand to real attributes (analyzed under `-Wthread-safety`) only on Clang; on other compilers they vanish into empty macros. |

The two modes have identical `sizeof` / `alignof` / method signatures (tests guard this with `static_assert`). So you write the source once and build with `ON` only when you need the analysis.

> [!IMPORTANT]
> Even with `OME_THREAD_SAFETY=ON`, no analysis happens unless the compiler is actually Clang. OME defaults to `OME_USE_CLANG=ON`, but that falls back to the system compiler (GCC) when `clang`/`clang++` are not installed, so make sure Clang is available. If TSA ends up off, CMake prints a `NO EFFECT` warning at configure time (not during the build); look for `[OME] Clang thread-safety analysis: ENABLED` in the configure log to confirm it is on (`cmake/CompilerOptions.cmake`).

---

## 2. Quick Start

### 2.1. Include

```cpp
#include <base/ovlibrary/ovlibrary.h>
```

This single header gives you all the `ov::` synchronization types and the `OV_*` annotation macros.

### 2.2. The Most Common Pattern: Guarding a Member with a Mutex

This is the pattern of protecting a member with a mutex and declaring that fact with an annotation. The frequently used macros are marked in comments.

```cpp
#include <base/ovlibrary/ovlibrary.h>

class Sample
{
public:
    void SetValue(int value)
    {
        // Guarded members are accessible only while the guard holds _mutex
        ov::LockGuard lock(_mutex);
        SetValueLocked(value);
    }

    void WaitUntilReady()
    {
        ov::LockGuard lock(_mutex);
        // Wait calls the predicate while holding the lock, but TSA analyzes the lambda
        // separately, so annotate it with OV_REQUIRES to say "called only while _mutex is held"
        _cv.Wait(lock, [this]() OV_REQUIRES(_mutex) -> bool { return _ready; });
    }

private:
    // OV_REQUIRES: an internal helper that does not lock itself; it assumes the caller already holds _mutex
    void SetValueLocked(int value) OV_REQUIRES(_mutex)
    {
        _value = value;
        _ready = true;
        _cv.NotifyAll();
    }

    ov::Mutex _mutex;

    // OV_GUARDED_BY: these members can be read or written only while holding _mutex
    int  _value OV_GUARDED_BY(_mutex) = 0;
    bool _ready OV_GUARDED_BY(_mutex) = false;

    ov::ConditionVariable _cv;
};
```

If you access `_value`/`_ready` without the lock, or call `SetValueLocked()` without the lock, an `ON`-mode (Clang) build emits a warning like:

```plain
warning: writing variable '_value' requires holding mutex '_mutex' exclusively
```

For details on the macros used above, see [5.4](#54-declaring-that-the-caller-holds-the-lock) (`OV_REQUIRES`) and [5.7](#57-condition-variable) (condition variable predicate).

### 2.3. Verifying with TSA Analysis

After writing new code, always build in `ON` mode (Clang) to check for violations. The relevant options are:

| Option | Default | Role |
| --- | --- | --- |
| `OME_THREAD_SAFETY` | `OFF` | When `ON` (Clang only), defines the `OME_THREAD_SAFETY` macro + enables `-Wthread-safety` |
| `OME_USE_CLANG` | `ON` | Use Clang as the compiler (required for TSA analysis) |
| `OME_BUILD_TESTS` | `OFF` | When `ON`, the TSA regression test is built/registered |

If you configure CMake directly, add `OME_THREAD_SAFETY=ON` like this (the other options are the same as a normal build).

```bash
# Configure with TSA enabled (Clang is the default compiler)
cmake -B <build-dir> -G Ninja -DOME_THREAD_SAFETY=ON -DOME_BUILD_TESTS=ON

# Build (TSA violations surface as -Wthread-safety warnings)
cmake --build <build-dir>
```

> [!IMPORTANT]
> TSA violations surface as compiler **warnings** (`-Wthread-safety`). OME does not use `-Werror`, so these warnings alone do not stop the build. After a change, check the build log yourself for any `warning: ... requires holding mutex ...` warnings.

---

## 3. Provided Types and Macros

All types are in the `ov::` namespace and are pulled in via `<base/ovlibrary/ovlibrary.h>`.

### 3.1. Mutex Classes

When you need manual lock/unlock, use the mutex's own methods rather than a guard.

| Type | std equivalent | Methods |
| --- | --- | --- |
| `ov::Mutex` | `std::mutex` | `Lock()` / `Unlock()` / `TryLock()` |
| `ov::RecursiveMutex` | `std::recursive_mutex` | same |
| `ov::SharedMutex` | `std::shared_mutex` | the above + `LockShared()` / `UnlockShared()` / `TryLockShared()` |
| `ov::ConditionVariable` | `std::condition_variable` | `Wait()` / `WaitFor()` / `WaitUntil()` + `NotifyOne()` / `NotifyAll()` |

> [!NOTE]
> `NativeHandle()`, which returns the internal `std::*mutex` handle, is `private`, and only `ScopedLock` (all mutexes) and `ConditionVariable` (only `ov::Mutex`) can access it. Application code cannot touch the internal std types directly (see [9.2](#92-why-nativehandle-is-hidden) for why).

### 3.2. RAII Guards

The guards are all pure RAII: they lock in the constructor and release in the destructor, and do nothing else.

| Type | std equivalent | Use |
| --- | --- | --- |
| `ov::LockGuard<T>` | `std::lock_guard<T>` | exclusive guard that locks a single mutex immediately |
| `ov::SharedLockGuard<T>` | `std::shared_lock<T>` (immediate-acquire) | locks immediately in shared mode |
| `ov::ScopedLock<T...>` | `std::scoped_lock<T...>` | locks multiple mutexes at once (avoids deadlock internally) |
| `ov::ReleasableLockGuard<T>` | N/A | immediate exclusive lock + early `Release()` before scope end |
| `ov::ReleasableSharedLockGuard<T>` | N/A | immediate shared lock + early `Release()` |

Class template argument deduction (CTAD) is supported, so you can write `ov::LockGuard lock(_mutex);` without the type argument.

> [!NOTE]
> There is a single exclusive guard for one mutex: `LockGuard`. Use `Releasable*` only when you truly need an early unlock; otherwise use the pure-RAII `LockGuard` / `SharedLockGuard`. `ov::UniqueLock` is not provided (see [9.1](#91-why-there-is-no-uniquelock)).

### 3.3. Annotation Macros

All of these are defined in `annotations.h`. At first it is enough to learn just the ones used in [2.2](#22-the-most-common-pattern-guarding-a-member-with-a-mutex): `OV_GUARDED_BY` and `OV_REQUIRES` (use `OV_REQUIRES_SHARED` for read-only paths), plus `OV_NO_THREAD_SAFETY_ANALYSIS` for opting out of analysis. You usually do not write `OV_ACQUIRE` / `OV_RELEASE` by hand; they are needed only when you build a function or guard that has a lock/unlock effect of its own.

| Category | Macro | Meaning |
| --- | --- | --- |
| Guarded data | `OV_GUARDED_BY(m)` | this member/global is protected by `m` (the most common one) |
|  | `OV_PT_GUARDED_BY(m)` | the value a pointer points to is protected by `m` |
| Capability | `OV_LOCKABLE` | put on a mutex-like class (same as `OV_CAPABILITY("mutex")`) |
|  | `OV_CAPABILITY("label")` | label a non-mutex capability (e.g. a thread role) |
|  | `OV_SCOPED_CAPABILITY` | RAII guard (acquires in ctor, releases in dtor) |
| Preconditions | `OV_REQUIRES(m...)` | `m` must be held exclusively at call time |
|  | `OV_REQUIRES_SHARED(m...)` | `m` must be held in shared mode at call time |
|  | `OV_EXCLUDES(m...)` | `m` must NOT be held at call time |
| State transitions | `OV_ACQUIRE(m...)` | the function acquires `m` exclusively and keeps it on return |
|  | `OV_ACQUIRE_SHARED(m...)` | the function acquires `m` in shared mode |
|  | `OV_RELEASE(m...)` | the function releases a capability held exclusively |
|  | `OV_RELEASE_SHARED(m...)` | the function releases a capability held in shared mode |
|  | `OV_TRY_ACQUIRE(ok, m...)` | try-acquire (`ok` is the success return value, usually `true`) |
|  | `OV_TRY_ACQUIRE_SHARED(ok, m...)` | shared-mode try-acquire |
| Runtime asserts | `OV_ASSERT_CAPABILITY(m)` | tell the analyzer `m` is held exclusively at this point |
|  | `OV_ASSERT_SHARED_CAPABILITY(m)` | tell the analyzer `m` is held in shared mode at this point |
| Opt out | `OV_NO_THREAD_SAFETY_ANALYSIS` | skip analysis for this function body (last resort) |

The precondition/state-transition macros such as `OV_REQUIRES` / `OV_ACQUIRE` / `OV_RELEASE` / `OV_EXCLUDES` are variadic, so you can list several mutexes separated by commas (e.g. `OV_REQUIRES(_a, _b)`).

---

## 4. Rules Contributors Must Follow

Please follow the rules below when writing new code or modifying existing code.

### 4.1. Use `ov::` Types for Synchronization Primitives

In new code, do not use `std::mutex` / `std::shared_mutex` / `std::lock_guard` directly; use the corresponding `ov::` types. Only the `ov::` types carry TSA annotations.

### 4.2. Annotate Mutex-Guarded Members with `OV_GUARDED_BY`

If you decide to protect a member with a mutex, declare which mutex protects it using `OV_GUARDED_BY`. Without the annotation, the member is left out of TSA tracking, so annotate every member you protect with a lock.

That said, not every shared member has to be protected by a mutex. A member synchronized by a lock-free mechanism such as `std::atomic` has no protecting mutex, so it is not an `OV_GUARDED_BY` target and gets no annotation (there is no mutex to name). Pick one strategy per member -- "mutex-protected" or "atomic/lock-free" -- and apply `OV_GUARDED_BY` only to the former.

```cpp
private:
    ov::Mutex        _mutex;
    int              _value OV_GUARDED_BY(_mutex) = 0;   // mutex-protected -> annotate
    std::atomic<int> _counter{0};                        // atomic -> no lock needed, no GUARDED_BY
```

### 4.3. Distinguish Reads and Writes as Shared / Exclusive

When using `ov::SharedMutex`, use `SharedLockGuard` (or `OV_REQUIRES_SHARED`) for read-only paths and `LockGuard` (or `OV_REQUIRES`) for write paths.

If a read method is `const` and locks inside it, the mutex member must be declared `mutable`. Because `Lock()` / `LockShared()` are non-const, locking inside a `const` method without `mutable` produces a compile error from inside `mutex.h`.

### 4.4. Do Manual Lock/Unlock on the Mutex, Not the Guard

The guards are pure RAII. If you need explicit lock/unlock, call `mutex.Lock()` / `mutex.Unlock()` / `mutex.TryLock()` directly.

### 4.5. Do Not Use Dynamic-Ownership Patterns

Dynamic-ownership features of `std::unique_lock` -- the `defer_lock` / `try_to_lock` / `adopt_lock` constructors, `release()`, `owns_lock()`, and move -- are not provided (compile error). If you need such a pattern, simplify the structure by splitting the critical section into a function or hoisting the branch up to the caller (see [6.2](#62-intentionally-blocked-patterns)).

### 4.6. Annotate Condition-Variable Predicates with `OV_REQUIRES`

When the predicate lambda of `Wait(lock, pred)` reads an `OV_GUARDED_BY` member, the analyzer does not know the lambda is called while holding the lock, so it produces a false positive. Fix it by annotating the lambda with `OV_REQUIRES` (see [5.7](#57-condition-variable)).

```cpp
ov::LockGuard lock(_mutex);
_cv.Wait(lock, [this]() OV_REQUIRES(_mutex) -> bool { return _ready; });
```

### 4.7. `OV_NO_THREAD_SAFETY_ANALYSIS` Is a Last Resort

Use it only per-function for patterns TSA cannot express, and keep its scope minimal. If possible, consider the alternatives in [Section 7](#7-limitations-of-tsa) first.

When you do apply this macro, you **must** leave a comment explaining why analysis is disabled. An `OV_NO_THREAD_SAFETY_ANALYSIS` without a rationale comment is not allowed. Because the entire function body becomes a TSA blind spot, the next person reading the code must be able to judge from that comment alone why it is safe without a lock.

```cpp
// Safety rationale: _cache is filled only in the constructor and accessed read-only afterward.
// This initialize-once invariant cannot be expressed in TSA, so analysis is disabled.
const Entry &Lookup(int key) const OV_NO_THREAD_SAFETY_ANALYSIS
{
    return _cache.at(key);
}
```

### 4.8. Always Verify `ON` Mode with Clang

On GCC the annotations are ignored, so even obvious violations pass with no warning. Always build your changes once with Clang + `OME_THREAD_SAFETY=ON` to verify.

### 4.9. Put Annotations on the Declaration, Not the `.cpp` Definition

When a function is declared in a header and defined in a `.cpp`, put its TSA annotation (`OV_REQUIRES`, `OV_ACQUIRE`, `OV_NO_THREAD_SAFETY_ANALYSIS`, and so on) on the **declaration** in the header. Do not put it on the out-of-line `.cpp` definition instead.

A caller is checked only against the declaration it can see. An annotation that lives only on the `.cpp` definition is invisible to other translation units, so the requirement is silently NOT enforced at their call sites, and it still compiles with no error, which makes the mistake easy to miss. The annotation on the declaration already governs the definition body, so do not repeat it on the definition.

```cpp
// session.h -- the annotation goes on the declaration
class Session
{
public:
    void Flush() OV_REQUIRES(_mutex);   // callers are checked against this precondition

private:
    ov::Mutex _mutex;
    int       _pending OV_GUARDED_BY(_mutex) = 0;
};

// session.cpp -- the definition carries no annotation; the declaration's precondition applies
void Session::Flush()
{
    _pending = 0;
}
```

Do NOT annotate only the definition: with a bare header declaration (`void Flush();`) and the annotation on the `.cpp` body (`void Session::Flush() OV_REQUIRES(_mutex) { ... }`), a caller that forgets the lock compiles cleanly with no warning. Member annotations such as `OV_GUARDED_BY` already live on the member declaration in the header, so they satisfy this rule naturally.

---

## 5. Patterns by Example

All of these are forms verified to work on Clang.

### 5.1. Simple Exclusive Protection

```cpp
class Account
{
public:
    void Deposit(int amount)
    {
        ov::LockGuard lock(_mutex);
        _balance += amount;
    }

private:
    ov::Mutex _mutex;
    int       _balance OV_GUARDED_BY(_mutex) = 0;
};
```

### 5.2. Shared vs Exclusive (SharedMutex)

```cpp
int  Read()  const OV_REQUIRES_SHARED(_mutex) { return _value; }
void Write()       OV_REQUIRES(_mutex)        { _value = 1; }
```

Call reads with `SharedLockGuard` and writes with `LockGuard`.

### 5.3. Locking Multiple Mutexes at Once

To lock several mutexes at once, use `ScopedLock`. Internally it uses `std::lock` to avoid deadlock.

```cpp
ov::ScopedLock lock(_first_mutex, _second_mutex);
// both mutexes are held in this region
```

### 5.4. Declaring That the Caller Holds the Lock

When a function touches protected variables directly but the caller is responsible for locking, declare the precondition with `OV_REQUIRES`. Adding a suffix like `Locked` to the name makes the intent clear.

```cpp
// Precondition: the caller holds the lock
void WriteLocked() OV_REQUIRES(_mutex)
{
    _value = 1;
}

void Write()
{
    ov::LockGuard lock(_mutex);
    WriteLocked();   // precondition satisfied -> no warning
}
```

### 5.5. Try-Acquire

The guards have no try-constructor. Call the mutex's `TryLock()` directly and let the caller handle the branch.

```cpp
if (_mutex.TryLock())
{
    // critical section
    _mutex.Unlock();
}
```

### 5.6. Early Unlock

Use `ReleasableLockGuard` only when the scope is hard to split and you must release the lock early.

```cpp
ov::ReleasableLockGuard lock(_mutex);
_value = 1;

lock.Release();   // unlock early here
DoLongIo();       // long work without holding the lock

// accessing `_value` after Release() makes TSA warn with 'requires holding mutex'
```

### 5.7. Condition Variable

The `Wait*` family of `ov::ConditionVariable` accepts only `ov::LockGuard<ov::Mutex>` as its lock argument (any other guard/lock type is a compile error).

```cpp
ov::LockGuard lock(_mutex);

// Recommended: annotate the predicate lambda with OV_REQUIRES
_cv.Wait(lock, [this]() OV_REQUIRES(_mutex) -> bool { return _ready; });
```

`Wait(lock, pred)` calls the predicate while holding the lock, so reading an `OV_GUARDED_BY` member inside the lambda is actually safe. But TSA analyzes the lambda body independently at its definition site, so it does not know "Wait calls it while holding the lock" and produces a false positive. Annotating the predicate lambda with `OV_REQUIRES(_mutex)` tells it "this lambda is called only while `_mutex` is held," and the warning goes away. In `OFF` mode `OV_REQUIRES` expands to nothing, so it behaves exactly like a plain lambda.

> [!WARNING]
> Calling `Wait` after manually `Unlock()`-ing the mutex while the guard is still alive is UB. This is the same hazard as `std::condition_variable`'s `lk.unlock(); cv.wait(lk);` and is not detected statically. Always manage lock state through the guard alone (see [9.3](#93-why-the-condition-variable-precondition-is-not-statically-enforced)).

### 5.8. Guarded Data Held via Pointer or Reference

The `OV_GUARDED_BY` family applies only to members/globals and is ignored on local variables.

| Alias form | annotation | what is tracked |
| --- | --- | --- |
| member pointer (the pointer) | `int *_p OV_GUARDED_BY(_m);` | reassigning/reading `_p` |
| member pointer (the pointee) | `int *_p OV_PT_GUARDED_BY(_m);` | accessing `*_p` |
| member reference | `int &_r OV_GUARDED_BY(_m);` | accessing `_r` |
| local pointer/reference | (anything) | ignored (blind spot) |

```cpp
// the value pointed to by *_p is guarded by _mutex
int *_p OV_PT_GUARDED_BY(_mutex) = nullptr;

void Ok()
{
    ov::LockGuard lock(_mutex);
    _p = &_value;
    *_p = 1;       // holding the lock -> OK (accessing `*_p` without the lock warns)
}
```

### 5.9. Non-Mutex Capabilities

`OV_LOCKABLE` is the same as `OV_CAPABILITY("mutex")`. To model a non-lock capability (e.g. a thread role), you can give a different label like `OV_CAPABILITY("role")`, and the diagnostic text is printed with that name. The string is only a diagnostic label and has no effect on the analysis itself (usually `OV_LOCKABLE` is all you need).

---

## 6. Migrating from std to ov

### 6.1. Mapping Table

| Old (`std::`) | New (`ov::`) | Note |
| --- | --- | --- |
| `std::mutex` | `ov::Mutex` | `NativeHandle()` is private |
| `std::recursive_mutex` | `ov::RecursiveMutex` |  |
| `std::shared_mutex` | `ov::SharedMutex` |  |
| `std::condition_variable` | `ov::ConditionVariable` | PascalCase methods; `Wait` takes only `LockGuard<ov::Mutex>` |
| `std::lock_guard<T>` | `ov::LockGuard<T>` | direct mapping |
| `std::unique_lock<T> lock(m);` (immediate) | `ov::LockGuard lock(m);` | use `LockGuard` for the immediate-acquire case |
| `std::shared_lock<T> lock(m);` | `ov::SharedLockGuard lock(m);` | immediate-acquire only |
| `std::scoped_lock<T...> lock(...);` | `ov::ScopedLock lock(...);` |  |

### 6.2. Intentionally Blocked Patterns

The patterns below are compile errors. Restructure as described in the alternative column.

| Old pattern | Alternative |
| --- | --- |
| `std::unique_lock(m, std::defer_lock)` then `lock.lock()` | restructure the critical section to avoid `defer_lock` |
| `std::unique_lock(m, std::try_to_lock)` + `owns_lock()` branch | branch directly with `mutex.TryLock()` |
| `std::unique_lock(m, std::adopt_lock)` | do not pre-lock; let `ov::ScopedLock` do the locking |
| `lock.release()` to transfer ownership | split the critical section into a function and re-enter the guard |
| `lock.owns_lock()` conditional unlock | move the branch outside the critical section |
| `std::move(lock)` | restructure the scope (move is not allowed) |

---

## 7. Limitations of TSA

These are the representative situations TSA misses or cannot handle. In the examples below, assume `_mutex` is an `ov::Mutex` and `_value` / `_ready` are `OV_GUARDED_BY(_mutex)` members.

### 7.1. Access Leaked via an Alias or Pointer Is Not Detected (false negative)

TSA tracks only by the name of the variable carrying `OV_GUARDED_BY`, so if you make an alias via an address or reference and access it outside the lock, it cannot catch it.

```cpp
void Leak()
{
    int *p;
    { ov::LockGuard lock(_mutex); p = &_value; }
    *p = 1;   // writes without a lock but no warning (access via `p`, not `_value`)
}
```

This detection failure is limited to addresses that escape the scope, such as local aliases. If you keep the alias as a member/global and annotate it with `OV_PT_GUARDED_BY` (etc.), it can be tracked (see [5.8](#58-guarded-data-held-via-pointer-or-reference)).

### 7.2. Clang-Only (false negative)

There is no analysis on GCC at all. The annotations are ignored, so even obvious violations pass with zero warnings.

```cpp
void Bad() { _value = 1; }   // clang(ON): warning / g++: no warning
```

### 7.3. Dynamic Ownership Cannot Be Expressed (over-detection)

Code where whether the lock is held depends on a runtime condition produces a warning even when it is safe, because it cannot prove the lock is "held on every path."

```cpp
// need_lock: caller passes true if it does not already hold _mutex
void Update(bool need_lock)
{
    if (need_lock) _mutex.Lock();
    _value = 1;                     // warning: writing _value needs the lock + not held on every path
    if (need_lock) _mutex.Unlock(); // warning: may release a mutex that was not held
}
```

Here `need_lock` is a flag that branches at runtime on "does the caller already hold the lock?" That is, this function mixes the case where it runs while holding the lock and the case where it does not, so the lock-held state does not align with the function boundary. TSA cannot track this runtime branch, so it cannot prove `_value` access is protected on every path.

The alternative is to manage the lock **per function**. Do not let one function lock and unlock conditionally; fix each function's role with respect to the lock to exactly one. A function becomes one of the following two:

- It owns the lock for its entire scope (locks with `ov::LockGuard` at the start).
- It does not touch the lock directly and declares with `OV_REQUIRES(_mutex)` that the caller must already hold it.

The `Update` above mixes both roles in one function. Split the protected logic into a function annotated with `OV_REQUIRES`, and handle the locking outside it with a guard.

```cpp
// Core logic: does not lock itself; assumes the caller holds _mutex
void UpdateLocked() OV_REQUIRES(_mutex)
{
    _value = 1;
}

// A caller that already holds the lock just calls it
void CallerThatHoldsLock() OV_REQUIRES(_mutex)
{
    UpdateLocked();
}

// A caller that does not hold the lock locks with a guard first, then calls
void CallerThatDoesNotHoldLock()
{
    ov::LockGuard lock(_mutex);
    UpdateLocked();
}
```

Fixing each function's lock role to one keeps the lock-held state inside the function constant (held for the whole scope, or held by the caller throughout). The runtime branch disappears, so the lock state on each path is statically clear and TSA can verify it (see [5.4](#54-declaring-that-the-caller-holds-the-lock)).

### 7.4. Lambda Bodies Are Analyzed at Their Definition Site (false positive)

Even a lambda called while holding the lock -- such as a `cv.Wait` predicate -- gets a warning on protected-variable reads, because TSA does not know the calling context. Fix it by annotating the predicate lambda with `OV_REQUIRES` (see [5.7](#57-condition-variable)).

### 7.5. The Condition-Variable Precondition Is Not Statically Enforced

`Wait*` has the precondition that "the guard must actually hold the mutex at call time," and the only way to violate it (manually `Unlock()`-ing behind the guard, then `Wait`) is not detected statically. This is the same structural limitation as `std::condition_variable` (see [9.3](#93-why-the-condition-variable-precondition-is-not-statically-enforced)).

---

## 8. Troubleshooting

### 8.1. Common Diagnostics

| Diagnostic | Cause | Fix |
| --- | --- | --- |
| `reading variable 'x' requires holding mutex 'm'` | reading an `OV_GUARDED_BY(m)` member without a lock | lock with `LockGuard`/`SharedLockGuard` before access, or declare `OV_REQUIRES` |
| `writing variable 'x' requires holding mutex 'm' exclusively` | writing a protected member without a lock | lock exclusively before access |
| `mutex 'm' is not held on every path through here` | conditional locking at runtime (dynamic ownership) | hoist the branch to the caller and declare `OV_REQUIRES` (see [7.3](#73-dynamic-ownership-cannot-be-expressed-over-detection)) |
| `reading variable 'x' requires holding mutex 'm'` (inside a predicate lambda) | a cv predicate reads a protected member with no annotation | add `OV_REQUIRES(m)` to the lambda (see [5.7](#57-condition-variable)) |
| `calling function 'f' requires holding mutex 'm' exclusively` | calling an `OV_REQUIRES` function without the lock | lock the mutex before the call |
| `acquiring mutex 'm' that is already held` | re-locking a mutex already held within analyzer-visible scope | remove the double-lock path, or split it out into an `OV_REQUIRES` helper (switching to `RecursiveMutex` does NOT silence this warning) |

### 8.2. Built with `ON` but No Warnings Appear

- Check that the compiler is Clang. On GCC the annotations are ignored (`OME_USE_CLANG=ON` is the default).
- Check that the CMake configure log printed `[OME] Clang thread-safety analysis: ENABLED`. If `OME_THREAD_SAFETY=ON` but the compiler is not Clang, a `NO EFFECT` warning is printed instead.
- Reconfigure the build directory. After changing an option, a stale cache may keep the change from taking effect.
- If a specific call site that should require a lock is not warned, check that the function's annotation is on its header declaration, not only the `.cpp` definition; an annotation seen only in the `.cpp` does not check callers in other files (see [4.9](#49-put-annotations-on-the-declaration-not-the-cpp-definition)).

### 8.3. Correct Code but a Warning Fires (false positive)

Most of these fall under the limitations in [Section 7](#7-limitations-of-tsa).

- Dynamic ownership (`if (cond) Lock();`) -> hoist the branch to the caller and express it with `OV_REQUIRES` (see [7.3](#73-dynamic-ownership-cannot-be-expressed-over-detection)).
- cv predicate lambda -> add `OV_REQUIRES` (see [5.7](#57-condition-variable)).
- If the pattern still cannot be expressed, apply `OV_NO_THREAD_SAFETY_ANALYSIS` to the function, but keep its scope minimal and leave a rationale comment (see [4.7](#47-ov_no_thread_safety_analysis-is-a-last-resort)).

### 8.4. Verification Checklist

For the TSA regression test (`tsa_negative_verify`) to be registered/run, all of the following must hold:

- `OME_THREAD_SAFETY=ON`
- the compiler is Clang
- `OME_BUILD_TESTS=ON`

If any is missing, the test is not registered. The non-Clang case is not silent: with `OME_THREAD_SAFETY=ON` on a non-Clang compiler, CMake's `NO EFFECT` configure warning explicitly states the negative-verify test is skipped. With all three enabled, the check runs automatically as part of the build.

The Clang `-verify` used here is a diagnostic-verification mode whose logic is the opposite of a normal compile. You declare in source comments, with markers (`// expected-warning {{...}}`), that "this diagnostic must fire on this line"; the compile succeeds only when the actual diagnostics exactly match the declarations (every declared diagnostic fires, and no unexpected diagnostic appears). The pass criterion is not "a warning was produced" but "expectation matches reality."

This test compiles `mutex_negative_test.cpp` with Clang `-Xclang -verify -fsyntax-only -Wthread-safety` (`-fsyntax-only` means it only checks diagnostics, with no codegen or linking). The file deliberately contains incorrect lock usage with an expected-warning marker on each line, so if an annotation is accidentally dropped or weakened in `mutex.h`, the expected warning disappears, `-verify` fails, and the regression is caught.

---

## 9. Design Background

The `ov::` synchronization wrappers follow the same "pure RAII + static capability" direction as libwebrtc `MutexLock`, abseil `MutexLock`, and Chromium `base::AutoLock`. They keep a single principle: a guard locks in the constructor and releases in the destructor, and does nothing else. The background for a few decisions that follow from this is below.

### 9.1. Why There Is No UniqueLock

The dynamic-ownership API of `std::unique_lock` (the `defer_lock` / `try_to_lock` / `adopt_lock` constructors, `release()`, `owns_lock()`, move) cannot be tracked statically by Clang TSA's capability-identity model. When the guard's construction is separated from the locking moment, the capability mapping between the constructor argument and the internal member is not formed, so a false positive occurs even after locking.

```cpp
// Hypothetical: if UniqueLock were implemented as defer-lock + manual Lock()
UniqueLock lk(_mutex, std::defer_lock);   // (1) bound only, not locked yet
lk.Lock();                                 // (2) actually locked later
_value = 1;                                // (3) still warns 'requires holding mutex' despite being locked
```

So the exclusive guard is unified into a single pure-RAII `LockGuard` and `UniqueLock` is removed. If you need explicit lock/unlock, use the mutex methods directly. The single effect of "early unlock before scope end," however, is something TSA can track cleanly (`Release()` carries `OV_RELEASE()`), so it is provided only as the separate types `ReleasableLockGuard` / `ReleasableSharedLockGuard`.

### 9.2. Why NativeHandle Is Hidden

`NativeHandle()` is an accessor that returns the wrapper's internal `std::*mutex&`. If it were public, the internal std handle could be taken out and locked/unlocked outside the guard/tracking, creating a TSA blind spot.

```cpp
// Hypothetical: if NativeHandle() were public
std::mutex *raw = &m.NativeHandle();
// lock/unlock through raw afterward would escape TSA tracking
```

So it is `private`, and only the friends that need the internal connection may access it. `ScopedLock` is a friend of all mutexes (for the `std::lock` algorithm), and `ConditionVariable` is a friend of `ov::Mutex` (for driving `std::condition_variable`).

### 9.3. Why the Condition-Variable Precondition Is Not Statically Enforced

`Wait*` has the standard `std::condition_variable` precondition that "the guard must actually hold the mutex at call time." Normal use (create guard -> Wait -> release at scope end) always satisfies it. The only way to violate it is to call `_mutex.Unlock()` manually behind the guard and then `Wait`, which is not detected statically.

```cpp
ov::LockGuard lock(_mutex);
_mutex.Unlock();              // released manually behind the guard
_cv.WaitFor(lock, 1ms);       // waiting on an unlocked mutex -> UB
```

This is not a defect specific to this implementation; it is the same structural limitation as `std::condition_variable` (`lk.unlock(); cv.wait(lk);`). Clang TSA cannot express "a function taking a guard requires the capability that guard wraps." To block it statically you would have to remove the public `Mutex::Unlock()` or change to a `Wait(Mutex&)` form, and both move away from the minimal API. So this is accepted as an intended characteristic, and the recommendation is to always manage lock state through the guard alone.

---

## 10. References

- Clang Thread Safety Analysis official documentation: <https://clang.llvm.org/docs/ThreadSafetyAnalysis.html>
- Source: `src/projects/base/ovlibrary/tsa/` (`annotations.h`, `mutex.h`, `mutex_test.cpp`, `mutex_negative_test.cpp`)
