//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "rtmp_handshake_handler.h"

#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include "../rtmp_provider_private.h"
#include "../rtmp_stream_v2.h"

#undef RTMP_STREAM_FORMAT_PREFIX
#define RTMP_STREAM_FORMAT_PREFIX "[%u] "

#undef RTMP_STREAM_DESC
#define RTMP_STREAM_DESC _stream->GetId()

namespace pvd::rtmp
{
	constexpr static const uint8_t GENUINE_FMS_KEY[] = {
		// "Genuine Adobe Flash Media Server 001"
		0x47, 0x65, 0x6E, 0x75, 0x69, 0x6E, 0x65, 0x20, 0x41, 0x64, 0x6F, 0x62, 0x65, 0x20, 0x46, 0x6C,
		0x61, 0x73, 0x68, 0x20, 0x4D, 0x65, 0x64, 0x69, 0x61, 0x20, 0x53, 0x65, 0x72, 0x76, 0x65, 0x72,
		0x20, 0x30, 0x30, 0x31,

		// Signature
		0xF0, 0xEE, 0xC2, 0x4A, 0x80, 0x68, 0xBE, 0xE8, 0x2E, 0x00, 0xD0, 0xD1, 0x02, 0x9E, 0x7E, 0x57,
		0x6E, 0xEC, 0x5D, 0x2D, 0x29, 0x80, 0x6F, 0xAB, 0x93, 0xB8, 0xE6, 0x36, 0xCF, 0xEB, 0x31, 0xAE};

	constexpr static const uint8_t GENUINE_FP_KEY[] = {
		// "Genuine Adobe Flash Player 001"
		0x47, 0x65, 0x6E, 0x75, 0x69, 0x6E, 0x65, 0x20, 0x41, 0x64, 0x6F, 0x62, 0x65, 0x20, 0x46, 0x6C,
		0x61, 0x73, 0x68, 0x20, 0x50, 0x6C, 0x61, 0x79, 0x65, 0x72, 0x20, 0x30, 0x30, 0x31,

		// Signature
		0xF0, 0xEE, 0xC2, 0x4A, 0x80, 0x68, 0xBE, 0xE8, 0x2E, 0x00, 0xD0, 0xD1, 0x02, 0x9E, 0x7E, 0x57,
		0x6E, 0xEC, 0x5D, 0x2D, 0x29, 0x80, 0x6F, 0xAB, 0x93, 0xB8, 0xE6, 0x36, 0xCF, 0xEB, 0x31, 0xAE};

	constexpr size_t RtmpHandshakeHandler::CalcuateDigestPosition(const void *data, size_t offset, size_t modular_value, size_t add_value)
	{
		auto buffer			   = static_cast<const uint8_t *>(data) + offset;
		size_t digest_position = 0;

		digest_position += *(buffer++);
		digest_position += *(buffer++);
		digest_position += *(buffer++);
		digest_position += *(buffer++);

		return (digest_position % modular_value) + add_value;
	}

	void RtmpHandshakeHandler::CalculateHMAC(const void *data, size_t data_length, const void *key, size_t key_length, void *digest)
	{
		uint32_t digest_length = 0;

		::HMAC(
			::EVP_sha256(),
			key, static_cast<int>(key_length),
			static_cast<const uint8_t *>(data), data_length,
			static_cast<uint8_t *>(digest), &digest_length);

		OV_ASSERT2(digest_length == SHA256_DIGEST_LENGTH);
	}

	void RtmpHandshakeHandler::CalculateDigest(const void *data, size_t digest_position, const void *key, size_t key_length, uint8_t *digest)
	{
		constexpr const size_t buffer_length = modules::rtmp::HANDSHAKE_PACKET_LENGTH - SHA256_DIGEST_LENGTH;
		uint8_t buffer[buffer_length]{};

		OV_ASSERT2(digest_position <= buffer_length);

		// Structure of data:
		//
		// | <time> | <version> | <random #1> | <digest> | <random #2> |
		//                                    ^  ^
		//                                    |  +-- sizeof(<digest>) == SHA256_DIGEST_LENGTH
		//                                    +-- digest_position
		//
		// It will be copied into the buffer:
		// | <time> | <version> | <random #1> | <random #2> |
		::memcpy(buffer, data, static_cast<size_t>(digest_position));
		::memcpy(buffer + digest_position, static_cast<const uint8_t *>(data) + (digest_position + SHA256_DIGEST_LENGTH), buffer_length - digest_position);

		CalculateHMAC(buffer, buffer_length, key, key_length, digest);
	}

