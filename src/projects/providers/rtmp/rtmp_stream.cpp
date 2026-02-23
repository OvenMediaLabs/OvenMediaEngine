//==============================================================================
//
//  RtmpProvider
//
//  Created by Getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#include "rtmp_stream.h"

#include <base/info/media_extradata.h>
#include <base/mediarouter/media_type.h>
#include <modules/bitstream/h264/h264_decoder_configuration_record.h>
#include <modules/containers/flv/flv_parser.h>
#include <base/modules/data_format/id3v2/frames/id3v2_frames.h>
#include <base/modules/data_format/id3v2/id3v2.h>
#include <orchestrator/orchestrator.h>

#include "base/info/application.h"
#include "base/provider/push_provider/application.h"
#include "base/provider/push_provider/provider.h"
#include "rtmp_provider_private.h"

/*
Process of publishing 

- Handshaking
 C->S : C0 + C1
 S->C : S0 + S1 + S2
 C->S : C2

- Connect
 C->S : Set Chunk Size
 C->S : Connect
 S->C : WindowAcknowledgement Size
 S->C : Set Peer Bandwidth
 C->S : Window Acknowledgement Size
 S->C : Stream Begin
 S->C : Set Chunk Size
 S->C : _result

- Publishing
 C->S : releaseStream
 C->S : FCPublish
 C->S : createStream
 S->C : _result
 C->S : publish

 S->C : Stream Begin
 S->C : onStatus(Start)
 C->S : @setDataFrame
 C->S : Video/Audio Data Stream
 - H.264 : SPS/PPS 
 - AAC : Control Byte 
*/

// When using b-frames in Wirecast, sometimes CTS is negative, so a PTS < DTS situation occurs.
// This is not a normal situation, so we need to adjust it.
//
// [OBS]
// DTS   CTS   PTS
//   0     0     0
//   0    66    66
//  33   166   199
//  66    66   132
//  99     0    99
// 132    33   165
//
// [Wirecast]
// DTS   CTS   PTS
//   0     0     0
//  33   100   133
//  66     0    66
// 100   -66    34 <== ERROR (PTS < DTS)
// 133   -33   100 <== ERROR (PTS < DTS)
// 166   100   266
// 200     0   200
#define ADJUST_PTS (150)

namespace pvd
{
	std::shared_ptr<RtmpStream> RtmpStream::Create(StreamSourceType source_type, uint32_t client_id, const std::shared_ptr<ov::Socket> &client_socket, const std::shared_ptr<PushProvider> &provider)
	{
		auto stream = std::make_shared<RtmpStream>(source_type, client_id, client_socket, provider);
		if (stream != nullptr)
		{
			stream->Start();
		}
		return stream;
	}

	RtmpStream::RtmpStream(StreamSourceType source_type, uint32_t client_id, std::shared_ptr<ov::Socket> client_socket, const std::shared_ptr<PushProvider> &provider)
		: PushStream(source_type, client_id, provider)
	{
		logat("Stream has been created");

		_remote = client_socket;
		SetMediaSource(_remote->GetRemoteAddressAsUrl());

		_import_chunk = std::make_shared<RtmpChunkParser>(RTMP_DEFAULT_CHUNK_SIZE);
		_export_chunk = std::make_shared<RtmpExportChunk>(false, RTMP_DEFAULT_CHUNK_SIZE);
		_media_info = std::make_shared<RtmpMediaInfo>();

		// For debug statistics
		_stream_check_time = time(nullptr);
	}

	RtmpStream::~RtmpStream()
	{
		logat("Stream has been terminated finally");
	}

	bool RtmpStream::Start()
	{
		SetState(Stream::State::PLAYING);
		_negative_cts_detected = false;

		return PushStream::Start();
	}

	bool RtmpStream::Stop()
	{
		if (GetState() == Stream::State::STOPPED)
		{
			return true;
		}

		// Send Close to Admission Webhooks
		auto requested_url = GetRequestedUrl();
		auto final_url = GetFinalUrl();
		if (_remote && requested_url && final_url)
		{
			auto request_info = std::make_shared<ac::RequestInfo>(requested_url, final_url, _remote, nullptr);
			GetProvider()->SendCloseAdmissionWebhooks(request_info);
		}
		// the return check is not necessary
		if (_remote->GetState() == ov::SocketState::Connected)
		{
			_remote->Close();
		}

		return PushStream::Stop();
	}

	bool RtmpStream::CheckStreamExpired()
	{
		if (_stream_expired_msec != 0 && _stream_expired_msec < ov::Clock::NowMSec())
		{
			return true;
		}

		return false;
	}

	bool RtmpStream::OnDataReceived(const std::shared_ptr<const ov::Data> &data)
	{
		if (GetState() == Stream::State::ERROR || GetState() == Stream::State::STOPPED)
		{
			return false;
		}

		// Check stream expired by signed policy
		if (CheckStreamExpired() == true)
		{
			logai("Stream has expired by signed policy");
			Stop();
			return false;
		}

		// Accumulate processed bytes for acknowledgement
		auto data_length = data->GetLength();
		_acknowledgement_traffic += data_length;
		if (_acknowledgement_traffic >= INT_MAX)
		{
			// Rolled
			_acknowledgement_traffic -= INT_MAX;
		}
		_acknowledgement_traffic_after_last_acked += data_length;

		if ((_remained_data != nullptr) && (_remained_data->GetLength() > RTMP_MAX_PACKET_SIZE))
		{
			logae("The packet is ignored because the size is too large: [%u]), packet size: %zu, threshold: %d",
				  GetChannelId(), _remained_data->GetLength(), RTMP_MAX_PACKET_SIZE);

			return false;
		}

		if ((_remained_data == nullptr) || _remained_data->IsEmpty())
		{
			_remained_data = data->Clone();
		}
		else
		{
			_remained_data->Append(data);
		}

		logap("Trying to parse data\n%s", _remained_data->Dump(_remained_data->GetLength()).CStr());

		while (true)
		{
			int32_t process_size = 0;

			if (_handshake_state == RtmpHandshakeState::Complete)
			{
				process_size = ReceiveChunkPacket(_remained_data);
			}
			else
			{
				process_size = ReceiveHandshakePacket(_remained_data);
			}

			if (process_size < 0)
			{
				logat("Could not process RTMP packet: size: %zu bytes, returns: %d",
					  _remained_data->GetLength(),
					  process_size);

				Stop();
				return false;
			}
			else if (process_size == 0)
			{
				// Need more data
				// logat("Not enough data");
				break;
			}

			_remained_data = _remained_data->Subdata(process_size);
		}

		if (_acknowledgement_traffic_after_last_acked > _acknowledgement_size)
		{
			SendAcknowledgementSize(_acknowledgement_traffic);
			_acknowledgement_traffic_after_last_acked -= _acknowledgement_size;
		}

		return true;
	}

	bool RtmpStream::PostPublish(const AmfDocument &document)
	{
		if (_tc_url.IsEmpty())
		{
			Stop();
			return false;
		}

		auto url = ov::Url::Parse(_tc_url);

		if (url == nullptr)
		{
			logaw("Could not parse the URL: %s", _tc_url.CStr());
			return false;
		}

		auto stream_name = document.GetProperty(3)->GetString();
		auto query_position = stream_name.IndexOf('?');
		if (query_position >= 0)
		{
			url->AppendQueryString(stream_name.Substring(query_position));
			stream_name = stream_name.Substring(0, query_position);
		}

		if (stream_name.IsEmpty() == false)
		{
			url->SetStream(stream_name);
		}

		// PORT can be omitted (1935), but SignedPolicy requires this information.
		if (url->Port() == 0)
		{
			url->SetPort(_remote->GetLocalAddress()->Port());
		}

		_url = url;
		_publish_url = _url;
		_stream_name = _url->Stream();
		_import_chunk->UpdateNamePath(GetNamePath());

		SetRequestedUrl(_url);
		SetFinalUrl(_url);

		return CheckAccessControl() && ValidatePublishUrl();
	}

