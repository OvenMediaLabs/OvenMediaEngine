//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/common_types.h>
#include <base/provider/push_provider/stream.h>
#include <modules/access_control/access_controller.h>

#include "./handlers/rtmp_chunk_handler.h"
#include "./handlers/rtmp_handshake_handler.h"

namespace modules::rtmp
{
	class AmfDocument;
	class AmfProperty;

	struct Message;

	struct ChunkHeader;
	struct ChunkWriteInfo;

	class ChunkParser;
	class ChunkWriter;
}  // namespace modules::rtmp

namespace pvd::rtmp
{
	class RtmpTrack;

	class RtmpStreamV2 final : public pvd::PushStream
	{
	public:
		friend class RtmpHandshakeHandler;
		friend class RtmpChunkHandler;
		friend class RtmpTrack;

	public:
		static std::shared_ptr<RtmpStreamV2> Create(StreamSourceType source_type, uint32_t channel_id, const std::shared_ptr<ov::Socket> &client_socket, const std::shared_ptr<PushProvider> &provider);

		explicit RtmpStreamV2(StreamSourceType source_type, uint32_t channel_id, std::shared_ptr<ov::Socket> client_socket, const std::shared_ptr<PushProvider> &provider);
		~RtmpStreamV2() final;

		//--------------------------------------------------------------------
		// Implementation of PushStream
		//--------------------------------------------------------------------
		bool Stop() override;

		PushStreamType GetPushStreamType() override
		{
			return PushStream::PushStreamType::INTERLEAVED;
		}

		bool OnDataReceived(const std::shared_ptr<const ov::Data> &data) override;

		bool UpdateConnectInfo(const ov::String &tc_url);

	protected:
		//--------------------------------------------------------------------
		// Implementation of PushStream
		//--------------------------------------------------------------------
		bool Start() override;

	private:
		bool IsReadyToPublish() const;

		void SetRequestedUrlWithPort(std::shared_ptr<ov::Url> requested_url);
		// Called when received AmfFCPublish & AmfPublish event
		bool PostPublish(const modules::rtmp::AmfDocument &document);

		bool SendData(const std::shared_ptr<const ov::Data> &data);
		bool SendMessage(const std::shared_ptr<const modules::rtmp::ChunkWriteInfo> &chunk_write_info);

		bool SetTrackInfo();

		bool PublishStream();
		// bool SetTrackInfo(const std::shared_ptr<MediaInfo> &media_info);

		bool CheckAccessControl();
		bool CheckStreamExpired() const;
		bool ValidatePublishUrl();

		void AdjustTimestamp(uint32_t track_id, int64_t &pts, int64_t &dts);

		// Called by `RtmpChunkHandler`
		std::shared_ptr<RtmpTrack> AddRtmpTrack(std::shared_ptr<RtmpTrack> rtmp_track);
		std::shared_ptr<RtmpTrack> GetRtmpTrack(MediaTrackId track_id) const;

	private:
		RtmpHandshakeHandler _handshake_handler;
		RtmpChunkHandler _chunk_handler;

		std::atomic<bool> _is_post_published = false;

		// To make first PTS 0
		bool _first_frame		  = true;
		int64_t _first_pts_offset = 0;
		int64_t _first_dts_offset = 0;

		uint32_t _rtmp_stream_id  = 1;

		ov::String _tc_url;
		info::VHostAppName _vhost_app_name		  = info::VHostAppName::InvalidVHostAppName();

		// To send data
		std::shared_ptr<ov::Socket> _remote		  = nullptr;

		// Received data buffer
		std::shared_ptr<ov::Data> _remaining_data = nullptr;

		// Signed Policy
		uint64_t _stream_expired_msec			  = 0;

		// RTMP tracks
		std::unordered_map<MediaTrackId, std::shared_ptr<RtmpTrack>> _rtmp_track_map;
	};
}  // namespace pvd::rtmp
