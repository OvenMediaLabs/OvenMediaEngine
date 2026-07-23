//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <openssl/pem.h>
#include <stdint.h>

#include "../ovlibrary/ovlibrary.h"

namespace ov
{
	class RsaConnector
	{
	public:
		static std::shared_ptr<ov::Data> Encrypt(const ov::String &public_key, const std::shared_ptr<ov::Data> &plain);
		static std::shared_ptr<ov::Data> Decrypt(const ov::String &private_key, const std::shared_ptr<ov::Data> &cipher);
		
		// Encrypt the plain text using the private key and encode the result in base64 format.
		static ov::String SignMessage(const ov::String &private_key, const ov::String &message);

		// Decode the base64 encoded cipher text and decrypt the result using the public key.
		static bool VerifySignature(const ov::String &public_key, const ov::String &message, const ov::String &signature);
	};
}