	bool RtmpStream::OnAmfConnect(const std::shared_ptr<const RtmpChunkHeader> &header, AmfDocument &document, double transaction_id)
	{
		double object_encoding = 0.0;

		auto meta_property = document.GetProperty(2, AmfTypeMarker::Object);
		if (meta_property != nullptr)
		{
			auto object = meta_property->GetObject();

			// object encoding
			auto pair = object->GetPair("objectEncoding", AmfTypeMarker::Number);
			if (pair != nullptr)
			{
				object_encoding = pair->property.GetNumber();
			}

			// app name set
			pair = object->GetPair("app", AmfTypeMarker::String);
			if (pair != nullptr)
			{
				_app_name	  = pair->property.GetString();

				auto seprator = _app_name.IndexOf('/');
				if (seprator >= 0)
				{
					_app_name = _app_name.Substring(0, seprator);
				}
			}

			// app url set
			pair = object->GetPair("tcUrl", AmfTypeMarker::String);
			if (pair != nullptr)
			{
				_tc_url = pair->property.GetString();
			}
		}

		// Parse the URL to obtain the domain name
		_url = ov::Url::Parse(_tc_url);

		if (_url != nullptr)
		{
			_vhost_app_name = ocst::Orchestrator::GetInstance()->ResolveApplicationNameFromDomain(_url->Host(), _app_name);

			//Since vhost/app/stream can be changed in AdmissionWebhooks, it is not checked here.
			// auto app_info = ocst::Orchestrator::GetInstance()->GetApplicationInfo(_vhost_app_name);
			// if (app_info.IsValid())
			// {
			// 	_app_id = app_info.GetId();
			// }
			// else
			// {
			// 	logae("%s application does not exist", _vhost_app_name.CStr());
			// 	Stop();
			// 	return;
			// }
		}
		else
		{
			logaw("Could not obtain tcUrl from the RTMP stream");

			// TODO(dimiden): If tcUrl is not provided, it's not possible to determine which VHost the request was received,
			// so it does not work properly.
			// So, if there is currently one VHost associated with the RTMP Provider, we need to modify it to work without tcUrl.
			_vhost_app_name = info::VHostAppName::InvalidVHostAppName();
		}

		UpdateNamePath(_vhost_app_name);
		_import_chunk->UpdateNamePath(GetNamePath());

		if (SendWindowAcknowledgementSize(RTMP_DEFAULT_ACKNOWNLEDGEMENT_SIZE) == false)
		{
			logae("SendWindowAcknowledgementSize Fail");
			return false;
		}

		if (SendSetPeerBandwidth(_peer_bandwidth) == false)
		{
			logae("SendSetPeerBandwidth Fail");
			return false;
		}

		if (SendStreamBegin(0) == false)
		{
			logae("SendStreamBegin Fail");
			return false;
		}

		if (SendSetChunkSize(RTMP_DEFAULT_CHUNK_SIZE) == false)
		{
			logae("SendSetChunkSize Fail");
			return false;
		}

		if (SendAmfConnectResult(transaction_id, object_encoding) == false)
		{
			logae("SendAmfConnectResult Fail");
			return false;
		}

		return true;
	}

	bool RtmpStream::OnAmfCreateStream(const std::shared_ptr<const RtmpChunkHeader> &header, AmfDocument &document, double transaction_id)
	{
		if (SendAmfCreateStreamResult(header->basic_header.chunk_stream_id, transaction_id) == false)
		{
			logae("SendAmfCreateStreamResult Fail");
			return false;
		}

		return true;
	}

	bool RtmpStream::CheckAccessControl()
	{
		// Check SignedPolicy
		auto provider = GetProvider();

		auto request_info = std::make_shared<ac::RequestInfo>(_url, nullptr, _remote, nullptr);

		auto [signed_policy_result, _signed_policy] = provider->VerifyBySignedPolicy(request_info);
		switch (signed_policy_result)
		{
			case AccessController::VerificationResult::Error:
				logaw("SignedPolicy error : %s", _url->ToUrlString().CStr());
				Stop();
				return false;

			case AccessController::VerificationResult::Fail:
				logaw("%s", _signed_policy->GetErrMessage().CStr());
				Stop();
				return false;

			case AccessController::VerificationResult::Off:
				break;

			case AccessController::VerificationResult::Pass:
				_stream_expired_msec = _signed_policy->GetStreamExpireEpochMSec();
				break;
		}

		auto [webhooks_result, _admission_webhooks] = provider->VerifyByAdmissionWebhooks(request_info);
		switch (webhooks_result)
		{
			case AccessController::VerificationResult::Error:
				logaw("AdmissionWebhooks error : %s", _url->ToUrlString().CStr());
				Stop();
				break;

			case AccessController::VerificationResult::Fail:
				logaw("AdmissionWebhooks error : %s", _admission_webhooks->GetErrReason().CStr());
				Stop();
				break;

			case AccessController::VerificationResult::Off:
				return true;

			case AccessController::VerificationResult::Pass:
				// Lifetime
				if (_admission_webhooks->GetLifetime() != 0)
				{
					// Choice smaller value
					auto stream_expired_msec_from_webhooks = ov::Clock::NowMSec() + _admission_webhooks->GetLifetime();
					if (_stream_expired_msec == 0 || stream_expired_msec_from_webhooks < _stream_expired_msec)
					{
						_stream_expired_msec = stream_expired_msec_from_webhooks;
					}
				}

				// Redirect URL
				if (_admission_webhooks->GetNewURL() != nullptr)
				{
					_publish_url = _admission_webhooks->GetNewURL();
					SetFinalUrl(_publish_url);
				}

				return true;
		}

		return false;
	}

	bool RtmpStream::ValidatePublishUrl()
	{
		if (_publish_url == nullptr)
		{
			logae("Publish URL is not set");
			return false;
		}

		auto scheme = _publish_url->Scheme().UpperCaseString();
		if (((scheme != "RTMP") && (scheme != "RTMPS")) ||
			_publish_url->Host().IsEmpty() ||
			_publish_url->App().IsEmpty() ||
			_publish_url->Stream().IsEmpty())
		{
			logae("Invalid publish URL: %s", _publish_url->ToUrlString().CStr());
			return false;
		}

		auto orchestrator = ocst::Orchestrator::GetInstance();
		auto vhost_app_name = orchestrator->ResolveApplicationNameFromDomain(_publish_url->Host(), _publish_url->App());

		auto app_info = orchestrator->GetApplicationInfo(vhost_app_name);

		if (app_info.IsValid())
		{
			_app_id = app_info.GetId();
			return true;
		}

		logae("Could not find application: %s", vhost_app_name.CStr());
		return false;
	}

	bool RtmpStream::OnAmfFCPublish(const std::shared_ptr<const RtmpChunkHeader> &header, AmfDocument &document, double transaction_id)
	{
		if (_stream_name.IsEmpty())
		{
			auto property = document.GetProperty(3, AmfTypeMarker::String);

			if (property == nullptr)
			{
				logae("OnAmfFCPublish - No stream name provided");
				return false;
			}

			// TODO: check if the chunk stream id is already exist, and generates new rtmp_stream_id and client_id.
			if (SendAmfOnFCPublish(_client_id) == false)
			{
				logae("SendAmfOnFCPublish Fail");
				return false;
			}

			return PostPublish(document);
		}

		return true;
	}

	bool RtmpStream::OnAmfPublish(const std::shared_ptr<const RtmpChunkHeader> &header, AmfDocument &document, double transaction_id)
	{
		if (_stream_name.IsEmpty())
		{
			auto property = document.GetProperty(3, AmfTypeMarker::String);
			if (property != nullptr)
			{
				if (PostPublish(document) == false)
				{
					return false;
				}
			}
			else
			{
				logae("OnAmfPublish - No stream name provided");

				// Reject
				SendAmfOnStatus(header->basic_header.chunk_stream_id,
								_rtmp_stream_id,
								"error",
								"NetStream.Publish.Rejected",
								"Authentication Failed.",
								_client_id);

				return false;
			}
		}

		_chunk_stream_id = header->basic_header.chunk_stream_id;

		if (SendStreamBegin(_rtmp_stream_id) == false)
		{
			logae("SendStreamBegin Fail");
			return false;
		}

		if (SendAmfOnStatus(static_cast<uint32_t>(_chunk_stream_id),
							_rtmp_stream_id,
							"status",
							"NetStream.Publish.Start",
							"Publishing",
							_client_id) == false)
		{
			logae("SendAmfOnStatus Fail");
			return false;
		}

		return true;
	}

