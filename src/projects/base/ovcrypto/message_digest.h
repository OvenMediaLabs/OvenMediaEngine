//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

namespace ov
{
	enum class CryptoAlgorithm
	{
		Unknown,

		Md5,
		Sha1,
		Sha224,
		Sha256,
		Sha384,
		Sha512,
		Sha512_224,
		Sha512_256,
	};

	constexpr const char *ToString(CryptoAlgorithm algorithm)
	{
		switch (algorithm)
		{
			OV_CASE_RETURN(CryptoAlgorithm::Unknown, "Unknown");
			OV_CASE_RETURN(CryptoAlgorithm::Md5, "MD5");
			OV_CASE_RETURN(CryptoAlgorithm::Sha1, "SHA-1");
			OV_CASE_RETURN(CryptoAlgorithm::Sha224, "SHA-224");
			OV_CASE_RETURN(CryptoAlgorithm::Sha256, "SHA-256");
			OV_CASE_RETURN(CryptoAlgorithm::Sha384, "SHA-384");
			OV_CASE_RETURN(CryptoAlgorithm::Sha512, "SHA-512");
			OV_CASE_RETURN(CryptoAlgorithm::Sha512_224, "SHA-512/224");
			OV_CASE_RETURN(CryptoAlgorithm::Sha512_256, "SHA-512/256");
		}

		return "Unknown";
	}

	class MessageDigest
	{
	public:
		MessageDigest();
		virtual ~MessageDigest();

		bool Create(CryptoAlgorithm algorithm);
		bool Destroy();
		bool Reset();

		static unsigned int Size(CryptoAlgorithm algorithm) noexcept;
		unsigned int Size() const noexcept;

		bool Update(const void *buffer, size_t length);
		bool Update(const std::shared_ptr<const ov::Data> &data);

		bool Finish(void *buffer, size_t length);
		std::shared_ptr<ov::Data> Finish();

		// RFC 2104 HMAC: H(K XOR opad, H(K XOR ipad, text))
		static bool ComputeHmac(CryptoAlgorithm algorithm, const void *key, size_t key_length, const void *input, size_t input_length, void *output, size_t output_length);
		static std::shared_ptr<ov::Data> ComputeHmac(CryptoAlgorithm algorithm, const std::shared_ptr<const ov::Data> &key, const std::shared_ptr<const ov::Data> &input);

		static bool ComputeDigest(CryptoAlgorithm algorithm, const void *input, size_t input_length, void *output, size_t output_length);
		static std::shared_ptr<ov::Data> ComputeDigest(CryptoAlgorithm algorithm, const void *input, size_t input_length);
		static std::shared_ptr<ov::Data> ComputeDigest(CryptoAlgorithm algorithm, const std::shared_ptr<const ov::Data> &input);

	protected:
		CryptoAlgorithm _algorithm;
		// 실제로는 EVP_MD_CTX * 타입. openssl을 외부로 부터 감추기 위해 void *로 선언함
		void *_context;
	};
}
