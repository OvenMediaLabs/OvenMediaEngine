//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "message_digest.h"
#include "ovcrypto_private.h"

#include <openssl/sha.h>
#include <openssl/md5.h>
#include <openssl/ossl_typ.h>
#include <openssl/evp.h>

#include <base/ovlibrary/ovlibrary.h>

namespace ov
{
	MessageDigest::MessageDigest()
		: _context(nullptr)
	{
	}

	MessageDigest::~MessageDigest()
	{
		OV_ASSERT(_context == nullptr, "Memory leak detected: Context is not destroyed: %p", _context);
	}

	bool MessageDigest::Create(CryptoAlgorithm algorithm)
	{
		OV_ASSERT(_context == nullptr, "Context is already created");

		if(_context != nullptr)
		{
			return false;
		}

		EVP_MD_CTX *context = EVP_MD_CTX_new();

		if(context == nullptr)
		{
			logtw("Could not allocate context");

			return false;
		}

		EVP_MD_CTX_init(context);

		switch(algorithm)
		{
			case CryptoAlgorithm::Md5:
				EVP_DigestInit_ex(context, EVP_md5(), nullptr);
				break;

			case CryptoAlgorithm::Sha1:
				EVP_DigestInit_ex(context, EVP_sha1(), nullptr);
				break;

			case CryptoAlgorithm::Sha224:
				EVP_DigestInit_ex(context, EVP_sha224(), nullptr);
				break;

			case CryptoAlgorithm::Sha256:
				EVP_DigestInit_ex(context, EVP_sha256(), nullptr);
				break;

			case CryptoAlgorithm::Sha384:
				EVP_DigestInit_ex(context, EVP_sha384(), nullptr);
				break;

			case CryptoAlgorithm::Sha512:
				EVP_DigestInit_ex(context, EVP_sha512(), nullptr);
				break;

			case CryptoAlgorithm::Sha512_224:
				EVP_DigestInit_ex(context, EVP_sha512_224(), nullptr);
				break;

			case CryptoAlgorithm::Sha512_256:
				EVP_DigestInit_ex(context, EVP_sha512_256(), nullptr);
				break;

			default:
				logtw("Could not create MessageDigest for algorithm: %d", ToUnderlyingType(algorithm));
				Destroy();
				return false;
		}

		_algorithm = algorithm;
		_context = context;

		return true;
	}

	bool MessageDigest::Destroy()
	{
		OV_ASSERT2(_context != nullptr);
		OV_ASSERT2(_algorithm != CryptoAlgorithm::Unknown);

		if(_context != nullptr)
		{
			EVP_MD_CTX_destroy((EVP_MD_CTX *)_context);
			_context = nullptr;
		}

		_algorithm = CryptoAlgorithm::Unknown;

		return true;
	}

	bool MessageDigest::Reset()
	{
		CryptoAlgorithm algorithm = _algorithm;

		return Destroy() && Create(algorithm);
	}

	unsigned int MessageDigest::Size(CryptoAlgorithm algorithm) noexcept
	{
		switch(algorithm)
		{
			case CryptoAlgorithm::Unknown:
				break;

			case CryptoAlgorithm::Md5:
				return MD5_DIGEST_LENGTH;

			case CryptoAlgorithm::Sha1:
				return SHA_DIGEST_LENGTH;

			case CryptoAlgorithm::Sha224:
				return SHA224_DIGEST_LENGTH;

			case CryptoAlgorithm::Sha256:
				return SHA256_DIGEST_LENGTH;

			case CryptoAlgorithm::Sha384:
				return SHA384_DIGEST_LENGTH;

			case CryptoAlgorithm::Sha512:
				return SHA512_DIGEST_LENGTH;

			case CryptoAlgorithm::Sha512_224:
				return SHA224_DIGEST_LENGTH;

			case CryptoAlgorithm::Sha512_256:
				return SHA256_DIGEST_LENGTH;
		}

		OV_ASSERT(false, "Invalid algorithm: %d", algorithm);
		return 0;
	}

	unsigned int MessageDigest::Size() const noexcept
	{
		// EVP_MD_size()를 사용해도 됨
		return MessageDigest::Size(_algorithm);
	}

	bool MessageDigest::Update(const void *buffer, size_t length)
	{
		OV_ASSERT2(_context != nullptr);

		if(_context == nullptr)
		{
			return false;
		}

		EVP_DigestUpdate((EVP_MD_CTX *)_context, buffer, length);

		return true;
	}

	bool MessageDigest::Update(const std::shared_ptr<const ov::Data> &data)
	{
		return Update(data->GetData(), data->GetLength());
	}

