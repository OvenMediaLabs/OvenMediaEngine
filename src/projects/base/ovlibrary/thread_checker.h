//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <pthread.h>

#include <mutex>
#include <thread>

#include "./assert.h"
#include "./log.h"

namespace ov
{
	// Captures a source location (file, line, function) at the call site.
	// Modeled after std::source_location (C++20), but usable in C++17.
	//
	// Because __builtin_FILE/LINE/FUNCTION are evaluated at the call site
	// when used as default arguments, constructing ThreadLocation with no
	// arguments (or as a default argument itself) automatically captures
	// the caller's location.
	//
	// Usage:
	//   void Foo(ov::ThreadLocation loc = {}) { /* loc.file, loc.line, loc.func */ }
	struct ThreadLocation
	{
		const char *file;
		int line;
		const char *func;

		constexpr ThreadLocation(
			const char *file = __builtin_FILE(),
			int line		 = __builtin_LINE(),
			const char *func = __builtin_FUNCTION()) noexcept
			: file(file), line(line), func(func)
		{
		}
	};

	// Verifies that methods are always called from the same thread.
	//
	// Usage:
	//
	// ```
	//   class MyClass {
	//   public:
	//       void DoWork() {
	//           OV_ASSERT_RUN_ON(_thread_checker);
	//           // ...
	//       }
	//   private:
	//       ov::ThreadChecker _thread_checker;
	//   };
	// ```
	//
	// In DEBUG builds, the checker binds to the thread that calls it first
	// (or the thread on which the object was constructed) and asserts that
	// all subsequent calls come from the same thread.
	//
	// In RELEASE builds, all methods are no-ops and compile away entirely.
	class ThreadChecker
	{
	public:
		// Non-copyable, non-movable - the checker is tied to *this* object's
		// lifetime and binding to one specific thread.
		ThreadChecker(const ThreadChecker &)			= delete;
		ThreadChecker &operator=(const ThreadChecker &) = delete;

		// Initializes the process-wide main thread checker.
		// MUST be called from the main thread, exactly once, before any
		// worker threads are spawned (typically at the very top of main()).
		static void InitMainThread()
		{
#ifdef DEBUG
			OV_ASSERT(_main_initialized == false,
					  "InitMainThread() has already been called");

			static ThreadChecker checker;
			_main_checker	  = &checker;
			_main_initialized = true;
#endif	// DEBUG
		}

		// Returns the process-wide ThreadChecker for the main thread.
		// InitMainThread() MUST have been called before this.
		static ThreadChecker &MainThread()
		{
#ifdef DEBUG
			OV_ASSERT(_main_initialized,
					  "InitMainThread() must be called before MainThread()");

			return *_main_checker;
#else	// DEBUG
			static ThreadChecker checker;
			return checker;
#endif	// DEBUG
		}

#ifdef DEBUG
		// Tag type for constructing a detached checker.
		// Use: ThreadChecker checker(ThreadChecker::Detached);
		struct DetachedTag
		{
		};
		static constexpr DetachedTag Detached{};

		// Binds to the current thread on construction.
		ThreadChecker(ThreadLocation loc = {})
			: _bound_thread(::pthread_self()),
			  _bind_location{loc, ov::StackTrace::GetStackTrace()}
		{
		}

		// Constructs in a detached state.
		// Binds to the first thread that calls IsCurrent() or AssertIsCurrent().
		explicit ThreadChecker(DetachedTag, ThreadLocation loc = {})
			: _attached(false),
			  _bound_thread(),
			  _bind_location{loc, {}}
		{
		}

		~ThreadChecker() = default;

		// Returns `true` if the current thread matches the bound thread.
		bool IsCurrent() const
		{
			std::scoped_lock lock(_mutex);

			if (_attached == false)
			{
				// Auto-bind: re-attach to the current thread on first use.
				// No explicit call site available, so location is left empty.
				_bound_thread  = ::pthread_self();
				_attached	   = true;
				_bind_location = {{}, ov::StackTrace::GetStackTrace()};
				return true;
			}

			return ::pthread_equal(_bound_thread, ::pthread_self()) != 0;
		}

