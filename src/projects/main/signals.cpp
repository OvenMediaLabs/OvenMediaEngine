//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./signals.h"

#include <base/ovlibrary/ovlibrary.h>
#include <config/config_manager.h>
#include <dirent.h>
#include <errno.h>
#ifndef __APPLE__
#include <malloc.h>
#endif
#include <orchestrator/orchestrator.h>
#ifndef __APPLE__
#include <semaphore.h>
#endif
#include <signal.h>
#include <sys/ucontext.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "./main_private.h"
#include "./third_parties.h"
#include "main.h"

#define SIGNAL_CASE(x) \
	case x:            \
		return #x;

namespace ov::sig
{
	constexpr int SIG_ATOMIC_TRUE  = 1;
	constexpr int SIG_ATOMIC_FALSE = 0;

	constexpr int MAX_SIGINT_COUNT = 3;

	namespace
	{
		std::atomic<bool> g_need_to_stop = false;
#ifdef __APPLE__
		std::mutex g_stop_mutex;
		std::condition_variable g_stop_cv;
#else
		sem_t g_semaphore;
#endif

		std::thread g_signal_thread;

		// Occasionally, the memory can become corrupted and the version may not be displayed.
		// In such cases, it is stored in a separate variable for later reference.
		//
		// This variable contains strings in the format of "v0.1.2 (v0.1.2-xxx-yyyy) [debug]".
		char g_ome_version[1024];
		char g_dump_fallback_directory[PATH_MAX];

		// Configure for SIGRT* to use jemalloc
#ifdef OME_USE_JEMALLOC
		// SIGRTMIN+0: Show jemalloc stats
		int SIG_JEMALLOC_SHOW_STATS	  = (SIGRTMIN + 0);
		// SIGRTMIN+1: Trigger dump (This only works when `OME_USE_JEMALLOC_PROFILE` is defined)
		int SIG_JEMALLOC_TRIGGER_DUMP = (SIGRTMIN + 1);
		bool g_use_jemalloc_signal	  = false;
#endif	// OME_USE_JEMALLOC

		using OV_SIG_ACTION = void (*)(int signum, siginfo_t *si, void *unused);

		// http://man7.org/linux/man-pages/man7/signal.7.html
		const char *GetSignalName(int signum)
		{
			switch (signum)
			{
				SIGNAL_CASE(SIGABRT);
				SIGNAL_CASE(SIGALRM);
				SIGNAL_CASE(SIGBUS);
				SIGNAL_CASE(SIGCHLD);
				SIGNAL_CASE(SIGCONT);
				SIGNAL_CASE(SIGFPE);
				SIGNAL_CASE(SIGHUP);
				SIGNAL_CASE(SIGILL);
				SIGNAL_CASE(SIGINT);
				SIGNAL_CASE(SIGKILL);
				SIGNAL_CASE(SIGPIPE);
				SIGNAL_CASE(SIGPROF);
				SIGNAL_CASE(SIGQUIT);
				SIGNAL_CASE(SIGSEGV);
				SIGNAL_CASE(SIGSTOP);
				SIGNAL_CASE(SIGSYS);
				SIGNAL_CASE(SIGTERM);
				SIGNAL_CASE(SIGTRAP);
				SIGNAL_CASE(SIGTSTP);
				SIGNAL_CASE(SIGTTIN);
				SIGNAL_CASE(SIGTTOU);
				SIGNAL_CASE(SIGURG);
				SIGNAL_CASE(SIGUSR1);
				SIGNAL_CASE(SIGUSR2);
				SIGNAL_CASE(SIGVTALRM);
				SIGNAL_CASE(SIGWINCH);
				SIGNAL_CASE(SIGXCPU);
				SIGNAL_CASE(SIGXFSZ);

#if IS_LINUX
				SIGNAL_CASE(SIGPOLL);
				SIGNAL_CASE(SIGPWR);
				SIGNAL_CASE(SIGSTKFLT);
#endif	// IS_LINUX

#if IS_MACOS
				// Apple OSX and iOS (Darwin)
				SIGNAL_CASE(SIGEMT);
				SIGNAL_CASE(SIGIO);
				SIGNAL_CASE(SIGINFO);
#endif	// IS_MACOS

				default:
					return "UNKNOWN";
			}
		}