	bool RtmpStream::OnAmfMetaData(const std::shared_ptr<const RtmpChunkHeader> &header, const AmfProperty *property)
	{
		auto object = (property->GetType() == AmfTypeMarker::Object)
						  ? static_cast<const AmfObjectArray *>(property->GetObject())
						  : static_cast<const AmfObjectArray *>(property->GetEcmaArray());

		// DeviceType
		{
			auto property_pair = object->GetPair("videodevice", AmfTypeMarker::String);

			if (property_pair != nullptr)
			{
				_device_string = property_pair->property.GetString();
			}
			else
			{
				property_pair = object->GetPair("encoder", AmfTypeMarker::String);

				if (property_pair != nullptr)
				{
					_device_string = property_pair->property.GetString();
				}
			}
		}

		// Encoder
		RtmpEncoderType encoder_type = RtmpEncoderType::Custom;
		{
			if (_device_string.IndexOf("Open Broadcaster") >= 0)
			{
				encoder_type = RtmpEncoderType::OBS;
			}
			else if (_device_string.IndexOf("obs-output") >= 0)
			{
				encoder_type = RtmpEncoderType::OBS;
			}
			else if (_device_string.IndexOf("XSplitBroadcaster") >= 0)
			{
				encoder_type = RtmpEncoderType::Xsplit;
			}
			else if (_device_string.IndexOf("Lavf") >= 0)
			{
				encoder_type = RtmpEncoderType::Lavf;
			}
			else
			{
				encoder_type = RtmpEncoderType::Custom;
			}
		}

		// Video Codec
		bool video_available = false;
		RtmpCodecType video_codec_type = RtmpCodecType::Unknown;
		{
			auto property_pair = object->GetPair("videocodecid");

			if (property_pair != nullptr)
			{
				auto &property = property_pair->property;
				video_available = true;

				switch (property.GetType())
				{
					case AmfTypeMarker::String: {
						auto value = property.GetString();
						if ((value == "avc1") || (value == "H264Avc"))
						{
							video_codec_type = RtmpCodecType::H264;
						}
						break;
					}

					case AmfTypeMarker::Number: {
						auto value = property.GetNumber();
						if (value == 7.0)
						{
							video_codec_type = RtmpCodecType::H264;
						}
						break;
					}

					default:
						break;
				}
			}
		}

		// Video Framerate
		double frame_rate;
		{
			auto value = object->GetDoubleValue("framerate");
			value = value.has_value() ? value : object->GetDoubleValue("videoframerate");
			frame_rate = value.value_or(30.0);
		}

		double video_width = object->GetDoubleValue("width").value_or(0.0);
		double video_height = object->GetDoubleValue("height").value_or(0.0);

		double video_bitrate;
		{
			auto value = object->GetDoubleValue("videodatarate");
			value = value.has_value() ? value : object->GetDoubleValue("bitrate");
		}

		{
			auto property_pair = object->GetPair("videodatarate", AmfTypeMarker::Number);

			if (property_pair != nullptr)
			{
				video_bitrate = property_pair->property.GetNumber();
			}
			else
			{
				property_pair = object->GetPair("bitrate", AmfTypeMarker::Number);

				if (property_pair != nullptr)
				{
					video_bitrate = property_pair->property.GetNumber();
				}
			}

			property_pair = object->GetPair("maxBitrate", AmfTypeMarker::String);

			if (property_pair != nullptr)
			{
				video_bitrate = ::strtol(property_pair->property.GetString(), nullptr, 0);
			}
		}

		_media_info->video_codec_type = video_codec_type;
		_media_info->video_width = static_cast<int32_t>(video_width);
		_media_info->video_height = static_cast<int32_t>(video_height);
		_media_info->video_framerate = static_cast<float>(frame_rate);
		_media_info->video_bitrate = static_cast<int32_t>(video_bitrate);

		// Audio Codec
		RtmpCodecType audio_codec_type = RtmpCodecType::Unknown;
		bool audio_available = false;
		{
			auto property_pair = object->GetPair("audiocodecid");

			if (property_pair != nullptr)
			{
				audio_available = true;

				auto &property = property_pair->property;
				auto type = property.GetType();

				switch (type)
				{
					case AmfTypeMarker::String: {
						auto value = property.GetString();
						if (value == "mp4a")
						{
							audio_codec_type = RtmpCodecType::AAC;
						}
						else if (value == "mp3" || value == ".mp3")
						{
							audio_codec_type = RtmpCodecType::MP3;
						}
						else if (value == "speex")
						{
							audio_codec_type = RtmpCodecType::Speex;
						}
						break;
					}

					case AmfTypeMarker::Number: {
						auto value = property.GetNumber();
						if (value == 10.0)
						{
							audio_codec_type = RtmpCodecType::AAC;
						}
						else if (value == 11.0)
						{
							audio_codec_type = RtmpCodecType::Speex;
						}
						else if (value == 2.0)
						{
							audio_codec_type = RtmpCodecType::MP3;
						}
						break;
					}

					default:
						break;
				}
			}
		}

		// Audio bitrate
		double audio_bitrate = 0.0;
		{
			auto property_pair = object->GetPair("audiodatarate", AmfTypeMarker::Number);

			if (property_pair != nullptr)
			{
				audio_bitrate = property_pair->property.GetNumber();
			}
			else
			{
				property_pair = object->GetPair("audiobitrate", AmfTypeMarker::Number);

				if (property_pair != nullptr)
				{
					audio_bitrate = property_pair->property.GetNumber();
				}
			}
		}

		// Audio Channels
		double audio_channels = 1.0;
		{
			auto property_pair = object->GetPair("audiochannels");

			if (property_pair != nullptr)
			{
				switch (property_pair->property.GetType())
				{
					case AmfTypeMarker::Number:
						audio_channels = property_pair->property.GetNumber();
						break;

					case AmfTypeMarker::String: {
						auto value = property_pair->property.GetString();
						if (value == "stereo")
						{
							audio_channels = 2;
						}
						else if (value == "mono")
						{
							audio_channels = 1;
						}
					}

					default:
						break;
				}
			}
		}

		// Audio samplerate
		double audio_samplerate = 0.0;
		{
			auto property_pair = object->GetPair("audiosamplerate");

			if (property_pair != nullptr)
			{
				audio_samplerate = property_pair->property.GetNumber();
			}
		}

		// Audio samplesize
		double audio_samplesize = 0.0;
		{
			auto property_pair = object->GetPair("audiosamplesize");

			if (property_pair != nullptr)
			{
				audio_samplesize = property_pair->property.GetNumber();
			}
		}

		_media_info->audio_codec_type = audio_codec_type;
		_media_info->audio_bitrate = static_cast<int32_t>(audio_bitrate);
		_media_info->audio_channels = static_cast<int32_t>(audio_channels);
		_media_info->audio_bits = static_cast<int32_t>(audio_samplesize);
		_media_info->audio_samplerate = static_cast<int32_t>(audio_samplerate);
		_media_info->encoder_type = encoder_type;

		if ((video_available && (video_codec_type != RtmpCodecType::H264)) ||
			(audio_available && (audio_codec_type != RtmpCodecType::AAC)))
		{
			logaw("AmfMeta has incompatible codec information. - video(%s) audio(%s)",
				  GetCodecString(video_codec_type).CStr(),
				  GetCodecString(audio_codec_type).CStr());
		}

		return true;
	}

	bool RtmpStream::OnAmfDeleteStream(const std::shared_ptr<const RtmpChunkHeader> &header, AmfDocument &document, double transaction_id)
	{
		logat("Delete Stream");

		_media_info->video_stream_coming = false;
		_media_info->audio_stream_coming = false;

		// it will call PhysicalPort::OnDisconnected
		_remote->Close();

		return true;
	}

	bool RtmpStream::OnAmfCustomData(const std::shared_ptr<const RtmpChunkHeader> &header, const AmfDocument &document)
	{
		int64_t pts_in_ms = 0;
		if (_last_video_pts_in_ms > _last_audio_pts_in_ms)
		{
			pts_in_ms = _last_video_pts_in_ms + _last_video_pts_clock.Elapsed();
		}
		else
		{
			pts_in_ms = _last_audio_pts_in_ms + _last_audio_pts_clock.Elapsed();
		}

		ov::ByteStream byte_stream;
		if (document.Encode(byte_stream) == false)
		{
			return false;
		}

		return SendDataFrame(pts_in_ms, cmn::BitstreamFormat::AMF, cmn::PacketType::EVENT, byte_stream.GetDataPointer(), false);
	}

