//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./certificate.h"

#include <openssl/core_names.h>

#include <utility>

Certificate::Certificate(X509 *cert)
{
	_certificate = cert;

	// Increments the reference count
	::X509_up_ref(_certificate);
}

Certificate::~Certificate()
{
}

ov::RaiiPtr<EVP_PKEY> Certificate::LoadPrivateKey(const char *pem_file)
{
	return LoadPem<EVP_PKEY>(
		LoadType::Pkey, pem_file,
		nullptr,
		[](int type, const OSSL_STORE_INFO *info, EVP_PKEY *previous_value) {
			return ::OSSL_STORE_INFO_get1_PKEY(info);
		},
		::EVP_PKEY_free);
}

ov::RaiiPtr<X509> Certificate::LoadCertificate(const char *pem_file)
{
	return LoadPem<X509>(
		LoadType::Cert, pem_file,
		nullptr,
		[](int type, const OSSL_STORE_INFO *info, X509 *previous_value) {
			// If there are multiple certificates, the first one is used
			if (previous_value == nullptr)
			{
				return ::OSSL_STORE_INFO_get1_CERT(info);
			}

			return previous_value;
		},
		::X509_free);
}

ov::RaiiPtr<STACK_OF(X509)> Certificate::LoadChainCertificate(const char *pem_file)
{
	return LoadPem<STACK_OF(X509)>(
		LoadType::Cert, pem_file,
		sk_X509_new_null(),
		[](int type, const OSSL_STORE_INFO *info, STACK_OF(X509) * previous_value) {
			::X509_add_cert(previous_value, ::OSSL_STORE_INFO_get1_CERT(info), X509_ADD_FLAG_DEFAULT);
			return previous_value;
		},
		X509StackFree);
}

std::shared_ptr<ov::OpensslError> Certificate::GenerateFromPem(
	const char *private_key_filename,
	const char *certificate_filename,
	const char *chain_certificate_filename)
{
	OV_ASSERT2(_private_key == nullptr);
	OV_ASSERT2(_certificate == nullptr);
	OV_ASSERT2(_chain_certificate == nullptr);

	if (
		(_private_key != nullptr) ||
		(_certificate != nullptr) ||
		(_chain_certificate != nullptr))
	{
		return std::make_shared<ov::OpensslError>("Certificate is already created");
	}

	auto private_key = LoadPrivateKey(private_key_filename);
	if (private_key == nullptr)
	{
		return std::make_shared<ov::OpensslError>();
	}

	auto certificate = LoadCertificate(certificate_filename);
	if (certificate == nullptr)
	{
		return std::make_shared<ov::OpensslError>();
	}

	if ((chain_certificate_filename != nullptr) && (chain_certificate_filename[0] != '\0'))
	{
		auto chain_certificate = LoadChainCertificate(chain_certificate_filename);
		if (chain_certificate == nullptr)
		{
			return std::make_shared<ov::OpensslError>();
		}

		_chain_certificate = std::move(chain_certificate);
		_chain_certificate_filename = chain_certificate_filename;
	}

	_private_key = std::move(private_key);
	_private_key_filename = private_key_filename;

	_certificate = std::move(certificate);
	_certificate_filename = certificate_filename;

	return nullptr;
}

std::shared_ptr<ov::OpensslError> Certificate::Generate()
{
	if (_certificate != nullptr)
	{
		return std::make_shared<ov::OpensslError>("Certificate is already created");
	}

	EVP_PKEY *pkey = MakeKey();
	if (pkey == nullptr)
	{
		return std::make_shared<ov::OpensslError>();
	}

	X509 *x509 = MakeCertificate(pkey);
	if (x509 == nullptr)
	{
		return std::make_shared<ov::OpensslError>();
	}

	_private_key = pkey;
	_certificate = x509;

	return nullptr;
}

// Make ECDSA Key
EVP_PKEY *Certificate::MakeKey()
{
	EVP_PKEY *key = ::EVP_PKEY_new();
	if (key == nullptr)
	{
		return nullptr;
	}

	bool succeeded = false;

	OSSL_PARAM params[2];
	char str[] = SN_X9_62_prime256v1;

	params[0] = ::OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME, str, 0);
	params[1] = ::OSSL_PARAM_construct_end();

	EVP_PKEY_CTX *pkey_context;

	do
	{
		pkey_context = ::EVP_PKEY_CTX_new_from_name(nullptr, "ec", nullptr);

		if (pkey_context == nullptr)
		{
			break;
		}

		if (::EVP_PKEY_keygen_init(pkey_context) <= 0)
		{
			break;
		}

		if (::EVP_PKEY_CTX_set_params(pkey_context, params) <= 0)
		{
			break;
		}

		if (::EVP_PKEY_keygen(pkey_context, &key) <= 0)
		{
			break;
		}

		succeeded = true;
	} while (false);

	if (succeeded == false)
	{
		loge("CERT", "Could not make a key: %s", ov::OpensslError().What());
	}

	::EVP_PKEY_CTX_free(pkey_context);

	return key;
}

