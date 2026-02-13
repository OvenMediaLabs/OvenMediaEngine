//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/info.h>
#include <modules/rtmp_v2/chunk/rtmp_define.h>

namespace pvd::rtmp
{
	class RtmpStreamV2;

	class RtmpHandshakeHandler
	{
	public:
		RtmpHandshakeHandler(RtmpStreamV2 *stream)
			: _stream(stream)
		{
		}

		std::shared_ptr<ov::Data> CreateS0S1S2(const std::shared_ptr<const ov::Data> &c1) const;
		bool VerifyC2(const std::shared_ptr<const ov::Data> &c2);

		off_t HandleData(const std::shared_ptr<const ov::Data> &data);

		info::NamePath GetNamePath() const;

		bool IsHandshakeCompleted() const
		{
			return _is_handshake_completed;
		}

	private:
		constexpr static size_t CalcuateDigestPosition(const void *data, size_t offset, size_t modular_value, size_t add_value);
		static void CalculateHMAC(const void *data, size_t data_length, const void *key, size_t key_length, void *digest);
		static void CalculateDigest(const void *data, size_t digest_position, const void *key, size_t key_size, uint8_t *digest);
		static bool VerifyDigest(int digest_pos, const void *data, const void *key, int key_length);

		static void CreateS0(const std::shared_ptr<const ov::Data> &c1, ov::ByteStream &byte_stream);
		static void CreateS1(const std::shared_ptr<const ov::Data> &c1, ov::ByteStream &byte_stream);
		static void CreateS2(const std::shared_ptr<const ov::Data> &c1, ov::ByteStream &byte_stream);

		off_t HandleC0(const std::shared_ptr<const ov::Data> &data);
		off_t HandleC1(const std::shared_ptr<const ov::Data> &data);
		off_t HandleC2(const std::shared_ptr<const ov::Data> &data);

		modules::rtmp::HandshakeState _handshake_state = modules::rtmp::HandshakeState::WaitingForC0;
		bool _is_handshake_completed				   = false;

	private:
		RtmpStreamV2 *_stream;
	};
}  // namespace pvd::rtmp