	off_t RtmpStream::ReceiveHandshakePacket(const std::shared_ptr<const ov::Data> &data)
	{
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

		int32_t process_size = 0;
		switch (_handshake_state)
		{
			case RtmpHandshakeState::Uninitialized:
				logat("Handshaking is started. Trying to parse for C0/C1 packets...");
				process_size = (sizeof(uint8_t) + RTMP_HANDSHAKE_PACKET_SIZE);
				break;

			case RtmpHandshakeState::S2:
				logat("Trying to parse for C2 packet...");
				process_size = (RTMP_HANDSHAKE_PACKET_SIZE);
				break;

			default:
				logae("Failed to handshake: state: %d", static_cast<int32_t>(_handshake_state));
				return -1LL;
		}

		if (static_cast<int32_t>(data->GetLength()) < process_size)
		{
			// Need more data
			logat("Need more data: data: %zu bytes, expected: %d bytes", data->GetLength(), process_size);
			return 0LL;
		}

		if (_handshake_state == RtmpHandshakeState::Uninitialized)
		{
			logat("C0/C1 packets are arrived");

			char version = data->At(0);

			logat("Trying to check RTMP version (%d)...", RTMP_HANDSHAKE_VERSION);

			if (version != RTMP_HANDSHAKE_VERSION)
			{
				logae("Invalid RTMP version: %d, expected: %d", version, RTMP_HANDSHAKE_VERSION);
				return -1LL;
			}

			_handshake_state = RtmpHandshakeState::C0;

			auto send_packet = data->Clone();

			// S0,S1,S2 전송

			logat("Trying to send S0/S1/S2 packets...");

			if (SendHandshake(send_packet) == false)
			{
				logae("Could not send S0/S1/S2 packets");
				return -1LL;
			}

			return process_size;
		}

		logat("C2 packet is arrived");

		_handshake_state = RtmpHandshakeState::C2;
		_handshake_state = RtmpHandshakeState::Complete;

		logat("Handshake is completed");

		return process_size;
	}

	int32_t RtmpStream::ReceiveChunkPacket(const std::shared_ptr<const ov::Data> &data)
	{
		int32_t process_size = 0;
		size_t import_size = 0;
		std::shared_ptr<const ov::Data> current_data = data;

		while (current_data->IsEmpty() == false)
		{
			auto status = _import_chunk->Parse(current_data, &import_size);

			process_size += import_size;

			switch (status)
			{
				case RtmpChunkParser::ParseResult::Error:
					logae("An error occurred while parse RTMP data");
					return -1;

				case RtmpChunkParser::ParseResult::NeedMoreData:
					break;

				case RtmpChunkParser::ParseResult::Parsed:
					if (ReceiveChunkMessage() == false)
					{
						logat("ReceiveChunkMessage Fail");
						logap("Failed to import packet\n%s", current_data->Dump(current_data->GetLength()).CStr());

						return -1LL;
					}
					break;
			}

			current_data = current_data->Subdata(import_size);

			if (status == RtmpChunkParser::ParseResult::NeedMoreData)
			{
				break;
			}
		}

		return process_size;
	}

	bool RtmpStream::ReceiveChunkMessage()
	{
		// Transact chunk message
		while (true)
		{
			auto message = _import_chunk->GetMessage();

			if ((message == nullptr) || (message->payload == nullptr))
			{
				break;
			}

			bool result = true;
			switch (message->header->completed.type_id)
			{
				case RtmpMessageTypeID::Audio:
					result = ReceiveAudioMessage(message);
					break;
				case RtmpMessageTypeID::Video:
					result = ReceiveVideoMessage(message);
					break;
				case RtmpMessageTypeID::SetChunkSize:
					result = ReceiveSetChunkSize(message);
					break;
				case RtmpMessageTypeID::Acknowledgement:
					// OME doesn't use this message
					break;
				case RtmpMessageTypeID::Amf0Data:
					ReceiveAmfDataMessage(message);
					break;
				case RtmpMessageTypeID::Amf0Command:
					result = ReceiveAmfCommandMessage(message);
					break;
				case RtmpMessageTypeID::UserControl:
					result = ReceiveUserControlMessage(message);
					break;
				case RtmpMessageTypeID::WindowAcknowledgementSize:
					result = ReceiveWindowAcknowledgementSize(message);
					break;
				default:
					logaw("Unknown Type - Type(%d)", ov::ToUnderlyingType(message->header->completed.type_id));
					break;
			}

			if (!result)
			{
				return false;
			}
		}

		return true;
	}

	bool RtmpStream::ReceiveSetChunkSize(const std::shared_ptr<const RtmpMessage> &message)
	{
		if (message->payload->GetLength() < 4)
		{
			logae("Invalid message size (data length must be at least 4 bytes, but %zu)", message->payload->GetLength());
			return false;
		}

		auto chunk_size = RtmpMuxUtil::ReadInt32(message->payload->GetData());

		logai("ChunkSize is changed to %u (stream id: %u)", chunk_size, message->header->completed.stream_id);

		_import_chunk->SetChunkSize(chunk_size);

		return true;
	}

	bool RtmpStream::ReceiveUserControlMessage(const std::shared_ptr<const RtmpMessage> &message)
	{
		auto data = message->payload;

		// RTMP Spec. v1.0 - 6.2. User Control Messages
		//
		// User Control messages SHOULD use message stream ID 0 (known as the
		// control stream) and, when sent over RTMP Chunk Stream, be sent on
		// chunk stream ID 2.
		auto message_stream_id = message->header->completed.stream_id;
		auto chunk_stream_id = message->header->basic_header.chunk_stream_id;
		if (
			(message_stream_id != 0) ||
			(chunk_stream_id != 2))
		{
			logae("Invalid id (message stream id: %u, chunk stream id: %u)", message_stream_id, chunk_stream_id);
			return false;
		}

		if (data->GetLength() < 2)
		{
			logae("Invalid user control message size (data length must greater than 2 bytes, but %zu)", data->GetLength());
			return false;
		}

		ov::ByteStream byte_stream(data);

		auto type = static_cast<UserControlMessageId>(byte_stream.ReadBE16());

		switch (type)
		{
			case UserControlMessageId::PingRequest: {
				if (data->GetLength() != 6)
				{
					logae("Invalid ping message size: %zu", data->GetLength());
					return false;
				}

				// +---------------+--------------------------------------------------+
				// | PingResponse  | The client sends this event to the server in     |
				// | (=7)          | response to the ping request. The event data is  |
				// |               | a 4-byte timestamp, which was received with the  |
				// |               | PingRequest request.                             |
				// +---------------+--------------------------------------------------+
				// ping response == event type (16 bits) + timestamp (32 bits)
				auto body = std::make_shared<std::vector<uint8_t>>(2 + 4);
				auto write_buffer = body->data();
				auto message_header = RtmpMuxMessageHeader::Create(chunk_stream_id, RtmpMessageTypeID::UserControl, message_stream_id, 6);

				*(reinterpret_cast<uint16_t *>(write_buffer)) = ov::HostToBE16(ov::ToUnderlyingType(UserControlMessageId::PingResponse));
				write_buffer += sizeof(uint16_t);
				*(reinterpret_cast<uint32_t *>(write_buffer)) = ov::HostToBE32(byte_stream.ReadBE32());

				return SendMessagePacket(message_header, body);
			}

			default:
				break;
		}

		return true;
	}

	bool RtmpStream::ReceiveWindowAcknowledgementSize(const std::shared_ptr<const RtmpMessage> &message)
	{
		if (message->payload->GetLength() < 4)
		{
			logae("Invalid message size (data length must be at least 4 bytes, but %zu)", message->payload->GetLength());
			return false;
		}

		auto acknowledgement_size = RtmpMuxUtil::ReadInt32(message->payload->GetData());

		if (acknowledgement_size != 0)
		{
			_acknowledgement_size = acknowledgement_size / 2;
		}

		return true;
	}

	bool RtmpStream::ReceiveAmfCommandMessage(const std::shared_ptr<const RtmpMessage> &message)
	{
		OV_ASSERT2(message->header != nullptr);
		OV_ASSERT2(message->payload != nullptr);

		if (message->payload->GetLength() != message->header->message_length)
		{
			OV_ASSERT2(message->payload->GetLength() == message->header->message_length);

			logae("Invalid AMF0CommandMessage size (data length must be equal to message length, but %zu != %u)",
				  message->payload->GetLength(),
				  message->header->message_length);
			return false;
		}

		ov::ByteStream byte_stream(message->payload);
		AmfDocument document;

		if (document.Decode(byte_stream) == false)
		{
			logae("Could not decode AMFDocument");
			return false;
		}

		// Message Name
		ov::String message_name;
		{
			auto property = document.GetProperty(0, AmfTypeMarker::String);

			if (property == nullptr)
			{
				logaw("The message was ignored - the name does not exist");
				return true;
			}

			message_name = property->GetString();
		}

		// Obtain the Message Transaction ID
		double transaction_id = 0.0;
		{
			auto property = document.GetProperty(1, AmfTypeMarker::Number);

			if (property != nullptr)
			{
				transaction_id = property->GetNumber();
			}
		}

		switch (RtmpCommandFromString(message_name))
		{
			case RtmpCommand::Connect:
				return OnAmfConnect(message->header, document, transaction_id);

			case RtmpCommand::CreateStream:
				return OnAmfCreateStream(message->header, document, transaction_id);

			case RtmpCommand::Publish:
				return OnAmfPublish(message->header, document, transaction_id);

			case RtmpCommand::FCPublish:
				return OnAmfFCPublish(message->header, document, transaction_id);

			case RtmpCommand::DeleteStream:
				//TODO(Dimiden): Check this message, This causes the stream to be deleted twice.
				//OnAmfDeleteStream(message->header, document, transaction_id);
				return true;

			case RtmpCommand::FCUnpublish:
				[[fallthrough]];
			case RtmpCommand::ReleaseStream:
				[[fallthrough]];
			case RtmpCommand::Ping:
				return true;

			default:
				break;
		}

		// Commands not handled by OME are not treated as errors but simply ignored
		logaw("Unknown Amf0CommandMessage - Message(%s:%.1f)", message_name.CStr(), transaction_id);

		return true;
	}

