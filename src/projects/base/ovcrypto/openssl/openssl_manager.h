//==============================================================================
//
//  OvenMediaEngine
//
//  Created by getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <openssl/bio.h>

#include "openssl_error.h"

namespace ov
{
	class OpensslManager : public Singleton<OpensslManager>
	{
	public:
		bool InitializeOpenssl();
		bool ReleaseOpenSSL();

		BIO_METHOD *GetBioMethod(const String &name);
		bool FreeBioMethod(const String &name);

	private:
		// Lock handoff driven by OpenSSL's locking callback:
		// lock and unlock happen in separate calls keyed by index into `_mutex_array`,
		// so the acquire and release are not statically pairable; analysis is disabled here.
		void MutexLock(int n, const char *file, int line) OV_NO_THREAD_SAFETY_ANALYSIS;
		void MutexUnlock(int n, const char *file, int line) OV_NO_THREAD_SAFETY_ANALYSIS;

		// Used by OpenSSL
		static unsigned long GetThreadId();
		static void MutexLock(int mode, int n, const char *file, int line);

		// Array of mutexes for OpenSSL's locking callback;
		// locked/unlocked by index, so it is left unguarded
		// (no single capability fits an index-keyed array).
		Mutex *_mutex_array = nullptr;

		SharedMutex _bio_mutex;
		std::map<String, BIO_METHOD *> _bio_method_map OV_GUARDED_BY(_bio_mutex);
	};
}  // namespace ov
