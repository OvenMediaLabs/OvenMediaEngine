#include <gtest/gtest.h>
#include <openssl/ssl.h>

#include "server/https_server.h"

namespace
{
	class TestCertificate : public info::Certificate
	{
	public:
		explicit TestCertificate(std::shared_ptr<::Certificate> certificate)
		{
			_certificate = std::move(certificate);
		}
	};

	class TestHttpsServer : public http::svr::HttpsServer
	{
	public:
		TestHttpsServer()
			: HttpsServer("test", "test")
		{
		}

		SSL_CTX *GetSslContext()
		{
			ov::LockGuard lock_guard(_https_certificate_map_mutex);
			return _https_certificate_map.begin()->second->tls_context->GetSslContext();
		}
	};

	bool Handshake(SSL *client, SSL *server)
	{
		for (int i = 0; i < 10; i++)
		{
			int client_result = ::SSL_do_handshake(client);
			int server_result = ::SSL_do_handshake(server);

			if ((client_result == 1) && (server_result == 1))
			{
				return true;
			}

			if ((::SSL_get_error(client, client_result) == SSL_ERROR_SSL) || (::SSL_get_error(server, server_result) == SSL_ERROR_SSL))
			{
				return false;
			}
		}

		return false;
	}
}  // namespace

TEST(HttpsServer, NegotiatesTls12EcdheWithAnEcdsaCertificate)
{
	auto certificate = std::make_shared<::Certificate>();
	ASSERT_EQ(certificate->Generate(), nullptr);

	TestHttpsServer https_server;
	ASSERT_EQ(https_server.InsertCertificate(std::make_shared<TestCertificate>(certificate)), nullptr);

	auto server_ctx = https_server.GetSslContext();

	auto ciphers = ::SSL_CTX_get_ciphers(server_ctx);
	for (int i = 0; i < sk_SSL_CIPHER_num(ciphers); i++)
	{
		auto cipher = sk_SSL_CIPHER_value(ciphers, i);
		auto kx = ::SSL_CIPHER_get_kx_nid(cipher);
		// TLS 1.3 suites report NID_kx_any; the cipher list applies to TLS 1.2 only
		EXPECT_TRUE((kx == NID_kx_ecdhe) || (kx == NID_kx_any)) << ::SSL_CIPHER_get_name(cipher);
	}

	auto client_ctx = ::SSL_CTX_new(::TLS_client_method());
	ASSERT_NE(client_ctx, nullptr);
	::SSL_CTX_set_max_proto_version(client_ctx, TLS1_2_VERSION);

	auto client = ::SSL_new(client_ctx);
	auto server = ::SSL_new(server_ctx);
	BIO *client_bio;
	BIO *server_bio;
	ASSERT_EQ(::BIO_new_bio_pair(&client_bio, 0, &server_bio, 0), 1);
	::SSL_set_bio(client, client_bio, client_bio);
	::SSL_set_bio(server, server_bio, server_bio);
	::SSL_set_connect_state(client);
	::SSL_set_accept_state(server);

	EXPECT_TRUE(Handshake(client, server));
	EXPECT_EQ(::SSL_version(client), TLS1_2_VERSION);

	::SSL_free(client);
	::SSL_free(server);
	::SSL_CTX_free(client_ctx);
}