	void RtmpStream::ReceiveAmfDataMessage(const std::shared_ptr<const RtmpMessage> &message)
	{
		if (message->payload->GetLength() != message->header->message_length)
		{
			OV_ASSERT2(message->payload->GetLength() == message->header->message_length);

			logae("Invalid AMF0DataMessage size (data length must be equal to message length, but %zu != %u)",
				  message->payload->GetLength(),
				  message->header->message_length);
			return;
		}

		ov::ByteStream byte_stream(message->payload);
		AmfDocument document;
		auto decode_length = document.Decode(byte_stream);

		if (decode_length == 0)
		{
			logae("Amf0DataMessage Document Length 0");
			return;
		}

		// Obtain the message name
		ov::String message_name;
		auto message_name_property = document.GetProperty(0);
		auto message_name_type = (message_name_property != nullptr) ? message_name_property->GetType() : AmfTypeMarker::Undefined;
		if (message_name_type == AmfTypeMarker::String)
		{
			message_name = message_name_property->GetString();
		}

		// Obtain the data name
		ov::String data_name;
		auto data_name_property = document.GetProperty(1);
		auto data_name_type = (data_name_property != nullptr) ? data_name_property->GetType() : AmfTypeMarker::Undefined;
		if (data_name_type == AmfTypeMarker::String)
		{
			data_name = data_name_property->GetString();
		}

		auto third_property = document.GetProperty(2);
		auto third_type = (third_property != nullptr) ? third_property->GetType() : AmfTypeMarker::Undefined;

		switch (RtmpCommandFromString(message_name))
		{
			case RtmpCommand::SetDataFrame:
				if ((::strcmp(data_name, StringFromRtmpCommand(RtmpCommand::OnMetaData)) == 0) &&
					((third_type == AmfTypeMarker::Object) || (third_type == AmfTypeMarker::EcmaArray)))
				{
					OnAmfMetaData(message->header, third_property);
				}
				else
				{
					logaw("SetDataFrame - Data type is not object or ecma array");
				}
				break;

			case RtmpCommand::OnMetaData:
				if ((data_name_type == AmfTypeMarker::Object) || (data_name_type == AmfTypeMarker::EcmaArray))
				{
					OnAmfMetaData(message->header, data_name_property);
				}
				else
				{
					logaw("OnMetaData - Data type is not object or ecma array");
				}
				break;

			case RtmpCommand::OnFI:
				// Not support yet
				break;

			default:
				break;
		}

		// Find it in Events
		if (CheckEventMessage(message->header, document) == false)
		{
			logat("There were no triggered events - Message(%s / %s)", message_name.CStr(), data_name.CStr());
		}
	}

	bool RtmpStream::CheckEventMessage(const std::shared_ptr<const RtmpChunkHeader> &header, AmfDocument &document)
	{
		bool found = false;

		for (const auto &event : _event_generator.GetEvents())
		{
			auto trigger = event.GetTrigger();
			auto trigger_list = trigger.Split(".");

			// Trigger length must be 3 or more
			// AMFDataMessage.[<Property>.<Property>...<Property>.]<Object Name>.<Key Name>
			if (trigger_list.size() < 3)
			{
				logat("Invalid trigger: %s", trigger.CStr());
				continue;
			}

			if (
				(header->completed.type_id == RtmpMessageTypeID::Amf0Data) &&
				(trigger_list.at(0) == "AMFDataMessage"))
			{
				auto count = trigger_list.size();

				for (std::size_t size = 1; size < count; size++)
				{
					auto property = document.GetProperty(size - 1);

					if (property == nullptr)
					{
						logat("Document has no property at %zu: %s", size - 1, trigger.CStr());
						break;
					}

					auto type = property->GetType();

					// if last item is must be object or array
					if (size == (count - 1))
					{
						if ((type == AmfTypeMarker::Object) || (type == AmfTypeMarker::EcmaArray))
						{
							auto object = property->GetObject();

							if (object == nullptr)
							{
								logat("Property is not object: %s", property->GetString().CStr());
								break;
							}

							auto key = trigger_list.at(size);

							{
								auto property_pair = object->GetPair(key, AmfTypeMarker::String);

								if (property_pair != nullptr)
								{
									found = true;
									auto value = property_pair->property.GetString();
									GenerateEvent(event, value);
								}
							}
						}
						else if (property->GetType() == AmfTypeMarker::String)
						{
							found = true;
							auto value = property->GetString();
							GenerateEvent(event, value);
						}
						else
						{
							logat("Document property type mismatch at %zu: %s", size - 1, property->GetString().CStr());
							break;
						}
					}
					else
					{
						if (trigger_list.at(size) != property->GetString())
						{
							logat("Document property mismatch at %zu: %s != %s", size - 1, trigger_list.at(size).CStr(), property->GetString().CStr());

							break;
						}
					}
				}
			}
		}

		return found;
	}

	void RtmpStream::GenerateEvent(const cfg::vhost::app::pvd::Event &event, const ov::String &value)
	{
		logat("Event generated: %s / %s", event.GetTrigger().CStr(), value.CStr());

		bool id3_enabled = false;
		auto id3v2_event = event.GetHLSID3v2(&id3_enabled);

		if (id3_enabled == true)
		{
			ID3v2 tag;
			tag.SetVersion(4, 0);

			if (id3v2_event.GetFrameType() == "TXXX")
			{
				auto info = id3v2_event.GetInfo();
				auto data = id3v2_event.GetData();

				if (info == "${TriggerValue}")
				{
					info = value;
				}
				else if (data == "${TriggerValue}")
				{
					data = value;
				}

				tag.AddFrame(std::make_shared<ID3v2TxxxFrame>(info, data));
			}
			else if (id3v2_event.GetFrameType().UpperCaseString().Get(0) == 'T')
			{
				auto data = id3v2_event.GetData();

				if (data == "${TriggerValue}")
				{
					data = value;
				}

				tag.AddFrame(std::make_shared<ID3v2TextFrame>(id3v2_event.GetFrameType(), data));
			}
			// PRIV
			else if (id3v2_event.GetFrameType() == "PRIV")
			{
				auto owner = id3v2_event.GetInfo();
				auto data = id3v2_event.GetData();

				if (owner == "${TriggerValue}")
				{
					owner = value;
				}
				else if (data == "${TriggerValue}")
				{
					data = value;
				}

				tag.AddFrame(std::make_shared<ID3v2PrivFrame>(owner, data));
			}
			else
			{
				logaw("Unsupported ID3v2 frame type: %s", id3v2_event.GetFrameType().CStr());
				return;
			}

			cmn::PacketType packet_type = cmn::PacketType::EVENT;
			if (id3v2_event.GetEventType().LowerCaseString() == "video")
			{
				packet_type = cmn::PacketType::VIDEO_EVENT;
			}
			else if (id3v2_event.GetEventType().LowerCaseString() == "audio")
			{
				packet_type = cmn::PacketType::AUDIO_EVENT;
			}
			else if (id3v2_event.GetEventType().LowerCaseString() == "event")
			{
				packet_type = cmn::PacketType::EVENT;
			}
			else
			{
				logaw("Unsupported inject type: %s", id3v2_event.GetEventType().CStr());
				return;
			}

			int64_t pts = 0;
			if (packet_type == cmn::PacketType::VIDEO_EVENT)
			{
				pts = _last_video_pts_in_ms;
				pts += _last_video_pts_clock.Elapsed();
			}
			else if (packet_type == cmn::PacketType::AUDIO_EVENT)
			{
				pts = _last_audio_pts_in_ms;
				pts += _last_audio_pts_clock.Elapsed();
			}

			SendDataFrame(pts, cmn::BitstreamFormat::ID3v2, packet_type, tag.Serialize(), false);
		}
	}

