//==============================================================================
//
//  RtmpProvider
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include "modules/rtmp/amf0/amf_document.h"
#include "base/common_types.h"
#include "base/provider/push_provider/stream.h"
#include "modules/rtmp/chunk/rtmp_chunk_parser.h"
#include "modules/rtmp/chunk/rtmp_export_chunk.h"
#include "modules/rtmp/chunk/rtmp_handshake.h"
#include "modules/access_control/access_controller.h"

#define MAX_STREAM_MESSAGE_COUNT (500)
#define BASELINE_PROFILE (66)
#define MAIN_PROFILE (77)

// Fix track id
#define RTMP_VIDEO_TRACK_ID 0
#define RTMP_AUDIO_TRACK_ID 1
#define RTMP_DATA_TRACK_ID 2

namespace pvd
{
	class RtmpStream final : public pvd::PushStream
	{
	public:
		static std::shared_ptr<RtmpStream> Create(StreamSourceType source_type, uint32_t channel_id, const std::shared_ptr<ov::Socket> &client_socket, const std::shared_ptr<PushProvider> &provider);

		explicit RtmpStream(StreamSourceType source_type, uint32_t channel_id, std::shared_ptr<ov::Socket> client_socket, const std::shared_ptr<PushProvider> &provider);
		~RtmpStream() final;

		bool Stop() override;

		// bool ConvertToVideoData(const std::shared_ptr<ov::Data> &data, int64_t &cts);
		// bool ConvertToAudioData(const std::shared_ptr<ov::Data> &data);

		// ------------------------------------------
		// Implementation of PushStream
		// ------------------------------------------
		PushStreamType GetPushStreamType() override
		{
			return PushStream::PushStreamType::INTERLEAVED;
		}
		bool OnDataReceived(const std::shared_ptr<const ov::Data> &data) override;

	protected:
		bool Start() override;

	private:
		// Called when received AmfFCPublish & AmfPublish event
		bool PostPublish(const AmfDocument &document);

		// AMF Event
		bool OnAmfConnect(const std::shared_ptr<const RtmpChunkHeader> &header, AmfDocument &document, double transaction_id);
		bool OnAmfCreateStream(const std::shared_ptr<const RtmpChunkHeader> &header, AmfDocument &document, double transaction_id);
		bool OnAmfFCPublish(const std::shared_ptr<const RtmpChunkHeader> &header, AmfDocument &document, double transaction_id);
		bool OnAmfPublish(const std::shared_ptr<const RtmpChunkHeader> &header, AmfDocument &document, double transaction_id);
		bool OnAmfDeleteStream(const std::shared_ptr<const RtmpChunkHeader> &header, AmfDocument &document, double transaction_id);
		bool OnAmfMetaData(const std::shared_ptr<const RtmpChunkHeader> &header, const AmfProperty *property);
		bool OnAmfCustomData(const std::shared_ptr<const RtmpChunkHeader> &header, const AmfDocument &document);


		// Send messages
		bool SendData(const std::shared_ptr<const ov::Data> &data);
		bool SendData(const void *data, size_t data_size);
		bool SendMessagePacket(std::shared_ptr<RtmpMuxMessageHeader> &message_header, const ov::Data *data);
		bool SendMessagePacket(std::shared_ptr<RtmpMuxMessageHeader> &message_header, std::shared_ptr<std::vector<uint8_t>> &data);
		bool SendAcknowledgementSize(uint32_t acknowledgement_traffic);

		bool SendUserControlMessage(UserControlMessageId message, std::shared_ptr<std::vector<uint8_t>> &data);
		bool SendWindowAcknowledgementSize(uint32_t size);
		bool SendSetPeerBandwidth(uint32_t bandwidth);
		bool SendStreamBegin(uint32_t stream_id);
		bool SendStreamEnd();
		bool SendSetChunkSize(uint32_t chunk_size);
		bool SendAmfCommand(std::shared_ptr<RtmpMuxMessageHeader> &message_header, AmfDocument &document);
		bool SendAmfConnectResult(double transaction_id, double object_encoding);
		bool SendAmfOnFCPublish(double client_id);
		bool SendAmfCreateStreamResult(uint32_t chunk_stream_id, double transaction_id);
		bool SendAmfOnStatus(uint32_t chunk_stream_id,
							 uint32_t stream_id,
							 const char *level,
							 const char *code,
							 const char *description,
							 double client_id);

		// Parsing handshake messages
		off_t ReceiveHandshakePacket(const std::shared_ptr<const ov::Data> &data);
		bool SendHandshake(const std::shared_ptr<const ov::Data> &data);

		// Parsing chunk messages
		int32_t ReceiveChunkPacket(const std::shared_ptr<const ov::Data> &data);
		bool ReceiveChunkMessage();