// Make X509 Certificate
X509 *Certificate::MakeCertificate(EVP_PKEY *pkey)
{
	// Allocation
	X509 *x509 = ::X509_new();
	if (x509 == nullptr)
	{
		return nullptr;
	}

	// Set the version
	if (X509_set_version(x509, 2L) == 0)
	{
		X509_free(x509);
		return nullptr;
	}

	// BIGNUM Allocation
	BIGNUM *serial_number = BN_new();
	if (serial_number == nullptr)
	{
		X509_free(x509);
		return nullptr;
	}

	// Allocation
	X509_NAME *name = X509_NAME_new();
	if (name == nullptr)
	{
		X509_free(x509);
		BN_free(serial_number);
		return nullptr;
	}

	// Set the public key as pkey
	if (!X509_set_pubkey(x509, pkey))
	{
		X509_free(x509);
		return nullptr;
	}

	ASN1_INTEGER *asn1_serial_number;

	// Generate a random value and use it as the X509 serial number
	BN_rand(serial_number, 64, 0, 0);

	// Return the Serial Number within the certificate (it should not be released as an internal value)
	asn1_serial_number = X509_get_serialNumber(x509);

	// Convert the serial_number generated by pseudo_rand to ASN1_INTEGER and directly write it to the internal serial number pointer in x509
	BN_to_ASN1_INTEGER(serial_number, asn1_serial_number);

	// Add information to the certificate
	if (!X509_NAME_add_entry_by_NID(name, NID_commonName, MBSTRING_UTF8, (unsigned char *)CERT_NAME, -1, -1, 0) ||
		!X509_set_subject_name(x509, name) ||
		!X509_set_issuer_name(x509, name))
	{
		X509_free(x509);
		BN_free(serial_number);
		X509_NAME_free(name);
		return nullptr;
	}

	// Set the validity period of the certificate
	time_t epoch_off = 0;
	time_t now = time(nullptr);

	if (!X509_time_adj(X509_get_notBefore(x509), now + CertificateWindowInSeconds, &epoch_off) ||
		!X509_time_adj(X509_get_notAfter(x509), now + DefaultCertificateLifetimeInSeconds, &epoch_off))
	{
		X509_free(x509);
		BN_free(serial_number);
		X509_NAME_free(name);
		return nullptr;
	}

	// Signing, authentication
	if (!X509_sign(x509, pkey, EVP_sha256()))
	{
		X509_free(x509);
		BN_free(serial_number);
		X509_NAME_free(name);
		return nullptr;
	}

	BN_free(serial_number);
	X509_NAME_free(name);
	return x509;
}

void Certificate::Print()
{
	BIO *temp_memory_bio = BIO_new(BIO_s_mem());

	if (!temp_memory_bio)
	{
		loge("CERT", "Failed to allocate temporary memory bio");
		return;
	}
	X509_print_ex(temp_memory_bio, _certificate, XN_FLAG_SEP_CPLUS_SPC, 0);
	BIO_write(temp_memory_bio, "\0", 1);
	char *buffer;
	BIO_get_mem_data(temp_memory_bio, &buffer);
	logt("CERT", "%s", buffer);
	BIO_free(temp_memory_bio);

	logt("CERT", "Fingerprint sha-256 : %s", GetFingerprint("sha-256").CStr());

	if (_private_key != nullptr)
	{
		BIO *bio_out = BIO_new_fp(stdout, BIO_NOCLOSE);
		logt("CERT", "Public Key :");
		EVP_PKEY_print_public(bio_out, _private_key, 0, NULL);
		logt("CERT", "Private Key ::");
		EVP_PKEY_print_private(bio_out, _private_key, 0, NULL);
		BIO_free(bio_out);
	}
}

bool Certificate::GetDigestEVP(const ov::String &algorithm, const EVP_MD **mdp)
{
	const EVP_MD *md;

	if (algorithm == "md5")
	{
		md = EVP_md5();
	}
	else if (algorithm == "sha-1")
	{
		md = EVP_sha1();
	}
	else if (algorithm == "sha-224")
	{
		md = EVP_sha224();
	}
	else if (algorithm == "sha-256")
	{
		md = EVP_sha256();
	}
	else if (algorithm == "sha-384")
	{
		md = EVP_sha384();
	}
	else if (algorithm == "sha-512")
	{
		md = EVP_sha512();
	}
	else
	{
		return false;
	}

	*mdp = md;
	return true;
}

bool Certificate::ComputeDigest(const ov::String &algorithm)
{
	const EVP_MD *md;
	unsigned int n;

	if (!GetDigestEVP(algorithm, &md))
	{
		return false;
	}

	uint8_t digest[EVP_MAX_MD_SIZE];
	X509_digest(GetCertification(), md, digest, &n);
	_digest.Append(digest, n);
	_digest_algorithm = algorithm;
	return true;
}

EVP_PKEY *Certificate::GetPrivateKey() const
{
	return _private_key;
}

X509 *Certificate::GetCertification() const
{
	return _certificate;
}

STACK_OF(X509) * Certificate::GetChainCertification() const
{
	return _chain_certificate;
}

ov::String Certificate::GetFingerprint(const ov::String &algorithm)
{
	std::unique_lock<std::mutex> lock(_digest_mutex);
	// Create digest if not created yet
	if (_digest.GetLength() <= 0)
	{
		if (!ComputeDigest(algorithm))
		{
			return "";
		}
	}
	lock.unlock();

	ov::String fingerprint = ov::ToHexStringWithDelimiter(&_digest, ':');
	fingerprint.MakeUpper();
	return fingerprint;
}