	bool RtmpHandshakeHandler::VerifyDigest(int digest_pos, const void *data, const void *key, int key_length)
	{
		uint8_t calc_digest[SHA256_DIGEST_LENGTH];

		CalculateDigest(data, digest_pos, key, key_length, calc_digest);

		return (::memcmp(static_cast<const uint8_t *>(data) + digest_pos, calc_digest, SHA256_DIGEST_LENGTH) == 0);
	}

	void RtmpHandshakeHandler::CreateS0(const std::shared_ptr<const ov::Data> &c1, ov::ByteStream &byte_stream)
	{
		// S0 - RTMP version
		byte_stream.Write8(modules::rtmp::HANDSHAKE_VERSION);
	}

	void RtmpHandshakeHandler::CreateS1(const std::shared_ptr<const ov::Data> &c1, ov::ByteStream &byte_stream)
	{
		// Time (4 bytes)
		byte_stream.WriteBE32(::time(nullptr));

		// Version - 0.1.0.1, (4 bytes)
		byte_stream.Write8(0x00);
		byte_stream.Write8(0x01);
		byte_stream.Write8(0x00);
		byte_stream.Write8(0x01);

		// Key - random values
		constexpr const auto random_offset = sizeof(uint32_t) + sizeof(uint32_t);
		static_assert(random_offset == 8, "Handshake packet length mismatch");

		uint8_t random_buffer[modules::rtmp::HANDSHAKE_PACKET_LENGTH - random_offset]{};
		ov::Random::Fill(random_buffer, OV_COUNTOF(random_buffer));

		const auto digest_position = CalcuateDigestPosition(random_buffer, random_offset, 728, 12);
		OV_ASSERT2(digest_position <= (modules::rtmp::HANDSHAKE_PACKET_LENGTH - SHA256_DIGEST_LENGTH));

		CalculateDigest(random_buffer, digest_position, GENUINE_FMS_KEY, 36, random_buffer + digest_position);

		byte_stream.Write(random_buffer, OV_COUNTOF(random_buffer));
	}

	void RtmpHandshakeHandler::CreateS2(const std::shared_ptr<const ov::Data> &c1, ov::ByteStream &byte_stream)
	{
		auto digest_position = CalcuateDigestPosition(c1->GetData(), 8, 728, 12);
		if (VerifyDigest(digest_position, c1->GetData(), GENUINE_FP_KEY, 30) == false)
		{
			digest_position = CalcuateDigestPosition(c1->GetData(), 772, 728, 776);
			if (VerifyDigest(digest_position, c1->GetData(), GENUINE_FP_KEY, 30) == false)
			{
				// Simple RTMP - Doesn't support digest
				byte_stream.Write(c1->GetData(), modules::rtmp::HANDSHAKE_PACKET_LENGTH);
				return;
			}
		}

		// Complex RTMP digest
		uint8_t c1_digest[SHA256_DIGEST_LENGTH];
		CalculateHMAC(c1->GetDataAs<uint8_t>() + digest_position, SHA256_DIGEST_LENGTH, GENUINE_FMS_KEY, sizeof(GENUINE_FMS_KEY), c1_digest);

		constexpr const auto s2_length = modules::rtmp::HANDSHAKE_PACKET_LENGTH - SHA256_DIGEST_LENGTH;
		static_assert(s2_length == 1504, "Handshake packet length mismatch");
		uint8_t random_buffer[modules::rtmp::HANDSHAKE_PACKET_LENGTH]{};
		ov::Random::Fill(random_buffer, s2_length);

		CalculateHMAC(random_buffer, s2_length, c1_digest, SHA256_DIGEST_LENGTH, random_buffer + s2_length);

		byte_stream.Write(random_buffer, sizeof(random_buffer));
	}

	std::shared_ptr<ov::Data> RtmpHandshakeHandler::CreateS0S1S2(const std::shared_ptr<const ov::Data> &c1) const
	{
		auto data = std::make_shared<ov::Data>(sizeof(uint8_t) + modules::rtmp::HANDSHAKE_PACKET_LENGTH + modules::rtmp::HANDSHAKE_PACKET_LENGTH);
		ov::ByteStream byte_stream(data);

		// S0
		CreateS0(c1, byte_stream);
		// S1
		CreateS1(c1, byte_stream);
		// S2
		CreateS2(c1, byte_stream);

		OV_ASSERT2(data->GetLength() == sizeof(uint8_t) + modules::rtmp::HANDSHAKE_PACKET_LENGTH + modules::rtmp::HANDSHAKE_PACKET_LENGTH);

		return data;
	}

