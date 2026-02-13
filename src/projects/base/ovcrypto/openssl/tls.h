//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "./tls_bio_callback.h"
#include "./tls_context.h"

namespace ov
{
	class Tls
	{
	public:
		Tls() = default;
		virtual ~Tls();

		bool Initialize(const std::shared_ptr<TlsContext> &tls_context, const TlsBioCallback &callback, bool is_nonblocking);
		bool Uninitialize();

		void SetTlsHostName(const ov::String &host_name);

		// @return Returns SSL_ERROR_NONE on success
		int Accept();

		void Shutdown();

		std::shared_ptr<const OpensslError> Connect();

		// @return Returns SSL_ERROR_NONE on success
		int Read(void *buffer, size_t length, size_t *read_bytes);

		std::shared_ptr<Data> Read();

		// @return Returns SSL_ERROR_NONE on success
		int Write(const void *data, size_t length, size_t *written_bytes);
		int Write(const std::shared_ptr<const Data> &data, size_t *written_bytes);

		bool FlushInput();

		// @return The number of buffered and processed application data bytes that are pending and are available for immediate read
		int Pending() const;

		// @return SSL_has_pending() returns 1 if there is buffered record data in the SSL object and 0 otherwise.
		bool HasPending() const;

		std::shared_ptr<Certificate> GetPeerCertificate() const;
		bool ExportKeyingMaterial(unsigned long crypto_suite, const ov::String &label, std::shared_ptr<ov::Data> &server_key, std::shared_ptr<ov::Data> &client_key);

		// APIs related to SRTP
		unsigned long GetSelectedSrtpProfileId();

		bool GetKeySaltLen(unsigned long crypto_suite, size_t *key_len, size_t *salt_len) const;

		ov::String GetServerName() const;
		ov::String GetSelectedAlpnName() const;

		// Obtains a string in the BIO which allocated using BIO_new(BIO_s_mem())
		static ov::String StringFromX509Name(const X509_NAME *name);

		long GetVersion() const;
		ov::String GetSubjectName() const;
		ov::String GetIssuerName() const;

	protected:
		static BIO_METHOD *PrepareBioMethod();
		bool PrepareBio(const TlsBioCallback &callback);
		bool PrepareSsl(const std::shared_ptr<TlsContext> &tls_context);

		template <typename Treturn, Treturn default_value, class Tmember, Tmember member, typename... Targuments>
		static Treturn DoCallback(void *tls_instance, Targuments... args)
		{
			if (tls_instance == nullptr)
			{
				OV_ASSERT2(false);
				return default_value;
			}

			auto tls	 = static_cast<Tls *>(tls_instance);
			auto &target = tls->_callback.*member;

			if (target != nullptr)
			{
				return target(tls, args...);
			}

			return default_value;
		}

		static int TlsCreate(BIO *b);
		static long TlsCtrl(BIO *b, int cmd, long num, void *ptr);
		static int TlsRead(BIO *b, char *out, int outl);
		static int TlsWrite(BIO *b, const char *in, int inl);
		static int TlsPuts(BIO *b, const char *str);
		static int TlsDestroy(BIO *b);

		int GetError(int code);

	protected:
		bool _is_nonblocking	= false;

		X509 *_peer_certificate = nullptr;

		BIO *_bio				= nullptr;
		SSL *_ssl				= nullptr;

		TlsBioCallback _callback;

		std::mutex _ssl_lock;
	};
}  // namespace ov
