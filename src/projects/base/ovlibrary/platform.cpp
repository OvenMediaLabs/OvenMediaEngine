//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#include "platform.h"

#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <zconf.h>

#include <cstring>

#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
#define HAVE_CPUID_H 1
#endif

#if IS_X86
#	include <cpuid.h>
#endif

namespace ov
{
	const char *Platform::GetName()
	{
		return PLATFORM_NAME;
	}

	uint64_t Platform::GetProcessId()
	{
		return static_cast<uint64_t>(getpid());
	}

	uint64_t Platform::GetThreadId()
	{
#if IS_MACOS
		uint64_t tid;
		::pthread_threadid_np(nullptr, &tid);
		return tid;
#elif IS_LINUX
		return static_cast<uint64_t>(::syscall(SYS_gettid));
#else
		return 0ULL;
#endif
	}

	const char *Platform::GetThreadName(pthread_t thread_id)
	{
		static thread_local char name[16]{0};

		if (::pthread_getname_np(thread_id, name, 16) != 0)
		{
			name[0] = '\0';
		}

		return name;
	}

	// I disabled the cache: Occasionally, if GetThreadName() is called just before pthread_setname_np() is called,
	//                       the thread name may be set to OvenMediaEngine
	const char *Platform::GetThreadName()
	{
		return GetThreadName(::pthread_self());
	}

	// Check if the CPU supports a specific feature
	// feature: sse, sse2, sse3, avx, avx2, neon
	bool Platform::IsSupportCPUFeature(CPUFeature feature)
	{
#if IS_X86
		int r1[4] = {0};  // EAX, EBX, ECX, EDX for leaf 1
		CPUID1(r1);
		unsigned int ecx1 = (unsigned)r1[2];
		unsigned int edx1 = (unsigned)r1[3];

		if (feature == CPUFeature::SSE)
		{
			return (edx1 & (1u << 25)) != 0;  // CPUID.1:EDX.SSE
		}
		if (feature == CPUFeature::SSE2)
		{
			return (edx1 & (1u << 26)) != 0;  // CPUID.1:EDX.SSE2
		}
		if (feature == CPUFeature::SSE3)
		{
			return (ecx1 & (1u << 0)) != 0;	 // CPUID.1:ECX.SSE3
		}

		// AVX: CPUID.1:ECX.AVX(28) & OSXSAVE(27) && XGETBV(XCR0) has XMM(1) & YMM(2)
		if (feature == CPUFeature::AVX)
		{
			bool avx = (ecx1 & (1u << 28)) != 0;
			bool osx = (ecx1 & (1u << 27)) != 0;
			if (!(avx && osx))
			{
				return false;
			}
			unsigned long long xcr0 = XGETBV(0);
			return ((xcr0 & 0x6) == 0x6);  // XMM(bit1) & YMM(bit2)
		}

		// AVX2: CPUID.(EAX=7, ECX=0):EBX.AVX2(bit 5) + requires the above AVX OS support check
		if (feature == CPUFeature::AVX2)
		{
			//Check if AVX is available (OSXSAVE + XCR0)
			bool avx = (ecx1 & (1u << 28)) != 0;
			bool osx = (ecx1 & (1u << 27)) != 0;
			if (!(avx && osx))
			{
				return false;
			}
			unsigned long long xcr0 = XGETBV(0);
			if ((xcr0 & 0x6) != 0x6)
			{
				return false;
			}
			int r7[4] = {0};
			CPUIDEX(r7, 7, 0);
			unsigned int ebx7 = (unsigned)r7[1];
			return (ebx7 & (1u << 5)) != 0;	 // AVX2
		}

		// There is no 'neon' flag on x86.
		if (feature == CPUFeature::NEON)
		{
			return false;
		}

		return false;

#elif IS_ARM
		// ARM/ARM64: NEON Check
		if (feature == CPUFeature::NEON)
		{
#	if IS_64BITS
			// AArch64는 NEON(AdvSIMD)
			return true;
#	elif defined(__ARM_NEON) || defined(__ARM_NEON__)
			return true;
#	elif IS_LINUX
#		include <asm/hwcap.h>
#		include <sys/auxv.h>
			unsigned long hw = getauxval(AT_HWCAP);
#		ifdef HWCAP_NEON
			if (hw & HWCAP_NEON)
				return true;
#		endif
			return false;
#	else
			return false;
#	endif
		}

		return false;

#else
		return false;
#endif
	}

	void Platform::CPUIDEX(int out[4], int leaf, int subleaf)
	{
#if IS_X86		
		unsigned int a, b, c, d;
		__cpuid_count(leaf, subleaf, a, b, c, d);
		out[0] = int(a);
		out[1] = int(b);
		out[2] = int(c);
		out[3] = int(d);
#endif		
	}
	void Platform::CPUID1(int out[4])
	{
#if IS_X86
		unsigned int a, b, c, d;
		__cpuid(1, a, b, c, d);
		out[0] = int(a);
		out[1] = int(b);
		out[2] = int(c);
		out[3] = int(d);
#endif		
	}
	unsigned long long Platform::XGETBV(unsigned int i)
	{
#if IS_X86
		unsigned int eax, edx;
		__asm__ volatile(".byte 0x0f, 0x01, 0xd0" : "=a"(eax), "=d"(edx) : "c"(i));
		return ((unsigned long long)(edx) << 32) | eax;
#else
		return 0;
#endif
	}

}  // namespace ov
