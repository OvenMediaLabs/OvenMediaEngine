//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <string>
#include <stdint.h>

#define IS_WINDOWS                              0
#define IS_UNIX                                 0
#define IS_LINUX                                0
#define IS_BSD                                  0
#define IS_MACOS                                0
#define IS_ANDROID                              0
#define IS_IOS                                  0
#define IS_ARM                                  0
#define IS_64BITS                               0
#define IS_X86                                  0
// Reference
//   http://nadeausoftware.com/articles/2012/01/c_c_tip_how_use_compiler_predefined_macros_detect_operating_system
//   https://stackoverflow.com/questions/142508/how-do-i-check-os-with-a-preprocessor-directive
#if defined(_WIN32)
// Windows
#   define PLATFORM_NAME                        "Windows"
#   undef IS_WINDOWS
#   define IS_WINDOWS                           1
#elif defined(_WIN64)
// Windows
#   define PLATFORM_NAME                        "Windows"
#   undef IS_WINDOWS
#   define IS_WINDOWS                           1
#elif defined(__CYGWIN__) && !defined(_WIN32)
// Windows (Cygwin POSIX under Microsoft Window)
#   define PLATFORM_NAME                        "Windows (Cygwin)"
#   undef IS_WINDOWS
#   define IS_WINDOWS                           1
#elif defined(__ANDROID__)
// Android (implies Linux, so it must come first)
#   define PLATFORM_NAME                        "Android"
#   undef IS_ANDROID
#   define IS_ANDROID                           1
#elif defined(__linux__)
// Debian, Ubuntu, Gentoo, Fedora, openSUSE, RedHat, Rocky Linux, AlmaLinux and other
#   define PLATFORM_NAME                        "Linux"
#   undef IS_LINUX
#   define IS_LINUX                             1
#elif defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#   include <sys/param.h>
#   undef IS_UNIX
#   define IS_UNIX                              1
#   if (defined(__APPLE__) && defined(__MACH__))
// Apple OSX and iOS (Darwin)
#      include <TargetConditionals.h>
#      if TARGET_IPHONE_SIMULATOR == 1
// Apple iOS
#           define PLATFORM_NAME "iOS (Simulator)"
#           undef IS_IOS
#           define IS_IOS                       1
#      elif TARGET_OS_IPHONE == 1
// Apple iOS
#           define PLATFORM_NAME "iOS (Device)"
#           undef IS_IOS
#           define IS_IOS                       1
#      elif TARGET_OS_MAC == 1
// Apple OSX
#           define PLATFORM_NAME "macOS"
#           undef IS_MACOS
#           define IS_MACOS                     1
#      endif
#   elif defined(BSD)
// FreeBSD, NetBSD, OpenBSD, DragonFly BSD
#       define PLATFORM_NAME                    "BSD"
#       undef IS_BSD
#       define IS_BSD                           1
#   else
#       define PLATFORM_NAME                    "Unix"
#   endif
#elif defined(__hpux)
// HP-UX
#   define PLATFORM_NAME                        "HP-UX"
#   undef IS_UNIX
#   define IS_UNIX                              1
#elif defined(_AIX)
// IBM AIX
#   define PLATFORM_NAME                        "AIX"
#   undef IS_UNIX
#   define IS_UNIX                              1
#elif defined(__sun) && defined(__SVR4)
// Oracle Solaris, Open Indiana
#   define PLATFORM_NAME                        "Solaris"
#   undef IS_UNIX
#   define IS_UNIX                              1
#else
#   define PLATFORM_NAME                        "Unknown"
#endif

#if defined(__arm__) || defined(__aarch64__) || defined(__ARM_ARCH) || defined(__ARM_ARCH__)
#   undef IS_ARM
#   define IS_ARM                               1
#endif

#if defined(__x86_64__) || defined(__ppc64__) || defined(__aarch64__) || defined(__LP64__)
#   undef IS_64BITS
#   define IS_64BITS                            1
#endif

#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
#	undef IS_X86
#	define IS_X86                               1
#endif

namespace ov
{
	constexpr int MAX_THREAD_NAME_LENGTH = 16;
	class Platform
	{
	public:
		static const char *GetName();
		static uint64_t GetProcessId();
		static uint64_t GetThreadId();
		static const char *GetThreadName(pthread_t thread_id);
		static const char *GetThreadName();

	public:
		enum class CPUFeature
		{
			SSE,
			SSE2,
			SSE3,
			AVX,
			AVX2,
			NEON,
			NB
		};
		static bool IsSupportCPUFeature(CPUFeature feature);
		static void CPUID1(int out[4]);
		static void CPUIDEX(int out[4], int leaf, int subleaf);
		static unsigned long long XGETBV(unsigned int i);
	};
}