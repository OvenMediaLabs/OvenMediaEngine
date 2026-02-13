//==============================================================================
//
//  OvenMediaEngine
//
//  Created by getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#include <openssl/srtp.h>
#include <base/ovlibrary/byte_io.h>
#include "srtp_adapter.h"

#define OV_LOG_TAG "SRTP"

SrtpAdapter::SrtpAdapter()
{
	_session = nullptr;
	_rtp_auth_tag_len = 0;
    _rtcp_auth_tag_len = 0;
}

SrtpAdapter::~SrtpAdapter()
{
}

bool SrtpAdapter::Release()
{
	std::lock_guard<std::mutex> lock(_session_lock);

	if(_session != nullptr)
	{
		srtp_dealloc(_session);
	}
	return true;
}

bool SrtpAdapter::SetKey(srtp_ssrc_type_t type, uint64_t crypto_suite, std::shared_ptr<ov::Data> key)
{
	srtp_policy_t policy;
	memset(&policy, 0, sizeof(policy));

	switch(crypto_suite)
	{
		case SRTP_AES128_CM_SHA1_80:
			srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtp);
			srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtcp);
			break;
		case SRTP_AES128_CM_SHA1_32:
			srtp_crypto_policy_set_aes_cm_128_hmac_sha1_32(&policy.rtp);
			srtp_crypto_policy_set_aes_cm_128_hmac_sha1_32(&policy.rtcp);
			break;
		case SRTP_AEAD_AES_128_GCM:
			srtp_crypto_policy_set_aes_gcm_128_16_auth(&policy.rtp);
			srtp_crypto_policy_set_aes_gcm_128_16_auth(&policy.rtcp);
			break;
		default:
			logte("Failed to create srtp adapter. Unsupported crypto suite %d", crypto_suite);
			return false;
	}

	policy.ssrc.type = type;
	policy.ssrc.value = 0;
	policy.key = key->GetWritableDataAs<uint8_t>();
	policy.window_size = 1024;
	policy.allow_repeat_tx = 1;
	policy.next = nullptr;

	std::lock_guard<std::mutex> lock(_session_lock);
	int err = srtp_create(&_session, &policy);
	if(err != srtp_err_status_ok)
	{
		_session = nullptr;
		logte("srtp_create failed, err=%d", err);
		return false;
	}
	srtp_set_user_data(_session, this);

	_rtp_auth_tag_len = policy.rtp.auth_tag_len;
    _rtcp_auth_tag_len = policy.rtcp.auth_tag_len;

    logtt("srtp teg size rtp(%d) rtcp(%d)", _rtp_auth_tag_len, _rtcp_auth_tag_len);


	return true;
}

bool SrtpAdapter::ProtectRtp(std::shared_ptr<ov::Data> data)
{
	if(!_session)
	{
		return false;
	}

	uint32_t need_len = data->GetLength() + _rtp_auth_tag_len;

	if(need_len > data->GetCapacity())
	{
		logte("Buffer capacity(%d) less than the needed(%d)", data->GetCapacity(), need_len);
		return false;
	}

	auto buffer = data->GetWritableData();
	int out_len = static_cast<int>(data->GetLength());
	data->SetLength(need_len);

	// FOR DEBUG
	auto byte_buffer = data->GetDataAs<uint8_t>();
	uint8_t payload_type = byte_buffer[1] & 0x7F;
	uint8_t red_payload_type = byte_buffer[12];
	uint16_t seq = ByteReader<uint16_t>::ReadBigEndian(&byte_buffer[2]);

	std::lock_guard<std::mutex> lock(_session_lock);
	int err = srtp_protect(_session, buffer, &out_len);
	if(err != srtp_err_status_ok)
	{
		logte("Failed to protect SRTP packet, err=%d, len=%d, seq=%u, payload_type=%d, red_payload_type=%d", err, out_len, seq, payload_type, red_payload_type);
		return false;
	}

	return true;
}

bool SrtpAdapter::ProtectRtcp(std::shared_ptr<ov::Data> data)
{
    if(!_session)
    {
        return false;
    }

    // Protect size check( data + tag + (E, Encryption. 1 bit. + SRTCP index. 31 bits.)
    uint32_t need_len = data->GetLength() + _rtcp_auth_tag_len + 4;

    if(need_len > data->GetCapacity())
    {
        logte("Buffer capacity(%d) less than the needed(%d)", data->GetCapacity(), need_len);
        return false;
    }

    auto buffer = data->GetWritableData();
    int out_len = static_cast<int>(data->GetLength());
    data->SetLength(need_len);

	std::lock_guard<std::mutex> lock(_session_lock);
    int err = srtp_protect_rtcp(_session, buffer, &out_len);
    if(err != srtp_err_status_ok)
    {
        logte("Failed to protect SRTCP packet, err=%d, len=%d", err, out_len);
        return false;
    }

    return true;
}

bool SrtpAdapter::UnprotectRtp(const std::shared_ptr<ov::Data> &data)
{
	if (!_session)
    {
        return false;
    }

    auto buffer = data->GetWritableData();
    int out_len = static_cast<int>(data->GetLength());

	std::lock_guard<std::mutex> lock(_session_lock);
    int err = srtp_unprotect(_session, buffer, &out_len);
    if (err != srtp_err_status_ok)
    {
        logte("Failed to unprotect SRTP packet, err=%d", err);
        return false;
    }

    data->SetLength(out_len);

	return true;
}

bool SrtpAdapter::UnprotectRtcp(const std::shared_ptr<ov::Data> &data)
{
    if (!_session)
    {
        return false;
    }

    auto buffer = data->GetWritableData();
    int out_len = static_cast<int>(data->GetLength());

	std::lock_guard<std::mutex> lock(_session_lock);
    int err = srtp_unprotect_rtcp(_session, buffer, &out_len);
    if (err != srtp_err_status_ok)
    {
        logte("Failed to unprotect SRTCP packet, err=%d", err);
        return false;
    }

    data->SetLength(out_len);

    return true;
}
