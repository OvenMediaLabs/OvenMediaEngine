//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "tls.h"

#include <utility>

#include "./openssl_manager.h"
#include "./openssl_private.h"

#define OV_TLS_BIO_METHOD_NAME "ov::Tls"

#define DO_CALLBACK_IF_AVAILABLE(return_type, default_value, tls_instance, callback_name, ...) \
	DoCallback<return_type, default_value, decltype(&TlsBioCallback::callback_name), &TlsBioCallback::callback_name>(tls_instance, ##__VA_ARGS__)

namespace ov
{
	Tls::~Tls()
	{
		Uninitialize();
	}

	bool Tls::Initialize(const std::shared_ptr<TlsContext> &tls_context, const TlsBioCallback &callback, bool is_nonblocking)
	{
		bool result = true;

		// Create BIO
		result		= result && PrepareBio(callback);
		// Create SSL
		result		= result && PrepareSsl(tls_context);

		if (result == false)
		{
			_callback = {};
		}

		return result;
	}

	bool Tls::Uninitialize()
	{
		if (_ssl != nullptr)
		{
			::SSL_shutdown(_ssl);
			::SSL_free(_ssl);
			_ssl = nullptr;
		}

		OV_SAFE_FUNC(_bio, nullptr, ::BIO_free, );

		OV_SAFE_RESET(_peer_certificate, nullptr, ::X509_free(_peer_certificate), _peer_certificate);

		_callback = {};

		return true;
	}

	void Tls::Shutdown()
	{
		if (_ssl != nullptr)
		{
			::SSL_shutdown(_ssl);
		}
	}

	BIO_METHOD *Tls::PrepareBioMethod()
	{
		static std::mutex bio_mutex;
		static BIO_METHOD *bio_method = nullptr;

		if (bio_method == nullptr)
		{
			auto lock_guard = std::lock_guard(bio_mutex);

			if (bio_method == nullptr)
			{
				bio_method = OpensslManager::GetInstance()->GetBioMethod(OV_TLS_BIO_METHOD_NAME);

				if (bio_method != nullptr)
				{
					int result = 1;

					result	   = result && ::BIO_meth_set_create(bio_method, TlsCreate);
					result	   = result && ::BIO_meth_set_ctrl(bio_method, TlsCtrl);
					result	   = result && ::BIO_meth_set_read(bio_method, TlsRead);
					result	   = result && ::BIO_meth_set_write(bio_method, TlsWrite);
					result	   = result && ::BIO_meth_set_puts(bio_method, TlsPuts);
					result	   = result && ::BIO_meth_set_destroy(bio_method, TlsDestroy);

					if (result == 0)
					{
						OpensslManager::GetInstance()->FreeBioMethod(OV_TLS_BIO_METHOD_NAME);
						::BIO_meth_free(bio_method);

						bio_method = nullptr;
					}
				}
			}
		}

		OV_ASSERT2(bio_method != nullptr);

		return bio_method;
	}

	bool Tls::PrepareBio(const TlsBioCallback &callback)
	{
		_callback			   = callback;

		BIO_METHOD *bio_method = PrepareBioMethod();

		_bio				   = ::BIO_new(bio_method);

		if (_bio == nullptr)
		{
			OV_ASSERT2(false);
			return false;
		}

		::BIO_set_data(_bio, this);

		return true;
	}

	bool Tls::PrepareSsl(const std::shared_ptr<TlsContext> &tls_context)
	{
		// Create a SSL session
		decltype(_ssl) ssl(::SSL_new(tls_context->GetSslContext()));

		if (ssl == nullptr)
		{
			return false;
		}

		// Setup the session
		::SSL_set_bio(ssl, _bio, _bio);
		::SSL_set_read_ahead(ssl, 1);
		::SSL_set_mode(ssl, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

		// To prevent double free (_bio will be freed when calling SSL_free())
		::BIO_up_ref(_bio);

		_ssl = std::move(ssl);

		return true;
	}

	int Tls::GetError(int code)
	{
		OV_ASSERT2(_ssl != nullptr);

		return ::SSL_get_error(_ssl, code);
	}

	void Tls::SetTlsHostName(const ov::String &host_name)
	{
		OV_ASSERT2(_ssl != nullptr);

		::SSL_set_tlsext_host_name(_ssl, host_name.CStr());
	}

	int Tls::Accept()
	{
		if (_ssl == nullptr)
		{
			logte("SSL is nullptr");
			return -1;
		}

		int result = ::SSL_accept(_ssl);

		switch (result)
		{
			case 1: {
				// The TLS/SSL handshake was successfully completed, a TLS/SSL connection has been established.
				return SSL_ERROR_NONE;
			}

			case 0:
				// The TLS/SSL handshake was not successful but was shut down controlled and by the specifications of the TLS/SSL protocol.
				// Call SSL_get_error() with the return value ret to find out the reason.
				break;

			default:
				// The TLS/SSL handshake was not successful because a fatal error occurred either at the protocol level or a connection failure occurred.
				// The shutdown was not clean.
				// It can also occur of action is need to continue the operation for non-blocking BIOs.
				// Call SSL_get_error() with the return value ret to find out the reason.
				break;
		}

		int error = GetError(result);

		switch (error)
		{
			case SSL_ERROR_WANT_READ:
			case SSL_ERROR_WANT_WRITE:
				// The operation did not complete; the same TLS/SSL I/O function should be called again later.
				break;

			default:
				// Another error
				logte("An error occurred while accept SSL connection: %s", OpensslError().What());
				break;
		}

		return error;
	}

	std::shared_ptr<const OpensslError> Tls::Connect()
	{
		if (_ssl == nullptr)
		{
			logte("SSL is nullptr");
			return std::make_shared<OpensslError>("SSL is nullptr");
		}

		// @return Returns
		//         0: The TLS/SSL handshake was not successful but was shut down controlled and by the specifications of the TLS/SSL protocol. Call SSL_get_error() with the return value ret to find out the reason.
		//         1: The TLS/SSL handshake was successfully completed, a TLS/SSL connection has been established.
		//         <0: The TLS/SSL handshake was not successful, because a fatal error occurred either at the protocol level or a connection failure occurred.
		//             The shutdown was not clean. It can also occur if action is needed to continue the operation for nonblocking BIOs. Call SSL_get_error() with the return value ret to find out the reason.
		auto result = ::SSL_connect(_ssl);

		if (result == 1)
		{
			// Connected
			_peer_certificate = ::SSL_get_peer_certificate(_ssl);

			return nullptr;
		}

		return std::make_shared<OpensslError>(_ssl, result);
	}

	int Tls::Read(void *buffer, size_t length, size_t *read_bytes)
	{
		OV_ASSERT2(_ssl != nullptr);

		int result = ::SSL_read(_ssl, buffer, static_cast<int>(length));

		if (result > 0)
		{
			// The read operation was successful.
			// The return value is the number of bytes actually read from the TLS/SSL connection.
			if (read_bytes != nullptr)
			{
				*read_bytes = static_cast<size_t>(result);
			}

			return SSL_ERROR_NONE;
		}

		// The read operation was not successful, because either the connection was closed,
		// an error occurred or action must be taken by the calling process.
		// Call SSL_get_error(3) with the return value ret to find out the reason.
		return GetError(result);
	}

	std::shared_ptr<ov::Data> Tls::Read()
	{
		// lock
		std::lock_guard lock(_ssl_lock);

		auto data = std::make_shared<ov::Data>(65535);

		unsigned char buf[1024];

		bool stop = false;

		while (stop == false)
		{
			size_t read_bytes = 0;

			int error		  = Read(buf, OV_COUNTOF(buf), &read_bytes);
			bool append_data  = false;
			int read_errno	  = errno;

			switch (error)
			{
				case SSL_ERROR_ZERO_RETURN:
					// Read successfully, and the connection was closed
					append_data = true;
					stop		= true;
					break;

				case SSL_ERROR_NONE:
					// Read successfully
					append_data = true;
					if (_is_nonblocking == false)
					{
						// Do not need to read whole data in blocking mode
						stop = true;
					}
					break;

				case SSL_ERROR_SSL:
					append_data = true;
					stop		= true;

					break;

				case SSL_ERROR_WANT_READ:
					// Need more data
					stop = true;
					break;

				case SSL_ERROR_SYSCALL:
					if (read_errno == 0)
					{
						append_data = true;
						stop		= true;
						break;
					}

					[[fallthrough]];

				default:
					OpensslError ssl_error;

					// Another error occurred
					OV_ASSERT(read_bytes == 0, "read_bytes must be 0, but %zu (code: %d)", read_bytes, error);

					logtw("Tls::Read() returns %d (errno: %d): %s", error, read_errno, ssl_error.What());

					data = nullptr;
					stop = true;
					break;
			}

			if (append_data && (read_bytes > 0) && (data != nullptr))
			{
				if (data->Append(buf, read_bytes) == false)
				{
					OV_ASSERT2(false);
					data = nullptr;
					stop = true;
				}
			}
		}

		return data;
	}

	int Tls::Write(const void *data, size_t length, size_t *written_bytes)
	{
		OV_ASSERT2(_ssl != nullptr);

		// lock
		std::lock_guard lock(_ssl_lock);

		size_t write_size = 0;

		do
		{
			// max 16384(2^14)byte write
			int result = ::SSL_write(_ssl, (void *)((char *)data + write_size), static_cast<int>(length - write_size));

			if (result <= 0)
			{
				// The write operation was not successful, because either the connection was closed,
				// an error occurred or action must be taken by the calling process.
				// Call SSL_get_error() with the return value ret to find out the reason.

				auto get_error = GetError(result);
				logtt("Tls::Write()::SSL_write returns %d %d, errno: %d", result, get_error, errno);
				return get_error;
			}

			write_size += static_cast<size_t>(result);
		} while (write_size < length);

		// The write operation was successful,
		// the return value is the number of bytes actually written to the TLS/SSL connection.
		if (written_bytes != nullptr)
		{
			*written_bytes += write_size;
		}

		return SSL_ERROR_NONE;
	}

	int Tls::Write(const std::shared_ptr<const ov::Data> &data, size_t *written_bytes)
	{
		return Write(data->GetData(), data->GetLength(), written_bytes);
	}

	bool Tls::FlushInput()
	{
		unsigned char buf[1024];

		while (HasPending())
		{
			size_t read_bytes;

			int error = Read(buf, OV_COUNTOF(buf), &read_bytes);

			if (error != SSL_ERROR_NONE)
			{
				OV_ASSERT2(false);
				return false;
			}
		}

		return true;
	}

	int Tls::Pending() const
	{
		OV_ASSERT2(_ssl != nullptr);

		// SSL_pending() returns the number of buffered and processed application data bytes that
		// are pending and are available for immediate read.
		return ::SSL_pending(_ssl);
	}

	bool Tls::HasPending() const
	{
		OV_ASSERT2(_ssl != nullptr);

		// SSL_has_pending() returns 1 if there is buffered record data in the SSL object and 0 otherwise.
		return (::SSL_has_pending(_ssl) == 1);
	}

	int Tls::TlsCreate(BIO *b)
	{
		::BIO_set_shutdown(b, 0);
		::BIO_set_init(b, 1);
		::BIO_set_data(b, nullptr);
		::BIO_set_flags(b, 0);

		return 1;
	}

	long Tls::TlsCtrl(BIO *b, int cmd, long num, void *ptr)
	{
		return DO_CALLBACK_IF_AVAILABLE(long, 0, BIO_get_data(b), ctrl_callback, cmd, num, ptr);
	}

	int Tls::TlsRead(BIO *b, char *out, int outl)
	{
		BIO_clear_retry_flags(b);

		OV_ASSERT2(out != nullptr);
		OV_ASSERT2(outl >= 0);

		auto read_bytes = DO_CALLBACK_IF_AVAILABLE(ssize_t, -1, BIO_get_data(b), read_callback, out, static_cast<size_t>(outl));

		if (read_bytes > 0)
		{
			return static_cast<int>(read_bytes);
		}
		else if (read_bytes == 0)
		{
			// No data to read
			BIO_set_retry_read(b);
		}
		else
		{
			// Error
		}

		return -1;
	}

	int Tls::TlsWrite(BIO *b, const char *in, int inl)
	{
		BIO_clear_retry_flags(b);

		OV_ASSERT2(in != nullptr);
		OV_ASSERT2(inl >= 0);

		logtp("Trying to write %d bytes...\n%s", inl, ov::Dump(in, inl).CStr());

		auto written_bytes = DO_CALLBACK_IF_AVAILABLE(ssize_t, -1, BIO_get_data(b), write_callback, in, static_cast<size_t>(inl));

		logtt("Written: %zd/%d", written_bytes, inl);

		if (written_bytes > 0)
		{
			return static_cast<int>(written_bytes);
		}
		else if (written_bytes == 0)
		{
			// No data to write
			BIO_set_retry_write(b);
		}
		else
		{
			// Error
		}

		return written_bytes;
	}

	int Tls::TlsPuts(BIO *b, const char *str)
	{
		return TlsWrite(b, str, static_cast<int>(::strlen(str)));
	}

	int Tls::TlsDestroy(BIO *b)
	{
		return DO_CALLBACK_IF_AVAILABLE(bool, false, BIO_get_data(b), destroy_callback) ? 1 : 0;
	}

	std::shared_ptr<Certificate> Tls::GetPeerCertificate() const
	{
		OV_ASSERT2(_ssl != nullptr);

		X509 *peer_certificate = ::SSL_get_peer_certificate(_ssl);

		if (peer_certificate == nullptr)
		{
			logte("An error occurred while get peer certificate");
			return nullptr;
		}

		auto certificate = std::make_shared<Certificate>(peer_certificate);

		::X509_free(peer_certificate);

		return certificate;
	}

	bool Tls::ExportKeyingMaterial(unsigned long crypto_suite, const ov::String &label, std::shared_ptr<ov::Data> &server_key, std::shared_ptr<ov::Data> &client_key)
	{
		OV_ASSERT2(_ssl != nullptr);

		size_t key_len;
		size_t salt_len;

		if (GetKeySaltLen(crypto_suite, &key_len, &salt_len) == false)
		{
			return false;
		}

		size_t capacity = (key_len + salt_len) * 2;

		auto key_data	= std::make_shared<ov::Data>(capacity);
		key_data->SetLength(capacity);

		auto key_buffer = key_data->GetWritableDataAs<uint8_t>();

		int result		= ::SSL_export_keying_material(
			 _ssl,
			 key_buffer, key_data->GetLength(),
			 label.CStr(), label.GetLength(),
			 nullptr, 0, false);

		if (result != 1)
		{
			// SSL_export_keying_material() returns 0 or -1 on failure or 1 on success.
			return false;
		}

		client_key->Clear();
		server_key->Clear();

		client_key->Append(key_buffer, key_len);
		key_buffer += key_len;

		server_key->Append(key_buffer, key_len);
		key_buffer += key_len;

		client_key->Append(key_buffer, salt_len);
		key_buffer += salt_len;

		server_key->Append(key_buffer, salt_len);

		return true;
	}

	unsigned long Tls::GetSelectedSrtpProfileId()
	{
		OV_ASSERT2(_ssl != nullptr);

		SRTP_PROTECTION_PROFILE *srtp_protection_profile = ::SSL_get_selected_srtp_profile(_ssl);

		if (srtp_protection_profile == nullptr)
		{
			return 0;
		}

		return srtp_protection_profile->id;
	}

	bool Tls::GetKeySaltLen(unsigned long crypto_suite, size_t *key_len, size_t *salt_len) const
	{
		switch (crypto_suite)
		{
			case SRTP_AES128_CM_SHA1_32:
			case SRTP_AES128_CM_SHA1_80:
				// SRTP_AES128_CM_HMAC_SHA1_32 and SRTP_AES128_CM_HMAC_SHA1_80 are defined
				// in RFC 5764 to use a 128 bits key and 112 bits salt for the cipher.
				*key_len  = 16L;
				*salt_len = 14L;
				break;

			case SRTP_AEAD_AES_128_GCM:
				// SRTP_AEAD_AES_128_GCM is defined in RFC 7714 to use a 128 bits key and
				// a 96 bits salt for the cipher.
				*key_len  = 16L;
				*salt_len = 12L;
				break;

			case SRTP_AEAD_AES_256_GCM:
				// SRTP_AEAD_AES_256_GCM is defined in RFC 7714 to use a 256 bits key and
				// a 96 bits salt for the cipher.
				*key_len  = 32L;
				*salt_len = 12L;
				break;

			default:
				return false;
		}

		return true;
	}

	ov::String Tls::GetServerName() const
	{
		return ::SSL_get_servername(_ssl, TLSEXT_NAMETYPE_host_name);
	}

	ov::String Tls::GetSelectedAlpnName() const
	{
		const unsigned char *data = NULL;
		unsigned int len		  = 0;

		SSL_get0_alpn_selected(_ssl, &data, &len);

		return ov::String(reinterpret_cast<const char *>(data), len);
	}

	long Tls::GetVersion() const
	{
		// Holds _peer_certificate to prevent referencing nullptr
		auto peer_certificate = _peer_certificate;
		return (peer_certificate != nullptr) ? X509_get_version(peer_certificate) : 0;
	}

	ov::String Tls::StringFromX509Name(const X509_NAME *name)
	{
		BIO *bio = ::BIO_new(::BIO_s_mem());
		::X509_NAME_print(bio, name, 0);
		int keylen = BIO_pending(bio);

		ov::String key;
		key.SetLength(keylen);
		::BIO_read(bio, key.GetBuffer(), keylen);
		::BIO_free_all(bio);

		return key;
	}

	ov::String Tls::GetSubjectName() const
	{
		// Holds _peer_certificate to prevent referencing nullptr
		auto peer_certificate = _peer_certificate;
		return (peer_certificate != nullptr) ? StringFromX509Name(::X509_get_subject_name(peer_certificate)) : "";
	}

	ov::String Tls::GetIssuerName() const
	{
		// Holds _peer_certificate to prevent referencing nullptr
		auto peer_certificate = _peer_certificate;
		return (peer_certificate != nullptr) ? StringFromX509Name(::X509_get_issuer_name(peer_certificate)) : "";
	}

};	// namespace ov
