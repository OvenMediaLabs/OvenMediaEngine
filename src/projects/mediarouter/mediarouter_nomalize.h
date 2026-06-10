//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <stdint.h>

#include <memory>
#include <queue>
#include <vector>

#include "base/info/stream.h"
#include "base/mediarouter/media_buffer.h"
#include "base/mediarouter/media_type.h"
#include "base/mediarouter/mediarouter_application_connector.h"
#include "modules/bitstream/av1/av1_decoder_configuration_record.h"
#include "modules/bitstream/av1/av1_types.h"
#include "modules/managed_queue/managed_queue.h"

class MediaRouterNormalize
{
public:
	bool NormalizeMediaPacket(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet);

	/// Copy every cross-checked field from an AV1 in-band Sequence Header summary onto the
	/// `av1C` `AV1DecoderConfigurationRecord` synthesized by the enhanced-RTMP (FLV) ingest path
	/// (lenient `0x81 0x00 0x00 0x00` default) so the downstream
	/// `AV1DecoderConfigurationRecord::ValidateConfigObus()` cross-check stays consistent with
	/// the actual bitstream.
	///
	/// AV1 ISOBMFF binding v1.3.0 section 2.3.4 (Semantics): "When a Sequence Header OBU is
	/// contained within the configOBUs of the AV1CodecConfigurationRecord, the values present in
	/// the Sequence Header OBU contained within configOBUs SHALL match the values of the
	/// AV1CodecConfigurationRecord." (`initial_presentation_delay` is excluded - it is an av1C-only
	/// field with no Sequence Header match rule.)
	///
	/// Exposed publicly (and as `static`) so the regression test in `mediarouter_test.cpp`
	/// can exercise the field-mapping logic without standing up an Orchestrator fixture.
	static void ApplyInBandSequenceHeaderToAv1Config(
		const std::shared_ptr<AV1DecoderConfigurationRecord> &av1_config,
		const Av1SequenceHeaderSummary &summary);

	bool ProcessH264AVCCStream(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet);
	bool ProcessH264AnnexBStream(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet);
	bool InsertH264SPSPPSAnnexB(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet, bool need_aud = false);
	bool InsertH264AudAnnexB(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet);

	bool ProcessH265AnnexBStream(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet);
	bool ProcessH265HVCCStream(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet);

	bool ProcessAACRawStream(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet);
	bool ProcessAACAdtsStream(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet);

	bool ProcessVP8Stream(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet);

	bool ProcessAV1OBUStream(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet);

	bool ProcessOPUSStream(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet);

	bool ProcessMP3Stream(const std::shared_ptr<info::Stream> &stream_info, std::shared_ptr<MediaTrack> &media_track, std::shared_ptr<MediaPacket> &media_packet);
};
