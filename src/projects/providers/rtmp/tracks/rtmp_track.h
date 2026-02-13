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
#include <base/mediarouter/media_buffer.h>
#include <modules/containers/flv_v2/flv_parser.h>
#include <modules/rtmp_v2/chunk/rtmp_datastructure.h>

#include "../rtmp_definitions.h"

namespace pvd::rtmp
{
	class RtmpStreamV2;

	class RtmpTrack
	{
	public:
		RtmpTrack(
			std::shared_ptr<RtmpStreamV2> stream,
			uint32_t track_id,
			bool from_ex_header,
			cmn::MediaCodecId codec_id,
			cmn::BitstreamFormat bitstream_format,
			cmn::Timebase time_base)
			: _stream(std::move(stream)),
			  _track_id(track_id),
			  _from_ex_header(from_ex_header),
			  _codec_id(codec_id),
			  _bitstream_format(bitstream_format)
		{
			SetTimebase(time_base);
		}
		virtual ~RtmpTrack()						= default;

		virtual cmn::MediaType GetMediaType() const = 0;

		static std::shared_ptr<RtmpTrack> Create(std::shared_ptr<RtmpStreamV2> stream, uint32_t track_id, bool from_ex_header, cmn::MediaCodecId codec_id);

		info::NamePath GetNamePath() const;

		bool IsFromExHeader() const
		{
			return _from_ex_header;
		}

		const auto &GetStream() const
		{
			return _stream;
		}

		const auto &GetTrackId() const
		{
			return _track_id;
		}

		const auto &GetCodecId() const
		{
			return _codec_id;
		}

		const auto &GetBitstreamFormat() const
		{
			return _bitstream_format;
		}

		const auto &GetTimebase() const
		{
			return _time_base;
		}

		void SetTimebase(const cmn::Timebase &time_base)
		{
			_time_base = time_base;
		}

		const auto &GetMediaPacketList() const
		{
			return _media_packet_list;
		}

		void ClearMediaPacketList()
		{
			_media_packet_list.clear();
		}

		bool HasTooManyPackets() const
		{
			return (_media_packet_list.size() > MAX_PACKET_COUNT_TO_WAIT_OTHER_TRACKS);
		}

		bool HasSequenceHeader() const
		{
			return (_sequence_header != nullptr);
		}

		const auto &GetSequenceHeader() const
		{
			return _sequence_header;
		}

		bool IsIgnored() const
		{
			return _is_ignored;
		}

		void SetIgnored(bool is_ignored)
		{
			_is_ignored = is_ignored;
		}

		virtual bool Handle(
			const std::shared_ptr<const modules::rtmp::Message> &message,
			const modules::flv::ParserCommon &parser,
			const std::shared_ptr<const modules::flv::CommonData> &data) = 0;

		virtual void FillMediaTrackMetadata(const std::shared_ptr<MediaTrack> &media_track);

	protected:
		std::shared_ptr<MediaPacket> CreateMediaPacket(
			const std::shared_ptr<const ov::Data> &payload,
			int64_t pts, int64_t dts,
			cmn::PacketType packet_type,
			bool is_key_frame);

	protected:
		std::shared_ptr<RtmpStreamV2> _stream;
		uint32_t _track_id					   = 0;
		// Whether this track is using E-RTMP
		bool _from_ex_header				   = false;
		cmn::MediaCodecId _codec_id			   = cmn::MediaCodecId::None;
		cmn::BitstreamFormat _bitstream_format = cmn::BitstreamFormat::Unknown;
		cmn::Timebase _time_base;

		std::shared_ptr<DecoderConfigurationRecord> _sequence_header;

		bool _is_ignored = false;

		std::vector<std::shared_ptr<MediaPacket>> _media_packet_list;
	};
}  // namespace pvd::rtmp
