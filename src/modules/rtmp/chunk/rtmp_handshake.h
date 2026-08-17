//==============================================================================
//
//  RtmpProvider
//
//  Created by Jaejong Bong
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include "rtmp_define.h"


//===============================================================================================
// RtmpHandshake
//===============================================================================================
class RtmpHandshake
{
public:
	RtmpHandshake() = default;
	~RtmpHandshake() = default;

public:
	static	bool	MakeS1(uint8_t *sig);
	static	bool	MakeC1(uint8_t *sig);
	
	static	bool	MakeS2(const uint8_t *client_sig, uint8_t *sig);
    static	bool	MakeC2(uint8_t *client_sig, uint8_t *sig);

private:
	static	int		GetDigestOffset1(const uint8_t *handshake);
	static	int		GetDigestOffset2(const uint8_t *handshake);
	static	void	HmacSha256(const uint8_t *message, int message_size, uint8_t *key, int key_size, uint8_t *digest);
	static	void	CalculateDigest(int digest_pos, const uint8_t *message, uint8_t *key, int key_size, uint8_t *digest);
	static	bool	VerifyDigest(int digest_pos, const uint8_t *message, uint8_t *key, int key_size);
};