		// If `action` is `nullptr`, the signal will be ignored (`SIG_IGN`).
		template <typename... Tsignal>
		bool RegisterSignals(OV_SIG_ACTION action, int sig, Tsignal... sigs)
		{
			static_assert((std::is_same_v<std::decay_t<Tsignal>, int> && ...), "All signal arguments must be int");

			struct sigaction sa{};

			if (action != nullptr)
			{
				sa.sa_flags = SA_SIGINFO;
			}

			// sigemptyset is a macro on macOS, so :: breaks compilation
#if defined(__APPLE__)
			sigemptyset(&sa.sa_mask);
#else
			::sigemptyset(&sa.sa_mask);
#endif

			if (action != nullptr)
			{
	#if defined(__APPLE__)
				const bool mask_added = (sigaddset(&sa.sa_mask, sig) == 0) &&
										((sigaddset(&sa.sa_mask, sigs) == 0) && ...);
#else
				const bool mask_added = (::sigaddset(&sa.sa_mask, sig) == 0) &&
										((::sigaddset(&sa.sa_mask, sigs) == 0) && ...);
#endif

				if (mask_added == false)
				{
					logtc("Failed to add signals to the mask.");
					return false;
				}

				sa.sa_sigaction = action;
			}
			else
			{
				sa.sa_handler = SIG_IGN;
			}

			auto result = (::sigaction(sig, &sa, nullptr) == 0) &&
						  ((::sigaction(sigs, &sa, nullptr) == 0) && ...);

			if (result == false)
			{
				logtc("Failed to register signals.");
			}

			return result;
		}

#if DEBUG
		size_t GetProcessThreadCount()
		{
#	if IS_LINUX
			auto *directory = ::opendir("/proc/self/task");
			if (directory == nullptr)
			{
				return 0;
			}

			size_t thread_count = 0;

			while (auto *entry = ::readdir(directory))
			{
				if (entry->d_name[0] == '.')
				{
					continue;
				}

				thread_count++;
			}

			::closedir(directory);
			return thread_count;
#	else
			return 1;
#	endif	// IS_LINUX
		}
#endif	// DEBUG

		void RequestStop(int signum)
		{
			g_need_to_stop = true;
#ifdef __APPLE__
			g_stop_cv.notify_all();
#else
			::sem_post(&g_semaphore);
#endif
		}

		namespace handlers
		{
			volatile static sig_atomic_t g_is_abort_triggered = SIG_ATOMIC_FALSE;