	bool RtmpStream::CheckReadyToPublish()
	{
		if (_media_info->video_stream_coming == true && _media_info->audio_stream_coming == true)
		{
			return true;
		}
		// If this algorithm causes latency, it may be better to use meta data from AmfMeta.
		// But AmfMeta is not always correct so we need more consideration

		// It makes this stream video only
		else if (_media_info->video_stream_coming == true && _stream_message_cache_video_count > MAX_STREAM_MESSAGE_COUNT / 2)
		{
			return true;
		}
		// It makes this stream audio only
		else if (_media_info->audio_stream_coming == true && _stream_message_cache_audio_count > MAX_STREAM_MESSAGE_COUNT / 2)
		{
			return true;
		}

		return false;
	}

	//====================================================================================================
	// Chunk Message - Video Message
	// * Packet structure
	// 1.Control Header(1Byte - Frame Type + Codec Type)
	// 2.Type : 1Byte( 0x00 - Config Packet/ 0x00 -Frame Packet)
	// 3.Composition Time Offset : 3Byte
	// 4.Frame Size(4Byte)
	//
	// * Frame process
	// 1. SPS/PPS information
	// 2. SEI + I-Frame
	// 3. I-Frame/P-Frame repetition
	//
	// 0x06 :  NAL_SEI  first frame, uuid and codec information
	// 0x67 : NAL_SPS
	// 0x68 : NAL_PPS
	// 0x41 : NAL_SLICE -> from basic frame
	// 0x01 : NAL_SLICE -> from basic frame
	// 0x65 : NAL_SLICE_IDR -> from key frame
	//====================================================================================================
	bool RtmpStream::ReceiveVideoMessage(const std::shared_ptr<const RtmpMessage> &message)
	{
		if (message->header->message_length == 0)
		{
			// Nothing to do
			logaw("0-byte video message received");
			return true;
		}

		// size check
		if ((message->header->message_length < RTMP_VIDEO_DATA_MIN_SIZE) ||
			(message->header->message_length > RTMP_MAX_PACKET_SIZE))
		{
			logae("Invalid payload size: %d", message->header->message_length);
			return false;
		}

		const std::shared_ptr<const ov::Data> &payload = message->payload;

		if (!IsPublished())
		{
			_media_info->video_stream_coming = true;

			if (CheckReadyToPublish() == true)
			{
				if (PublishStream() == false)
				{
					logae("Input create fail");
					return false;
				}
			}
			else
			{
				// Store stream data until stream is published
				_stream_message_cache.push_back(message);
				_stream_message_cache_video_count++;
				if (_stream_message_cache.size() > MAX_STREAM_MESSAGE_COUNT)
				{
					logaw("Rtmp input stream init message count over - size(%zu:%d)",
						  _stream_message_cache.size(),
						  MAX_STREAM_MESSAGE_COUNT);
				}

				return true;
			}
		}

		// video stream callback
		if (_media_info->video_stream_coming)
		{
			// Parsing FLV
			flv::VideoData flv_video;
			if (flv_video.Parse(message->payload) == false)
			{
				logae("Could not parse flv video");
				return false;
			}

			if (flv_video.CompositionTime() < 0L)
			{
				if (_negative_cts_detected == false)
				{
					logaw("A negative CTS has been detected and will attempt to adjust the PTS. HLS/DASH may not play smoothly in the beginning.");
					_negative_cts_detected = true;
				}
			}

			int64_t dts = message->header->completed.timestamp;
			int64_t pts = dts + flv_video.CompositionTime();

			if (_negative_cts_detected)
			{
				pts += ADJUST_PTS;
			}

			auto video_track = GetTrack(RTMP_VIDEO_TRACK_ID);
			if (video_track == nullptr)
			{
				logae("Cannot get video track");
				return false;
			}

			dts *= video_track->GetVideoTimestampScale();
			pts *= video_track->GetVideoTimestampScale();

			cmn::PacketType packet_type = cmn::PacketType::Unknown;
			{
				const auto flv_packet_type = flv_video.PacketType();

				if (flv_packet_type == flv::AvcPacketType::SequenceHeader)
				{
					packet_type = cmn::PacketType::SEQUENCE_HEADER;
				}
				else if (flv_packet_type == flv::AvcPacketType::NALU)
				{
					packet_type = cmn::PacketType::NALU;
					AdjustTimestamp(video_track->GetId(), pts, dts);
				}
				else if (flv_packet_type == flv::AvcPacketType::EndOfSequence)
				{
					// what can I do?
					return true;
				}
			}

			auto data		 = std::make_shared<ov::Data>(flv_video.Payload(), flv_video.PayloadLength());
			auto video_frame = std::make_shared<MediaPacket>(GetMsid(),
															 cmn::MediaType::Video,
															 RTMP_VIDEO_TRACK_ID,
															 data,
															 pts,
															 dts,
															 -1LL,
															 MediaPacketFlag::Unknown,
															 cmn::BitstreamFormat::H264_AVCC,  // RTMP's packet type is AVCC
															 packet_type);

			SendFrame(video_frame);

			// logac("Video packet sent - stream(%s/%s) type(%d) size(%d) pts(%" PRId64 ") dts(%" PRId64 ")",
			// 	  _vhost_app_name.CStr(),
			// 	  _stream_name.CStr(),
			// 	  flv_video.PacketType(),
			// 	  flv_video.PayloadLength(),
			// 	  pts,
			// 	  dts);

			_last_video_pts_in_ms = dts;

			if (_last_video_pts_clock.IsStart() == false)
			{
				_last_video_pts_clock.Start();
			}
			else
			{
				_last_video_pts_clock.Update();
			}

			// Statistics for debugging
			if (flv_video.FrameType() == flv::VideoFrameType::Key)
			{
				_key_frame_interval = message->header->completed.timestamp - _previous_key_frame_timestamp;
				_previous_key_frame_timestamp = message->header->completed.timestamp;
				video_frame->SetFlag(MediaPacketFlag::Key);
			}
			else
			{
				video_frame->SetFlag(MediaPacketFlag::NoFlag);
			}

			_last_video_timestamp = message->header->completed.timestamp;
			_video_frame_count++;

			time_t current_time = time(nullptr);
			uint32_t check_gap = current_time - _stream_check_time;

			if (check_gap >= 10)
			{
				logi("RTMPProvider.Stat", "Rtmp Provider Info - stream(%s/%s) key(%ums) timestamp(v:%ums/a:%ums/g:%dms) fps(v:%u/a:%u) gap(v:%ums/a:%ums)",
					 _vhost_app_name.CStr(),
					 _stream_name.CStr(),
					 _key_frame_interval,
					 _last_video_timestamp,
					 _last_audio_timestamp,
					 _last_video_timestamp - _last_audio_timestamp,
					 _video_frame_count / check_gap,
					 _audio_frame_count / check_gap,
					 _last_video_timestamp - _previous_last_video_timestamp,
					 _last_audio_timestamp - _previous_last_audio_timestamp);

				_stream_check_time = time(nullptr);
				_video_frame_count = 0;
				_audio_frame_count = 0;
				_previous_last_video_timestamp = _last_video_timestamp;
				_previous_last_audio_timestamp = _last_audio_timestamp;
			}
		}

		return true;
	}