	bool RtmpHandshakeHandler::VerifyC2(const std::shared_ptr<const ov::Data> &c2)
	{
		return true;
	}

	off_t RtmpHandshakeHandler::HandleC0(const std::shared_ptr<const ov::Data> &data)
	{
		if (data->GetLength() < sizeof(uint8_t))
		{
			// Need more data
			return 0LL;
		}

		logtt("Trying to check RTMP version (%d)...", modules::rtmp::HANDSHAKE_VERSION);

		auto version = data->At(0);

		if (version != modules::rtmp::HANDSHAKE_VERSION)
		{
			logte("Invalid RTMP version: %d, expected: %d", version, modules::rtmp::HANDSHAKE_VERSION);
			return -1LL;
		}

		_handshake_state = modules::rtmp::HandshakeState::WaitingForC1;
		return 1LL;
	}

	off_t RtmpHandshakeHandler::HandleC1(const std::shared_ptr<const ov::Data> &data)
	{
		if (data->GetLength() < modules::rtmp::HANDSHAKE_PACKET_LENGTH)
		{
			return 0LL;
		}

		// OM-1629 - Elemental Encoder
		auto s0s1s2 = CreateS0S1S2(data->Subdata(0, modules::rtmp::HANDSHAKE_PACKET_LENGTH));

		if (s0s1s2 == nullptr)
		{
			logte("Failed to create S0/S1/S2 packets - Invalid C1");
			return -1LL;
		}

		if (_stream->SendData(s0s1s2) == false)
		{
			logae("Could not send S0/S1/S2 packets");
			return -1LL;
		}

		_handshake_state = modules::rtmp::HandshakeState::WaitingForC2;
		return modules::rtmp::HANDSHAKE_PACKET_LENGTH;
	}

	off_t RtmpHandshakeHandler::HandleC2(const std::shared_ptr<const ov::Data> &data)
	{
		if (data->GetLength() < modules::rtmp::HANDSHAKE_PACKET_LENGTH)
		{
			return 0LL;
		}

		if (VerifyC2(data) == false)
		{
			return -1LL;
		}

		_handshake_state		= modules::rtmp::HandshakeState::Completed;
		_is_handshake_completed = true;

		logtt("Handshake is completed");
		return modules::rtmp::HANDSHAKE_PACKET_LENGTH;
	}

	// Handles RTMP handshake data
	//
	// ```
	// +-------------+                           +-------------+
	// |    Client   |       TCP/IP Network      |    Server   |
	// +-------------+            |              +-------------+
	//       |                    |                     |
	// Uninitialized              |               Uninitialized
	//       |          C0        |                     |
	//       |------------------->|         C0          |
	//       |                    |-------------------->|
	//       |          C1        |                     |
	//       |------------------->|         S0          |
	//       |                    |<--------------------|
	//       |                    |         S1          |
	//  Version sent              |<--------------------|
	//       |          S0        |                     |
	//       |<-------------------|                     |
	//       |          S1        |                     |
	//       |<-------------------|                Version sent
	//       |                    |         C1          |
	//       |                    |-------------------->|
	//       |          C2        |                     |
	//       |------------------->|         S2          |
	//       |                    |<--------------------|
	//    Ack sent                |                  Ack Sent
	//       |          S2        |                     |
	//       |<-------------------|                     |
	//       |                    |         C2          |
	//       |                    |-------------------->|
	//  Handshake Done            |               Handshake Done
	//       |                    |                     |
	//
	//           Pictorial Representation of Handshake
	// ```
	off_t RtmpHandshakeHandler::HandleData(const std::shared_ptr<const ov::Data> &data)
	{
		off_t result = -1LL;

		switch (_handshake_state)
		{
			case modules::rtmp::HandshakeState::WaitingForC0:
				result = HandleC0(data);
				break;

			case modules::rtmp::HandshakeState::WaitingForC1:
				result = HandleC1(data);
				break;

			case modules::rtmp::HandshakeState::WaitingForC2:
				result = HandleC2(data);
				break;

			case modules::rtmp::HandshakeState::Completed:
				logte("Could not handle data: handshake is already completed");
				OV_ASSERT(false, "Handshake is already completed");
				break;
		}

		return result;
	}

	info::NamePath RtmpHandshakeHandler::GetNamePath() const
	{
		return _stream->GetNamePath();
	}

}  // namespace pvd::rtmp