			// Handler for abort signals
			//
			// Intentional signals (ignore)
			//     SIGABRT, SIGSEGV, SIGBUS, SIGILL, SIGFPE,
			//     SIGSYS, SIGXCPU, SIGXFSZ, SIGPOLL
			void Abort(int signum, siginfo_t *si, void *context)
			{
				// Allow only the first thread entering this handler to proceed.
				// If another signal arrives concurrently, skip re-entry and return.
				sig_atomic_t expected = SIG_ATOMIC_FALSE;
				if (::__atomic_compare_exchange_n(&g_is_abort_triggered, &expected, SIG_ATOMIC_TRUE, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) == false)
				{
					return;
				}

				char time_buffer[30]{};
				String file_name(PATH_MAX);
				time_t t			   = ::time(nullptr);

				const auto pid		   = Platform::GetProcessId();
				const auto tid		   = Platform::GetThreadId();
				const auto thread_name = Platform::GetThreadName();

				utsname uts{};
				::uname(&uts);

				// Ensure that the version string is not corrupted.
				g_ome_version[OV_COUNTOF(g_ome_version) - 1] = '\0';
				logtc("OME %s received signal %d (%s), interrupt.", g_ome_version, signum, GetSignalName(signum));
				logtc("- PID: %lu, OS: %s %s - %s, %s", pid, uts.sysname, uts.machine, uts.release, uts.version);

				// Exclude AbortHandler() from the stack trace
				const auto stack_trace = StackTrace::GetStackTrace(1);
				logtc("- Stack trace\n%s", stack_trace.CStr());

				const auto registers = StackTrace::GetRegisters(reinterpret_cast<const ucontext_t *>(context));
				logtc("- Registers\n%s", registers.CStr());

				// Ensure that g_dump_path is not corrupted.
				g_dump_fallback_directory[OV_COUNTOF(g_dump_fallback_directory) - 1] = '\0';

				std::tm local_time{};
				::localtime_r(&t, &local_time);

				std::ofstream ostream;

				{
					const char *file_prefix = "dumps/";
					bool fallback			= false;

					::strftime(time_buffer, OV_COUNTOF(time_buffer), "crash_%Y%m%d.dump", &local_time);

					file_name = file_prefix;
					file_name.Append(time_buffer);

					if (::mkdir(file_prefix, 0755) != 0)
					{
						if (errno != EEXIST)
						{
							logtc("Could not create a directory for crash dump: %s, use the fallback directory instead: %s",
								  file_prefix,
								  g_dump_fallback_directory);

							fallback = true;
						}
					}

					if (fallback == false)
					{
						auto stream = std::ofstream(file_name, std::ofstream::app);

						if (stream.is_open())
						{
							ostream = std::move(stream);
						}
						else
						{
							logte("Could not open dump file to write: %s, use the fallback directory instead: %s",
								  file_name.CStr(),
								  g_dump_fallback_directory);

							fallback = true;
						}
					}

					if (fallback)
					{
						file_prefix = g_dump_fallback_directory;

						file_name	= file_prefix;
						file_name.Append(time_buffer);

						ostream = std::ofstream(file_name, std::ofstream::app);

						if (ostream.is_open() == false)
						{
							logte("Could not open dump file to write: %s", file_name.CStr());
						}
					}
				}

				if (ostream.is_open())
				{
					::strftime(time_buffer, OV_COUNTOF(time_buffer), "%Y-%m-%dT%H:%M:%S%z", &local_time);

					ostream << "***** Crash dump *****" << std::endl;
					ostream << "OvenMediaEngine " << g_ome_version << " received signal " << signum << " (" << GetSignalName(signum) << ")" << std::endl;
					ostream << "- OS: " << uts.sysname << " " << uts.machine << " - " << uts.release << ", " << uts.version << std::endl;
					ostream << "- Time: " << time_buffer << ", PID: " << pid << ", TID: " << tid << " (" << thread_name << ")" << std::endl;

					ostream << "- Stack trace" << std::endl;
					ostream << stack_trace << std::endl;

					ostream << "- Registers" << std::endl;
					ostream << registers << std::endl;

					std::ifstream istream("/proc/self/maps", std::ifstream::in);

					if (istream.is_open())
					{
						ostream << "- Module maps" << std::endl;
						ostream << istream.rdbuf();
					}
					else
					{
						ostream << "(Could not read module maps)" << std::endl;
					}

					// need not call fstream::close() explicitly to close the file
				}

				::_exit(signum);
			}

			void MonitoredSignal(int signum)
			{
				static std::atomic<int> sigint_count = 0;

				switch (signum)
				{
					case SIGINT: {
						auto count = ++sigint_count;

						if (count >= MAX_SIGINT_COUNT)
						{
							logtc("The termination request has been made %d times by signal %d. OME is forcibly terminated.",
								  static_cast<int>(count),
								  signum);
							::_exit(1);
						}

						logtc("Caught terminate signal %d. Trying to terminate... (Repeat %d more times to forcibly terminate)",
							  signum,
							  static_cast<int>(MAX_SIGINT_COUNT - count));

						RequestStop(signum);
						return;
					}

					case SIGTERM:
						[[fallthrough]];
					case SIGQUIT:
						logtw("Caught terminate signal %d. OME is terminating...", signum);

						RequestStop(signum);
						return;

					case SIGHUP:
						logti("Received SIGHUP signal.");
						return;

					case SIGUSR1:
	#ifndef __APPLE__
					logtc("Trim result: %d", ::malloc_trim(0));
#endif
						return;
				}

#ifdef OME_USE_JEMALLOC
				if (g_use_jemalloc_signal)
				{
					if (signum == SIG_JEMALLOC_SHOW_STATS)
					{
						logtc("Jemalloc stats signal received.");
						JemallocShowStats();
						return;
					}
					else if (signum == SIG_JEMALLOC_TRIGGER_DUMP)
					{
						logtc("Jemalloc dump trigger signal received.");
						JemallocTriggerDump();
						return;
					}
				}
#endif	// OME_USE_JEMALLOC

				logtw("Unhandled monitored signal %d (%s)", signum, GetSignalName(signum));
			}
		}  // namespace handlers