		bool ReceiveSetChunkSize(const std::shared_ptr<const RtmpMessage> &message);
		bool ReceiveUserControlMessage(const std::shared_ptr<const RtmpMessage> &message);
		bool ReceiveWindowAcknowledgementSize(const std::shared_ptr<const RtmpMessage> &message);
		bool ReceiveAmfCommandMessage(const std::shared_ptr<const RtmpMessage> &message);
		void ReceiveAmfDataMessage(const std::shared_ptr<const RtmpMessage> &message);

		bool CheckEventMessage(const std::shared_ptr<const RtmpChunkHeader> &header, AmfDocument &document);
		void GenerateEvent(const cfg::vhost::app::pvd::Event &event, const ov::String &value);

		bool ReceiveAudioMessage(const std::shared_ptr<const RtmpMessage> &message);
		bool ReceiveVideoMessage(const std::shared_ptr<const RtmpMessage> &message);

		ov::String GetCodecString(RtmpCodecType codec_type);
		ov::String GetEncoderTypeString(RtmpEncoderType encoder_type);

		bool CheckReadyToPublish();
		bool PublishStream();
		bool SetTrackInfo(const std::shared_ptr<RtmpMediaInfo> &media_info);

		bool CheckAccessControl();
		bool CheckStreamExpired();
		bool ValidatePublishUrl();

		void AdjustTimestamp(uint32_t track_id, int64_t &pts, int64_t &dts);

		// RTMP related
		RtmpHandshakeState _handshake_state = RtmpHandshakeState::Uninitialized;

		std::shared_ptr<RtmpChunkParser> _import_chunk;
		std::shared_ptr<RtmpExportChunk> _export_chunk;
		std::shared_ptr<RtmpMediaInfo> _media_info;

		std::vector<std::shared_ptr<const RtmpMessage>> _stream_message_cache;
		uint32_t _stream_message_cache_video_count = 0;
		uint32_t _stream_message_cache_audio_count = 0;

		uint32_t _acknowledgement_size = RTMP_DEFAULT_ACKNOWNLEDGEMENT_SIZE / 2;
		// Accumulated amount of traffic since the stream started
		uint32_t _acknowledgement_traffic = 0;
		// The accumulated amount of traffic since the last ACK was sent
		uint32_t _acknowledgement_traffic_after_last_acked = 0;
		uint32_t _rtmp_stream_id = 1;
		uint32_t _peer_bandwidth = RTMP_DEFAULT_PEER_BANDWIDTH;
		double _client_id = 12345.0;
		// Set from OnAmfPublish
		int32_t _chunk_stream_id = 0;

		// parsed from packet
		std::shared_ptr<ov::Url> _url = nullptr;
		std::shared_ptr<const SignedPolicy> _signed_policy = nullptr;
		std::shared_ptr<const AdmissionWebhooks> _admission_webhooks = nullptr;

		ov::String _full_url;  // with stream_name
		ov::String _tc_url;
		ov::String _app_name;
		ov::String _domain_name;
		info::VHostAppName _vhost_app_name = info::VHostAppName::InvalidVHostAppName();
		ov::String _stream_name;
		ov::String _device_string;

		std::shared_ptr<ov::Url> _publish_url = nullptr;  // AccessControl can redirect url set in RTMP to another url.

		// Cache (GetApplicationInfo()->GetId())
		info::application_id_t _app_id = 0;

		// For sending data
		std::shared_ptr<ov::Socket> _remote = nullptr;

		// Received data buffer
		std::shared_ptr<ov::Data> _remained_data = nullptr;

		// Singed Policy
		uint64_t _stream_expired_msec = 0;

		// Make first PTS 0
		bool _first_frame = true;
		int64_t _first_pts_offset = 0;
		int64_t _first_dts_offset = 0;

		// Data frame
		int64_t _last_video_pts_in_ms = 0;
		ov::StopWatch _last_video_pts_clock;
		int64_t _last_audio_pts_in_ms = 0;
		ov::StopWatch _last_audio_pts_clock;

		// For statistics
		time_t _stream_check_time = 0;

		uint32_t _key_frame_interval = 0;
		uint32_t _previous_key_frame_timestamp = 0;
		uint32_t _last_video_timestamp = 0;
		uint32_t _last_audio_timestamp = 0;
		uint32_t _previous_last_video_timestamp = 0;
		uint32_t _previous_last_audio_timestamp = 0;
		uint32_t _video_frame_count = 0;
		uint32_t _audio_frame_count = 0;

		bool _negative_cts_detected = false;

		cfg::vhost::app::pvd::EventGenerator _event_generator;
		ov::DelayQueue _event_test_timer{"RtmpEventTestTimer"};
	};
}  // namespace pvd