	//====================================================================================================
	// Chunk Message - Audio Message
	// * Packet structure
	// 1. AAC
	// - Control Header : 	1Byte(Codec/SampleRate/SampleSize/Channels)
	// - Sequence Type : 	1Byte(0x00 - Config Packet/ 0x00 -Frame Packet)
	// - Frame
	//
	// 2.Speex/MP3
	// - Control Header(1Byte - Codec/SampleRate/SampleSize/Channels)
	// - Frame
	//
	//AAC : Adts header information
	//Speex : size information
	//====================================================================================================
	bool RtmpStream::ReceiveAudioMessage(const std::shared_ptr<const RtmpMessage> &message)
	{
		if (message->header->message_length == 0)
		{
			// Nothing to do
			logaw("0-byte audio message received");
			return true;
		}

		// size check
		if ((message->header->message_length < RTMP_AAC_AUDIO_DATA_MIN_SIZE) ||
			(message->header->message_length > RTMP_MAX_PACKET_SIZE))
		{
			logae("Invalid payload size: size(%d)", message->header->message_length);

			return false;
		}

		if (!IsPublished())
		{
			_media_info->audio_stream_coming = true;

			if (CheckReadyToPublish() == true)
			{
				if (PublishStream() == false)
				{
					logae("Input create fail");
					return false;
				}
			}
			else
			{
				// Store stream data until stream is published
				_stream_message_cache.push_back(message);
				_stream_message_cache_audio_count++;
				if (_stream_message_cache.size() > MAX_STREAM_MESSAGE_COUNT)
				{
					logaw("Rtmp input stream init message count over -  size(%zu:%d)",
						  _stream_message_cache.size(),
						  MAX_STREAM_MESSAGE_COUNT);
				}

				return true;
			}
		}

		// audio stream callback
		if (_media_info->audio_stream_coming)
		{
			// Get audio track info
			auto audio_track = GetTrack(RTMP_AUDIO_TRACK_ID);
			if (audio_track == nullptr)
			{
				logae("Cannot get audio track");
				return false;
			}

			// Parsing FLV
			flv::AudioData flv_audio;
			if (flv_audio.Parse(message->payload) == false)
			{
				logae("Could not parse flv audio");
				return false;
			}

			auto data = std::make_shared<ov::Data>(flv_audio.Payload(), flv_audio.PayloadLength());

			int64_t dts = message->header->completed.timestamp;
			int64_t pts = dts;

			if (_negative_cts_detected)
			{
				pts += ADJUST_PTS;
			}

			cmn::PacketType packet_type = cmn::PacketType::Unknown;
			{
				const auto flv_packet_type = flv_audio.PacketType();

				if (flv_packet_type == flv::AACPacketType::SequenceHeader)
				{
					packet_type = cmn::PacketType::SEQUENCE_HEADER;
				}
				else if (flv_packet_type == flv::AACPacketType::Raw)
				{
					packet_type = cmn::PacketType::RAW;
					AdjustTimestamp(audio_track->GetId(), pts, dts);
				}
			}

			auto frame = std::make_shared<MediaPacket>(GetMsid(),
													   cmn::MediaType::Audio,
													   RTMP_AUDIO_TRACK_ID,
													   data,
													   pts,
													   dts,
													   -1LL,
													   MediaPacketFlag::Unknown,
													   cmn::BitstreamFormat::AAC_RAW,
													   packet_type);

			SendFrame(frame);

			_last_audio_pts_in_ms = dts;

			if (_last_audio_pts_clock.IsStart() == false)
			{
				_last_audio_pts_clock.Start();
			}
			else
			{
				_last_audio_pts_clock.Update();
			}

			_last_audio_timestamp = message->header->completed.timestamp;
			_audio_frame_count++;
		}

		return true;
	}

	// Make PTS/DTS of first frame are 0
	void RtmpStream::AdjustTimestamp(uint32_t track_id, int64_t &pts, int64_t &dts)
	{
		AdjustTimestampByBase(track_id, pts, dts, std::numeric_limits<int64_t>::max());
	}

	bool RtmpStream::PublishStream()
	{
		if (_publish_url == nullptr)
		{
			logae("Publish url is not set");
			return false;
		}

		_vhost_app_name = ocst::Orchestrator::GetInstance()->ResolveApplicationNameFromDomain(_publish_url->Host(), _publish_url->App());
		_stream_name = _publish_url->Stream();

		UpdateNamePath(_vhost_app_name);

		// Get application config
		if (GetProvider() == nullptr)
		{
			logae("Could not find provider");
			return false;
		}

		auto application = GetProvider()->GetApplicationByName(_vhost_app_name);
		if (application == nullptr)
		{
			logae("Could not find application");
			Stop();
			return false;
		}

		const auto &rtmp_provider = application->GetConfig().GetProviders().GetRtmpProvider();

		_event_generator = rtmp_provider.GetEventGenerator();

		SetName(_publish_url->Stream());

		// Set Track Info
		SetTrackInfo(_media_info);

		// Publish
		if (PublishChannel(_vhost_app_name) == false)
		{
			Stop();
			return false;
		}

#if 0
		// Keep Alive Data Channel
		_event_test_timer.Push(
		[this](void *parameter) -> ov::DelayQueueAction {
			
			for (const auto &event : _event_generator.GetEvents())
			{
				if (event.GetTrigger() == "KEEP-ALIVE")
				{
					GenerateEvent(event, "KEEP-ALIVE");
				}
			}

			return ov::DelayQueueAction::Repeat;
		},
		500);
		_event_test_timer.Start();
#endif

		//   stored messages
		for (auto message : _stream_message_cache)
		{
			if ((message->header->completed.type_id == RtmpMessageTypeID::Video) && _media_info->video_stream_coming)
			{
				ReceiveVideoMessage(message);
			}
			else if ((message->header->completed.type_id == RtmpMessageTypeID::Audio) && _media_info->audio_stream_coming)
			{
				ReceiveAudioMessage(message);
			}
		}
		_stream_message_cache.clear();

		return true;
	}

	bool RtmpStream::SetTrackInfo(const std::shared_ptr<RtmpMediaInfo> &media_info)
	{
		if (media_info->video_stream_coming)
		{
			auto new_track = std::make_shared<MediaTrack>();

			new_track->SetId(RTMP_VIDEO_TRACK_ID);
			new_track->SetMediaType(cmn::MediaType::Video);
			new_track->SetCodecId(cmn::MediaCodecId::H264);
			new_track->SetOriginBitstream(cmn::BitstreamFormat::H264_AVCC);
			new_track->SetTimeBase(1, 1000);
			new_track->SetVideoTimestampScale(1.0);

			// Below items are not mandatory, it will be parsed again from SPS parser
			new_track->SetWidth((uint32_t)media_info->video_width);
			new_track->SetHeight((uint32_t)media_info->video_height);
			new_track->SetFrameRateByConfig(media_info->video_framerate);

			// Kbps -> bps, it is just metadata
			new_track->SetBitrateByConfig(media_info->video_bitrate * 1000);

			AddTrack(new_track);
		}

		if (media_info->audio_stream_coming)
		{
			auto new_track = std::make_shared<MediaTrack>();

			new_track->SetId(RTMP_AUDIO_TRACK_ID);
			new_track->SetMediaType(cmn::MediaType::Audio);
			new_track->SetCodecId(cmn::MediaCodecId::Aac);
			new_track->SetOriginBitstream(cmn::BitstreamFormat::AAC_RAW);
			new_track->SetTimeBase(1, 1000);

			//////////////////
			// Below items are not mandatory, it will be parsed again from ADTS parser
			//////////////////
			new_track->SetSampleRate(media_info->audio_samplerate);
			new_track->GetSample().SetFormat(cmn::AudioSample::Format::S16);
			// Kbps -> bps
			new_track->SetBitrateByConfig(media_info->audio_bitrate * 1000);
			// new_track->SetSampleSize(conn->_audio_samplesize);

			if (media_info->audio_channels == 1)
			{
				new_track->GetChannel().SetLayout(cmn::AudioChannel::Layout::LayoutMono);
			}
			else if (media_info->audio_channels == 2)
			{
				new_track->GetChannel().SetLayout(cmn::AudioChannel::Layout::LayoutStereo);
			}

			AddTrack(new_track);
		}

		// Data Track
		if (GetFirstTrackByType(cmn::MediaType::Data) == nullptr)
		{
			auto data_track = std::make_shared<MediaTrack>();

			data_track->SetId(RTMP_DATA_TRACK_ID);
			data_track->SetMediaType(cmn::MediaType::Data);
			// Since elsewhere, such as in `SendDataFrame()`, the data track’s timebase is assumed to be 1/1000, you must not arbitrarily change the timebase
			data_track->SetTimeBase(1, 1000);
			data_track->SetOriginBitstream(cmn::BitstreamFormat::Unknown);

			AddTrack(data_track);
		}

		return true;
	}

	bool RtmpStream::SendData(const std::shared_ptr<const ov::Data> &data)
	{
		if (data == nullptr)
		{
			return false;
		}

		return _remote->Send(data);
	}

	bool RtmpStream::SendData(const void *data, size_t data_size)
	{
		if (data == nullptr)
		{
			return false;
		}

		return _remote->Send(data, data_size);
	}

	bool RtmpStream::SendMessagePacket(std::shared_ptr<RtmpMuxMessageHeader> &message_header, const ov::Data *data)
	{
		auto export_data = _export_chunk->ExportStreamData(message_header, data->GetDataAs<uint8_t>(), data->GetLength());

		return SendData(export_data->data(), export_data->size());
	}

	bool RtmpStream::SendMessagePacket(std::shared_ptr<RtmpMuxMessageHeader> &message_header, std::shared_ptr<std::vector<uint8_t>> &data)
	{
		auto export_data = _export_chunk->ExportStreamData(message_header, data->data(), data->size());

		return SendData(export_data->data(), export_data->size());
	}