		bool InitializeSignalMonitorThread()
		{
#ifdef OME_USE_JEMALLOC
			g_use_jemalloc_signal = true;

			if (SIG_JEMALLOC_TRIGGER_DUMP >= SIGRTMAX)
			{
				logtc("Cannot initialize SIGRT handler for jemalloc: `SIGRTMAX` is too small.");
				g_use_jemalloc_signal = false;
			}
#endif	// OME_USE_JEMALLOC

			sigset_t sig_set;

#if defined(__APPLE__)
			sigemptyset(&sig_set);
#else
			::sigemptyset(&sig_set);
#endif

			bool success = true;

#if defined(__APPLE__)
			success		 = success && (sigaddset(&sig_set, SIGINT) == 0);
			success		 = success && (sigaddset(&sig_set, SIGTERM) == 0);
			success		 = success && (sigaddset(&sig_set, SIGHUP) == 0);
			success		 = success && (sigaddset(&sig_set, SIGQUIT) == 0);
			success		 = success && (sigaddset(&sig_set, SIGUSR1) == 0);
#else
			success		 = success && (::sigaddset(&sig_set, SIGINT) == 0);
			success		 = success && (::sigaddset(&sig_set, SIGTERM) == 0);
			success		 = success && (::sigaddset(&sig_set, SIGHUP) == 0);
			success		 = success && (::sigaddset(&sig_set, SIGQUIT) == 0);
			success		 = success && (::sigaddset(&sig_set, SIGUSR1) == 0);
#endif

#ifdef OME_USE_JEMALLOC
			if (g_use_jemalloc_signal)
			{
				success = success && (::sigaddset(&sig_set, SIG_JEMALLOC_SHOW_STATS) == 0);
				success = success && (::sigaddset(&sig_set, SIG_JEMALLOC_TRIGGER_DUMP) == 0);
			}
#endif	// OME_USE_JEMALLOC

			if (success == false)
			{
				logtc("Failed to configure the monitored signal set.");
				return false;
			}

			// Block signals in the main thread so they are handled by the dedicated thread.
			// This mask will be inherited by all subsequently created threads.
			if (::pthread_sigmask(SIG_BLOCK, &sig_set, nullptr) != 0)
			{
				logtc("Failed to block monitored signals in the main thread.");
				return false;
			}

			try
			{
				g_signal_thread = std::thread(
					+[](sigset_t sig_set) {
						while (true)
						{
							int signum = 0;

							if (::sigwait(&sig_set, &signum) == 0)
							{
								handlers::MonitoredSignal(signum);
							}
						}
					},
					sig_set);

				::pthread_setname_np(g_signal_thread.native_handle(), "SignalMonitor");

				// Detach because we don't have a clean join point and it runs for the lifetime of the process.
				g_signal_thread.detach();
			}
			catch (const std::exception &ex)
			{
				logtc("Failed to start signal monitor thread: %s", ex.what());
				return false;
			}

			return true;
		}

		bool InitializeInternal()
		{
			//	 1) SIGHUP		 2) SIGINT		 3) SIGQUIT		 4) SIGILL		 5) SIGTRAP
			//	 6) SIGABRT		 7) SIGBUS		 8) SIGFPE		 9) SIGKILL		10) SIGUSR1
			//	11) SIGSEGV		12) SIGUSR2		13) SIGPIPE		14) SIGALRM		15) SIGTERM
			//	16) SIGSTKFLT	17) SIGCHLD		18) SIGCONT		19) SIGSTOP		20) SIGTSTP
			//	21) SIGTTIN		22) SIGTTOU		23) SIGURG		24) SIGXCPU		25) SIGXFSZ
			//	26) SIGVTALRM	27) SIGPROF		28) SIGWINCH	29) SIGIO		30) SIGPWR
			//	31) SIGSYS		34) SIGRTMIN	35) SIGRTMIN+1	36) SIGRTMIN+2	37) SIGRTMIN+3
			//	38) SIGRTMIN+4	39) SIGRTMIN+5	40) SIGRTMIN+6	41) SIGRTMIN+7	42) SIGRTMIN+8
			//	43) SIGRTMIN+9	44) SIGRTMIN+10	45) SIGRTMIN+11	46) SIGRTMIN+12	47) SIGRTMIN+13
			//	48) SIGRTMIN+14	49) SIGRTMIN+15	50) SIGRTMAX-14	51) SIGRTMAX-13	52) SIGRTMAX-12
			//	53) SIGRTMAX-11	54) SIGRTMAX-10	55) SIGRTMAX-9	56) SIGRTMAX-8	57) SIGRTMAX-7
			//	58) SIGRTMAX-6	59) SIGRTMAX-5	60) SIGRTMAX-4	61) SIGRTMAX-3	62) SIGRTMAX-2
			//	63) SIGRTMAX-1	64) SIGRTMAX

#if DEBUG
			const auto thread_count = GetProcessThreadCount();
			if (thread_count == 0)
			{
				logtc("Failed to inspect the current process thread count.");
				return false;
			}

			OV_ASSERT(thread_count == 1, "ov::sig::Initialize() must be called before any extra threads are created (thread_count: %zu)", thread_count);
			if (thread_count != 1)
			{
				logtc("ov::sig::Initialize() must be called before any extra threads are created (thread_count: %zu)", thread_count);
				return false;
			}
#endif	// DEBUG

#ifndef __APPLE__
			if (::sem_init(&g_semaphore, 0, 0) != 0)
			{
				logtc("Failed to initialize the signal semaphore (errno: %d)", errno);
				return false;
			}
#endif

::memset(g_ome_version, 0, sizeof(g_ome_version));
			::strncpy(g_ome_version, info::OmeVersion::GetInstance()->ToString().CStr(), OV_COUNTOF(g_ome_version) - 1);

			SetDumpFallbackPath(::ov_log_get_path());

			return InitializeSignalMonitorThread() &&
				   RegisterSignals(
					   handlers::Abort,
					   // Core dumped signal
					   SIGABRT,	 // `assert()`, `raise()`, `abort()`
					   SIGSEGV,	 // illegal memory access
					   SIGBUS,	 // illegal memory access
					   SIGILL,	 // execute a malformed instruction.
					   SIGFPE,	 // divide by zero
					   SIGSYS,	 // bad system call
					   SIGXCPU,	 // cpu time limit exceeded
					   SIGXFSZ	 // file size limit exceeded
#if IS_LINUX
					   , SIGPOLL	// pollable event
#endif							// IS_LINUX
					   ) &&

				   // Ignore `SIGPIPE`
				   RegisterSignals(
					   nullptr,
					   SIGPIPE);
		}

