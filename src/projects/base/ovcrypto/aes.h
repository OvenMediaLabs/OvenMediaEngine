//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <openssl/evp.h>

namespace ov
{
	class AES
	{
	public:
        // output must be allocated with input_length + AES_BLOCK_SIZE
		static bool EncryptWith128Cbc(const void *input, size_t input_length, void *output, const uint8_t *key, size_t key_length, const uint8_t *iv, size_t iv_length)
		{
            if (key_length != 16 || iv_length != 16)
            {
                return false;
            }

            [[maybe_unused]] size_t output_length = 0;

			EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
			if (ctx == nullptr)
			{
				return false;
			}

			if (EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr, (const unsigned char *)key, (const unsigned char *)iv) != 1)
			{
				EVP_CIPHER_CTX_free(ctx);
				return false;
			}

            EVP_CIPHER_CTX_set_padding(ctx, 0);

			int output_length_actual = 0;
			if (EVP_EncryptUpdate(ctx, (unsigned char *)output, &output_length_actual, (const unsigned char *)input, input_length) != 1)
			{
				EVP_CIPHER_CTX_free(ctx);
				return false;
			}
            output_length += output_length_actual;

			int output_length_final = 0;
			if (EVP_EncryptFinal_ex(ctx, (unsigned char *)output + output_length_actual, &output_length_final) != 1)
			{
				EVP_CIPHER_CTX_free(ctx);
				return false;
			}
            output_length += output_length_final;

			EVP_CIPHER_CTX_free(ctx);
			return true;
		}

		static bool EncryptWith128Ecb(const void *input, size_t input_length, void *output, const uint8_t *key, size_t key_length)
		{
			if (key_length != 16)
			{
				return false;
			}

			EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
			if (ctx == nullptr)
			{
				return false;
			}

			if (EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr, (const unsigned char *)key, nullptr) != 1)
			{
				EVP_CIPHER_CTX_free(ctx);
				return false;
			}

			EVP_CIPHER_CTX_set_padding(ctx, 0);

			int output_length_actual = 0;
			if (EVP_EncryptUpdate(ctx, (unsigned char *)output, &output_length_actual, (const unsigned char *)input, input_length) != 1)
			{
				EVP_CIPHER_CTX_free(ctx);
				return false;
			}

			int output_length_final = 0;
			if (EVP_EncryptFinal_ex(ctx, (unsigned char *)output + output_length_actual, &output_length_final) != 1)
			{
				EVP_CIPHER_CTX_free(ctx);
				return false;
			}

			EVP_CIPHER_CTX_free(ctx);
			return true;
		}

		bool Initialize(const EVP_CIPHER *cipher, const uint8_t *key, size_t key_length, const uint8_t *iv, size_t iv_length, bool padding)
		{
			_ctx = EVP_CIPHER_CTX_new();
			if (_ctx == nullptr)
			{
				return false;
			}

			if (EVP_EncryptInit_ex(_ctx, cipher, nullptr, (const unsigned char *)key, (const unsigned char *)iv) != 1)
			{
				EVP_CIPHER_CTX_free(_ctx);
				return false;
			}

			EVP_CIPHER_CTX_set_padding(_ctx, padding ? 1 : 0);

			_output_length = 0;

			return true;
		}

		bool Update(const void *input, size_t input_length, void *output)
		{
			int output_length_actual = 0;
			if (EVP_EncryptUpdate(_ctx, (unsigned char *)output, &output_length_actual, (const unsigned char *)input, input_length) != 1)
			{
				return false;
			}
			_output_length += output_length_actual;

			return true;
		}

		bool Finalize(void *output)
		{
			int output_length_final = 0;
			if (EVP_EncryptFinal_ex(_ctx, (unsigned char *)output, &output_length_final) != 1)
			{
				return false;
			}

			_output_length += output_length_final;

			EVP_CIPHER_CTX_free(_ctx);
			_ctx = nullptr;

			return true;
		}

		size_t GetOutputLength() const noexcept
		{
			return _output_length;
		}

		bool IsInitialized() const noexcept
		{
			return _ctx != nullptr;
		}

	private:
		EVP_CIPHER_CTX *_ctx = nullptr;
		size_t _output_length = 0;
	};
}  // namespace ov