	//====================================================================================================
	// Send handshake packet
	// s0 + s1 + s2
	//====================================================================================================
	bool RtmpStream::SendHandshake(const std::shared_ptr<const ov::Data> &data)
	{
		uint8_t s0 = 0;
		uint8_t s1[RTMP_HANDSHAKE_PACKET_SIZE]{};
		uint8_t s2[RTMP_HANDSHAKE_PACKET_SIZE]{};

		s0 = RTMP_HANDSHAKE_VERSION;

		RtmpHandshake::MakeS1(s1);
		RtmpHandshake::MakeS2(data->GetDataAs<uint8_t>() + sizeof(uint8_t), s2);

		_handshake_state = RtmpHandshakeState::C1;

		// OM-1629 - Elemental Encoder
		ov::Data dataToSend;

		dataToSend.Append(&s0, sizeof(s0));
		dataToSend.Append(s1, sizeof(s1));
		dataToSend.Append(s2, sizeof(s2));

		if (SendData(dataToSend.GetData(), dataToSend.GetLength()) == false)
		{
			logae("Handshake Send Fail");
			return false;
		}

		_handshake_state = RtmpHandshakeState::S2;

		return true;
	}

	bool RtmpStream::SendUserControlMessage(UserControlMessageId message, std::shared_ptr<std::vector<uint8_t>> &data)
	{
		auto message_header = RtmpMuxMessageHeader::Create(
			RtmpChunkStreamId::Urgent, RtmpMessageTypeID::UserControl, 0, data->size() + 2);

		data->insert(data->begin(), 0);
		data->insert(data->begin(), 0);
		RtmpMuxUtil::WriteInt16(data->data(), ov::ToUnderlyingType(message));

		return SendMessagePacket(message_header, data);
	}

	bool RtmpStream::SendWindowAcknowledgementSize(uint32_t size)
	{
		auto body = std::make_shared<std::vector<uint8_t>>(sizeof(int));
		auto message_header = RtmpMuxMessageHeader::Create(
			RtmpChunkStreamId::Urgent, RtmpMessageTypeID::WindowAcknowledgementSize, 0, body->size());

		RtmpMuxUtil::WriteInt32(body->data(), size);

		return SendMessagePacket(message_header, body);
	}

	bool RtmpStream::SendAcknowledgementSize(uint32_t acknowledgement_traffic)
	{
		auto body = std::make_shared<std::vector<uint8_t>>(sizeof(int));
		auto message_header = RtmpMuxMessageHeader::Create(
			RtmpChunkStreamId::Urgent, RtmpMessageTypeID::Acknowledgement, 0, body->size());

		RtmpMuxUtil::WriteInt32(body->data(), acknowledgement_traffic);

		return SendMessagePacket(message_header, body);
	}

	bool RtmpStream::SendSetPeerBandwidth(uint32_t bandwidth)
	{
		auto body = std::make_shared<std::vector<uint8_t>>(5);
		auto message_header = RtmpMuxMessageHeader::Create(
			RtmpChunkStreamId::Urgent, RtmpMessageTypeID::SetPeerBandwidth, 0, body->size());

		RtmpMuxUtil::WriteInt32(body->data(), bandwidth);
		RtmpMuxUtil::WriteInt8(body->data() + 4, 2);

		return SendMessagePacket(message_header, body);
	}

	bool RtmpStream::SendStreamBegin(uint32_t stream_id)
	{
		auto body = std::make_shared<std::vector<uint8_t>>(4);

		RtmpMuxUtil::WriteInt32(body->data(), stream_id);

		return SendUserControlMessage(UserControlMessageId::StreamBegin, body);
	}

	bool RtmpStream::SendStreamEnd()
	{
		auto body = std::make_shared<std::vector<uint8_t>>(4);

		RtmpMuxUtil::WriteInt32(body->data(), _rtmp_stream_id);

		return SendUserControlMessage(UserControlMessageId::StreamEof, body);
	}
	
	bool RtmpStream::SendSetChunkSize(uint32_t chunk_size)
	{
		auto body			= std::make_shared<std::vector<uint8_t>>(sizeof(int));
		auto message_header = RtmpMuxMessageHeader::Create(
			RtmpChunkStreamId::Urgent, RtmpMessageTypeID::SetChunkSize, 0, body->size());

		RtmpMuxUtil::WriteInt32(body->data(), chunk_size);

		return SendMessagePacket(message_header, body);
	}

	bool RtmpStream::SendAmfCommand(std::shared_ptr<RtmpMuxMessageHeader> &message_header, AmfDocument &document)
	{
		if (message_header == nullptr)
		{
			return false;
		}

		ov::ByteStream stream(2048);
		if (document.Encode(stream) == false)
		{
			return false;
		}

		auto data = stream.GetData();
		message_header->body_size = data->GetLength();

		return SendMessagePacket(message_header, data);
	}

	bool RtmpStream::SendAmfConnectResult(double transaction_id, double object_encoding)
	{
		auto message_header = RtmpMuxMessageHeader::Create(RtmpChunkStreamId::Control);

		AmfDocument document;

		// _result
		document.AppendProperty(StringFromRtmpCommand(RtmpCommand::AckResult));
		document.AppendProperty(transaction_id);

		// properties
		{
			AmfObject object;
			object.Append("fmsVer", "FMS/3,5,2,654");
			object.Append("capabilities", 31.0);
			object.Append("mode", 1.0);
			document.AppendProperty(object);
		}

		// information
		{
			AmfObject object;
			object.Append("level", "status");
			object.Append("code", "NetConnection.Connect.Success");
			object.Append("description", "Connection succeeded.");
			object.Append("clientid", _client_id);
			object.Append("objectEncoding", object_encoding);

			AmfEcmaArray array;
			array.Append("version", "3,5,2,654");
			object.Append("data", array);

			document.AppendProperty(object);
		}

		return SendAmfCommand(message_header, document);
	}

	bool RtmpStream::SendAmfOnFCPublish(double client_id)
	{
		auto message_header = RtmpMuxMessageHeader::Create(RtmpChunkStreamId::Control, RtmpMessageTypeID::Amf0Command, 0);

		AmfDocument document;

		document.AppendProperty(StringFromRtmpCommand(RtmpCommand::OnFCPublish));
		document.AppendProperty(0.0);
		document.AppendProperty(AmfProperty::NullProperty());

		{
			AmfObject object;
			object.Append("level", "status");
			object.Append("code", "NetStream.Publish.Start");
			object.Append("description", "FCPublish");
			object.Append("clientid", client_id);
			document.AppendProperty(object);
		}

		return SendAmfCommand(message_header, document);
	}

	bool RtmpStream::SendAmfCreateStreamResult(uint32_t chunk_stream_id, double transaction_id)
	{
		auto message_header = RtmpMuxMessageHeader::Create(chunk_stream_id);

		AmfDocument document;

		// 스트림ID 정하기
		_rtmp_stream_id = 1;

		document.AppendProperty(StringFromRtmpCommand(RtmpCommand::AckResult));
		document.AppendProperty(transaction_id);
		document.AppendProperty(AmfProperty::NullProperty());
		document.AppendProperty(static_cast<double>(_rtmp_stream_id));

		return SendAmfCommand(message_header, document);
	}

	bool RtmpStream::SendAmfOnStatus(uint32_t chunk_stream_id,
									 uint32_t stream_id,
									 const char *level,
									 const char *code,
									 const char *description,
									 double client_id)
	{
		auto message_header = RtmpMuxMessageHeader::Create(chunk_stream_id, RtmpMessageTypeID::Amf0Command, stream_id);
		AmfDocument document;

		document.AppendProperty(StringFromRtmpCommand(RtmpCommand::OnStatus));
		document.AppendProperty(0.0);
		document.AppendProperty(AmfProperty::NullProperty());

		{
			AmfObject object;
			object.Append("level", level);
			object.Append("code", code);
			object.Append("description", description);
			object.Append("clientid", client_id);
			document.AppendProperty(object);
		}

		return SendAmfCommand(message_header, document);
	}

	ov::String RtmpStream::GetCodecString(RtmpCodecType codec_type)
	{
		ov::String codec_string;

		switch (codec_type)
		{
			case RtmpCodecType::H264:
				codec_string = "h264";
				break;
			case RtmpCodecType::AAC:
				codec_string = "aac";
				break;
			case RtmpCodecType::MP3:
				codec_string = "mp3";
				break;
			case RtmpCodecType::Speex:
				codec_string = "speex";
				break;
			case RtmpCodecType::Unknown:
			default:
				codec_string = "unknown";
				break;
		}

		return codec_string;
	}

	ov::String RtmpStream::GetEncoderTypeString(RtmpEncoderType encoder_type)
	{
		ov::String codec_string;

		switch (encoder_type)
		{
			case RtmpEncoderType::Xsplit:
				codec_string = "Xsplit";
				break;
			case RtmpEncoderType::OBS:
				codec_string = "OBS";
				break;
			case RtmpEncoderType::Lavf:
				codec_string = "Lavf/ffmpeg";
				break;
			case RtmpEncoderType::Custom:
				codec_string = "Unknown";
				break;

			default:
				codec_string = "Unknown";
				break;
		}

		return codec_string;
	}
}  // namespace pvd