		void SetDumpFallbackPathInternal(const char *path)
		{
			static constexpr auto BUFFER_COUNT = OV_COUNTOF(g_dump_fallback_directory);

			::memset(g_dump_fallback_directory, 0, sizeof(g_dump_fallback_directory));
			::strncpy(g_dump_fallback_directory, path, BUFFER_COUNT - 1);

			if (g_dump_fallback_directory[0] != '\0')
			{
				::strncat(g_dump_fallback_directory, "/", BUFFER_COUNT - 1);
			}
			else
			{
				// No fallback path is set, so use the current directory
			}

			// To prevent overflow where memory is corrupt, add a null character explicitly to work as well as possible
			g_dump_fallback_directory[BUFFER_COUNT - 1] = '\0';
		}
	}  // namespace

	bool Initialize()
	{
		return InitializeInternal();
	}

	void SetDumpFallbackPath(const char *path)
	{
		SetDumpFallbackPathInternal(path);
	}

	bool WaitAndStop(int milliseconds)
	{
		if (g_need_to_stop.load())
		{
			return true;
		}

#ifdef __APPLE__
		{
			auto deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(milliseconds);
			std::unique_lock<std::mutex> lock(g_stop_mutex);
			g_stop_cv.wait_until(lock, deadline, [] { return g_need_to_stop.load(); });
		}
#else
		struct timespec ts;
		if (::clock_gettime(CLOCK_REALTIME, &ts) != 0)
		{
			OV_ASSERT2(false);
			::usleep(500 * 1000);
			return g_need_to_stop.load();
		}
		ts.tv_sec += milliseconds / 1000;
		ts.tv_nsec += (milliseconds % 1000) * 1000000;
		if (ts.tv_nsec >= 1000000000)
		{
			ts.tv_sec++;
			ts.tv_nsec -= 1000000000;
		}
		while (::sem_timedwait(&g_semaphore, &ts) == -1)
		{
			const auto error_number = errno;

			if (error_number == EINTR)
			{
				if (g_need_to_stop.load())
				{
					return true;
				}

				// Interrupted by a signal while waiting.
				// Retry because the absolute timeout in `ts` is still valid.
				continue;
			}

			if (error_number == ETIMEDOUT)
			{
				// The wait interval elapsed without a semaphore post.
				// Return the current stop state instead of unconditional `false`
				// so this path remains consistent even in an abnormal race/fallback case.
				break;
			}

			// Unexpected sem_timedwait() failure.
			// Fall through and return the current stop state.
			break;
		}
#endif

		return g_need_to_stop.load();
	}
}  // namespace ov::sig
