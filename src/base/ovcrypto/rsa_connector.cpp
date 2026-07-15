//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#include "rsa_connector.h"
#include "base_64.h"
 #include <openssl/err.h>

namespace ov
{
	std::shared_ptr<ov::Data> RsaConnector::Encrypt(const ov::String &public_key, const std::shared_ptr<ov::Data> &plain)
	{
		// Not yet implemented
		return nullptr;
	}

	std::shared_ptr<ov::Data> RsaConnector::Decrypt(const ov::String &private_key, const std::shared_ptr<ov::Data> &cipher)
	{
		// Not yet implemented
		return nullptr;
	}

	// Encrypt the plain text using the private key and encode the result in base64 format.
	ov::String RsaConnector::SignMessage(const ov::String &private_key, const ov::String &message)
	{
		// Not yet implemented
		return "";
	}

	// Decode the base64 encoded cipher text and decrypt the signature using the public key.
	// Returns true if the signature is valid, otherwise false.
	bool RsaConnector::VerifySignature(const ov::String &public_key, const ov::String &message, const ov::String &signature_base64)
	{
		// Decode the based64 encoded signature
		auto signature = ov::Base64::Decode(signature_base64);

		auto bio = BIO_new_mem_buf(public_key.CStr(), public_key.GetLength());
		if (bio == nullptr)
		{
			return false;
		}

		EVP_PKEY *pub_key = nullptr;
		pub_key = PEM_read_bio_PUBKEY(bio, &pub_key, nullptr, nullptr);
		if (pub_key == nullptr)
		{
			BIO_free_all(bio);
			return false;
		}

		EVP_MD_CTX *md_ctx = EVP_MD_CTX_new();
		if (md_ctx == nullptr)
		{
			EVP_PKEY_free(pub_key);
			BIO_free_all(bio);
			return false;
		}

		bool verify_result = false;
		do
		{
			if (EVP_DigestVerifyInit(md_ctx, nullptr, EVP_sha256(), nullptr, pub_key) <= 0)
			{
				break;
			}

			if (EVP_DigestVerifyUpdate(md_ctx, message.CStr(), message.GetLength()) <= 0)
			{
				break;
			}

			if (EVP_DigestVerifyFinal(md_ctx, signature->GetDataAs<unsigned char>(), signature->GetLength()) <= 0)
			{
				break;
			}

			verify_result = true;

		} while (0);

		// Free the resources
		EVP_MD_CTX_free(md_ctx);
		EVP_PKEY_free(pub_key);
		BIO_free_all(bio);

		return verify_result;
	}
}  // namespace ov