	bool MessageDigest::Finish(void *buffer, size_t length)
	{
		OV_ASSERT2(_context != nullptr);

		if(_context == nullptr)
		{
			return false;
		}

		if(length < Size())
		{
			// 버퍼가 부족함
			OV_ASSERT(false, "Not enough buffer");

			return false;
		}

		// TODO: openssl은 데이터를 int크기 밖에 받질 못함
		unsigned int final_length = 0;
		EVP_DigestFinal_ex((EVP_MD_CTX *)_context, reinterpret_cast<unsigned char *>(buffer), &final_length);

		// TODO: 항상 같게 나오는지 확인해봐야 함
		OV_ASSERT2(final_length == Size());

		return (final_length == Size());
	}

	std::shared_ptr<ov::Data> MessageDigest::Finish()
	{
		std::shared_ptr<ov::Data> data = std::make_shared<ov::Data>(Size());

		Finish(data->GetWritableData(), data->GetCapacity());

		return data;
	}

	bool MessageDigest::ComputeHmac(CryptoAlgorithm algorithm, const void *key, size_t key_length, const void *input, size_t input_length, void *output, size_t output_length)
	{
		MessageDigest digest;

		if(digest.Create(algorithm) == false)
		{
			return false;
		}

		// 현재 코드에서는 SHA-256 이하만 처리 가능
		constexpr unsigned int block_length = 64;
		const unsigned int digest_size = digest.Size();

		// padding을 위해, 키를 임시 버퍼에 복사함
		uint8_t new_key[block_length] = { 0 };

		if(key_length > block_length)
		{
			// key 길이가 block 길이보다 크면 hash 한 뒤 그 결과를 key로 사용함
			if(digest.Update(key, key_length) && digest.Finish(new_key, OV_COUNTOF(new_key)))
			{
				// key hash 성공

				// 아래에서 재사용 할 수 있게 초기화 함
				digest.Reset();
			}
			else
			{
				// key hash 도중 오류 발생
				digest.Destroy();
				return false;
			}
		}
		else
		{
			// key 길이가 block 길이보다 작으면 그냥 복사
			::memcpy(new_key, key, key_length);
		}

		// salt를 위해 위에서 계산한 key로 부터 패딩 계산

		// input padding
		uint8_t input_pad[block_length] = { 0 };
		// output padding
		uint8_t output_pad[block_length] = { 0 };

		for(unsigned int index = 0; index < block_length; index++)
		{
			input_pad[index] = 0x36 ^ new_key[index];
			output_pad[index] = 0x5C ^ new_key[index];
		}

		// padding 데이터 및 입력 데이터를 hash
#if defined(__clang__)
		uint8_t inner[digest_size];
		memset(&inner, 0, digest_size);
#else
		uint8_t inner[digest_size] = { 0 };
#endif
		bool result = true;

		// inner hash
		// digest input padding
		result = result && digest.Update(input_pad, OV_COUNTOF(input_pad));
		// digest input
		result = result && digest.Update(input, input_length);
		result = result && digest.Finish(inner, digest_size);

		result = result && digest.Reset();

		// outer hash
		// digest output padding
		result = result && digest.Update(output_pad, OV_COUNTOF(output_pad));
		// digest inner hash
		result = result && digest.Update(inner, digest_size);
		result = result && digest.Finish(output, output_length);

		digest.Destroy();

		return result;
	}

	std::shared_ptr<ov::Data> MessageDigest::ComputeHmac(CryptoAlgorithm algorithm, const std::shared_ptr<const ov::Data> &key, const std::shared_ptr<const ov::Data> &input)
	{
		std::shared_ptr<ov::Data> data = std::make_shared<ov::Data>();

		data->SetLength(MessageDigest::Size(algorithm));

		if(ComputeHmac(algorithm, key->GetData(), key->GetLength(), input->GetData(), input->GetLength(), data->GetWritableData(), data->GetLength()))
		{
			return data;
		}

		return nullptr;
	}

	bool MessageDigest::ComputeDigest(CryptoAlgorithm algorithm, const void *input, size_t input_length, void *output, size_t max_output_length)
	{
		MessageDigest digest;

		if(digest.Create(algorithm) == false)
		{
			return false;
		}

		bool result = true;

		result = result && digest.Update(input, input_length);
		result = result && digest.Finish(output, max_output_length);

		digest.Destroy();

		return result;
	}

	std::shared_ptr<ov::Data> MessageDigest::ComputeDigest(CryptoAlgorithm algorithm, const void *input, size_t input_length)
	{
		std::shared_ptr<ov::Data> data = std::make_shared<ov::Data>();

		data->SetLength(Size(algorithm));

		if(ComputeDigest(algorithm, input, input_length, data->GetWritableData(), data->GetLength()))
		{
			return data;
		}

		data->SetLength(0L);

		return data;
	}

	std::shared_ptr<ov::Data> MessageDigest::ComputeDigest(CryptoAlgorithm algorithm, const std::shared_ptr<const ov::Data> &input)
	{
		return ComputeDigest(algorithm, input->GetData(), input->GetLength());
	}
}
