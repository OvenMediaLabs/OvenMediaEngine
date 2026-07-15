//==============================================================================
//
//  OvenMediaEngine
//
//  Created by getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#include "openssl_manager.h"

#include <base/ovlibrary/ovlibrary.h>
#include <openssl/crypto.h>
#include <openssl/dtls1.h>
#include <openssl/engine.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/tls1.h>
#include <openssl/x509v3.h>

#include <mutex>
#include <thread>

#include "./openssl_private.h"

namespace ov
{
	bool OpensslManager::InitializeOpenssl()
	{
		// Create mutexes for OpenSSL
		_mutex_array = new std::mutex[CRYPTO_num_locks()];

		if (_mutex_array == nullptr)
		{
			return false;
		}

		// Init OpenSSL
		::SSL_library_init();
		::SSL_load_error_strings();
		// ::FIPS_mode_set(1);
		::ERR_load_crypto_strings();
		::OpenSSL_add_all_algorithms();

		CRYPTO_set_id_callback(GetThreadId);
		CRYPTO_set_locking_callback(MutexLock);

		return true;
	}

	bool OpensslManager::ReleaseOpenSSL()
	{
		// ::FIPS_mode_set(0);

		{
			auto lock_guard = std::lock_guard(_bio_mutex);

			for (auto item : _bio_method_map)
			{
				::BIO_meth_free(item.second);
			}

			_bio_method_map.clear();
		}

		CRYPTO_set_id_callback(nullptr);
		CRYPTO_set_locking_callback(nullptr);

		SSL_COMP_free_compression_methods();

		ENGINE_cleanup();

		CONF_modules_free();
		::CONF_modules_unload(1);

		COMP_zlib_cleanup();

		ERR_free_strings();
		EVP_cleanup();

		CRYPTO_cleanup_all_ex_data();

		OV_SAFE_FUNC(_mutex_array, nullptr, delete[], );

		return true;
	}

	BIO_METHOD *OpensslManager::GetBioMethod(const String &name)
	{
		{
			auto shared_lock = std::shared_lock(_bio_mutex);
			auto item = _bio_method_map.find(name);

			if (item != _bio_method_map.end())
			{
				return item->second;
			}
		}

		auto lock_guard = std::lock_guard(_bio_mutex);

		// DCL
		auto item = _bio_method_map.find(name);

		if (item != _bio_method_map.end())
		{
			return item->second;
		}

		BIO_METHOD *bio_method = nullptr;

		bio_method = ::BIO_meth_new(BIO_TYPE_MEM, name.CStr());

		if (bio_method != nullptr)
		{
			_bio_method_map[name] = bio_method;
		}
		else
		{
			logte("Could not allocate BIO method: %s", OpensslError().What());
		}

		return bio_method;
	}

	bool OpensslManager::FreeBioMethod(const String &name)
	{
		auto lock_guard = std::lock_guard(_bio_mutex);

		// DCL
		auto item = _bio_method_map.find(name);

		if (item == _bio_method_map.end())
		{
			return false;
		}

		::BIO_meth_free(item->second);

		_bio_method_map.erase(item);

		return true;
	}

	unsigned long OpensslManager::GetThreadId()
	{
		std::hash<std::thread::id> hasher;

		return hasher(std::this_thread::get_id());
	}

	void OpensslManager::MutexLock(int n, const char *file, int line)
	{
		_mutex_array[n].lock();
	}

	void OpensslManager::MutexUnlock(int n, const char *file, int line)
	{
		_mutex_array[n].unlock();
	}

	void OpensslManager::MutexLock(int mode, int n, const char *file, int line)
	{
		if (mode & CRYPTO_LOCK)
		{
			OpensslManager::GetInstance()->MutexLock(n, file, line);
		}
		else
		{
			OpensslManager::GetInstance()->MutexUnlock(n, file, line);
		}
	}
}  // namespace ov