//==============================================================================
//
//  Transcode
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../transcoder_decoder.h"

class DecoderAAC : public TranscodeDecoder
{
public:
	DecoderAAC(const info::Stream &stream_info)
		: TranscodeDecoder(stream_info)
	{
	}

	cmn::MediaCodecId GetCodecID() const noexcept override
	{
		return cmn::MediaCodecId::Aac;
	}

	cmn::MediaCodecModuleId GetModuleID() const noexcept
	{
		return cmn::MediaCodecModuleId::DEFAULT;
	}

	cmn::MediaType GetMediaType() const noexcept
	{
		return cmn::MediaType::Audio;
	}

	bool IsHWAccel() const noexcept
	{
		return false;
	}

	std::shared_ptr<const MediaPacket> _cur_pkt = nullptr;
	size_t _pkt_offset = 0;
	std::shared_ptr<const ov::Data> _cur_data = nullptr;

	int64_t _first_pkt_pts = INT64_MIN;
	int64_t _last_pkt_pts = INT64_MIN;
	int64_t _last_pkt_duration = 0;

	bool InitCodec();

	void CodecThread() override;

protected:
};
