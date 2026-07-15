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
		void MutexLock(int n, const char *file, int line);
		void MutexUnlock(int n, const char *file, int line);

		// Used by OpenSSL
		static unsigned long GetThreadId();
		static void MutexLock(int mode, int n, const char *file, int line);

		std::mutex *_mutex_array = nullptr;

		std::shared_mutex _bio_mutex;
		std::map<String, BIO_METHOD *> _bio_method_map;
	};
}  // namespace ov