		// Binds the checker to the current thread, replacing any previous binding.
		// Useful for explicitly transferring ownership to the calling thread —
		// no need to call Detach() first.
		void Bind(ThreadLocation loc = {})
		{
			std::scoped_lock lock(_mutex);
			_bound_thread  = ::pthread_self();
			_attached	   = true;
			_bind_location = {loc, ov::StackTrace::GetStackTrace()};
		}

		// Detaches the checker from its current thread.
		// The next call to IsCurrent() will re-bind to whichever thread calls it.
		// Useful when an object is created on one thread but then handed off to
		// another.
		void Detach()
		{
			std::scoped_lock lock(_mutex);
			_attached = false;
		}

		// Asserts that the current thread matches the bound thread.
		// Called by OV_ASSERT_RUN_ON() — do not call directly.
		void AssertIsCurrent(ThreadLocation loc = {}) const
		{
			if (IsCurrent())
			{
				return;
			}

			auto expected					= _bound_thread;
			auto self						= ::pthread_self();

			ov::String expected_thread_name = ov::Platform::GetThreadName(expected);
			ov::String thread_name			= ov::Platform::GetThreadName(self);

			ov::String bind_location_str;
			if (_bind_location.loc.file != nullptr)
			{
				bind_location_str = ov::String::FormatString(
					"%s:%d in %s()\n\tBind stack trace:\n%s",
					_bind_location.loc.file, _bind_location.loc.line, _bind_location.loc.func,
					_bind_location.stack_trace.CStr());
			}
			else
			{
				bind_location_str = ov::String::FormatString(
					"(implicit bind)\n\tBind stack trace:\n%s",
					_bind_location.stack_trace.CStr());
			}

			OV_ASSERT(false,
					  "Thread checker failed at %s:%d in %s()\n"
					  "\tExpected thread: %s (%lu)\n"
					  "\tBound at: %s\n"
					  "\tActual   thread: %s (%lu)\n"
					  "\tStack trace:\n%s",
					  loc.file, loc.line, loc.func,
					  expected_thread_name.CStr(), static_cast<unsigned long>(expected),
					  bind_location_str.CStr(),
					  thread_name.CStr(), static_cast<unsigned long>(self),
					  ov::StackTrace::GetStackTrace().CStr());
		}

	private:
		struct BindLocation
		{
			ThreadLocation loc;
			ov::String stack_trace;
		};

		mutable std::mutex _mutex;
		mutable bool _attached = true;
		mutable pthread_t _bound_thread;
		mutable BindLocation _bind_location;

		inline static bool _main_initialized	   = false;
		inline static ThreadChecker *_main_checker = nullptr;

#else  // DEBUG

		struct DetachedTag
		{
		};
		static constexpr DetachedTag Detached{};

		constexpr ThreadChecker(ThreadLocation = {}) {}
		constexpr explicit ThreadChecker(DetachedTag, ThreadLocation = {}) {}

		constexpr bool IsCurrent() const
		{
			return true;
		}
		constexpr void Bind(ThreadLocation = {}) {}
		constexpr void Detach() {}
		constexpr void AssertIsCurrent(ThreadLocation = {}) const {}

#endif	// DEBUG
	};
}  // namespace ov

#define OV_CHECKER_BIND(checker) (checker).Bind()

// Asserts that the current thread matches the thread bound to |checker|.
#define OV_ASSERT_RUN_ON(checker) (checker).AssertIsCurrent()

// Asserts that the current thread is the main thread.
// Requires ov::ThreadChecker::InitMainThread() to have been called from the
// main thread before any worker threads are spawned.
#define OV_ASSERT_MAIN_THREAD() \
	OV_ASSERT_RUN_ON(ov::ThreadChecker::MainThread())
