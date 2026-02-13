//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================

#include "rtspc_stream.h"

#include <base/info/application.h>
#include <base/ovlibrary/byte_io.h>
#include <modules/rtp_rtcp/rtp_depacketizer_mpeg4_generic_audio.h>

#include "rtspc_provider.h"

#define OV_LOG_TAG "RtspcStream"

namespace pvd
{
	std::shared_ptr<RtspcStream> RtspcStream::Create(const std::shared_ptr<pvd::PullApplication> &application,
													 const uint32_t stream_id, const ov::String &stream_name,
													 const std::vector<ov::String> &url_list,
													 const std::shared_ptr<pvd::PullStreamProperties> &properties)
	{
		info::Stream stream_info(*std::static_pointer_cast<info::Application>(application), StreamSourceType::RtspPull);

		stream_info.SetId(stream_id);
		stream_info.SetName(stream_name);

		auto stream = std::make_shared<RtspcStream>(application, stream_info, url_list, properties);
		if (!stream->PullStream::Start())
		{
			// Explicit deletion
			stream.reset();
			return nullptr;
		}

		return stream;
	}

	RtspcStream::RtspcStream(const std::shared_ptr<pvd::PullApplication> &application, const info::Stream &stream_info, const std::vector<ov::String> &url_list, const std::shared_ptr<pvd::PullStreamProperties> &properties)
		: pvd::PullStream(application, stream_info, url_list, properties), Node(NodeType::Rtsp), _sdp(SessionDescription::SdpType::Answer)
	{
		SetState(State::IDLE);
	}

	RtspcStream::~RtspcStream()
	{
		PullStream::Stop();
		Release();
	}

	std::shared_ptr<pvd::RtspcProvider> RtspcStream::GetRtspcProvider()
	{
		return std::static_pointer_cast<RtspcProvider>(GetApplication()->GetParentProvider());
	}

	bool RtspcStream::AddDepacketizer(uint8_t payload_type, RtpDepacketizingManager::SupportedDepacketizerType codec_id)
	{
		// Depacketizer
		auto depacketizer = RtpDepacketizingManager::Create(codec_id);
		if (depacketizer == nullptr)
		{
			logte("%s - Could not create depacketizer : codec_id(%d)", GetName().CStr(), static_cast<uint8_t>(codec_id));
			return false;
		}

		_depacketizers[payload_type] = depacketizer;

		return true;
	}

	std::shared_ptr<RtpDepacketizingManager> RtspcStream::GetDepacketizer(uint8_t payload_type)
	{
		auto it = _depacketizers.find(payload_type);
		if (it == _depacketizers.end())
		{
			return nullptr;
		}

		return it->second;
	}

	void RtspcStream::Release()
	{
		if (_rtp_rtcp != nullptr)
		{
			_rtp_rtcp->Stop();
		}

		if (_signalling_socket != nullptr)
		{
			_signalling_socket->Close();
		}
	}

	bool RtspcStream::StartStream(const std::shared_ptr<const ov::Url> &url)
	{
		// Only start from IDLE, ERROR, STOPPED
		if (!(GetState() == State::IDLE || GetState() == State::ERROR || GetState() == State::STOPPED))
		{
			return true;
		}

		_curr_url = url;
		_sent_sequence_header = false;

		ov::StopWatch stop_watch;

		stop_watch.Start();
		if (ConnectTo() == false)
		{
			return false;
		}
		_origin_request_time_msec = stop_watch.Elapsed();

		stop_watch.Update();

		if (RequestDescribe() == false)
		{
			Release();
			return false;
		}

		if (RequestSetup() == false)
		{
			Release();
			return false;
		}

		if (RequestPlay() == false)
		{
			Release();
			return false;
		}

		_origin_response_time_msec = stop_watch.Elapsed();

		// Stream was created completly
		_stream_metrics = StreamMetrics(*std::static_pointer_cast<info::Stream>(PullStream::GetSharedPtr()));
		if (_stream_metrics != nullptr)
		{
			_stream_metrics->SetOriginConnectionTimeMSec(_origin_request_time_msec);
			_stream_metrics->SetOriginSubscribeTimeMSec(_origin_response_time_msec);
		}

		return true;
	}

	bool RtspcStream::RestartStream(const std::shared_ptr<const ov::Url> &url)
	{
		logti("[%s/%s(%u)] stream tries to reconnect to %s", GetApplicationTypeName(), GetName().CStr(), GetId(), url->ToUrlString().CStr());
		return StartStream(url);
	}

	bool RtspcStream::StopStream()
	{
		if (GetState() == State::STOPPED)
		{
			return true;
		}

		if (!RequestStop())
		{
			// Force terminate
			SetState(State::ERROR);
		}

		Release();

		ov::Node::Stop();

		return true;
	}

	bool RtspcStream::ConnectTo()
	{
		if (GetState() == State::PLAYING || GetState() == State::TERMINATED)
		{
			return false;
		}

		logtt("Requested url[%d] : %s", strlen(_curr_url->Source().CStr()), _curr_url->Source().CStr());

		auto scheme = _curr_url->Scheme();
		if (scheme.UpperCaseString() != "RTSP")
		{
			SetState(State::ERROR);
			logte("The scheme is not rtsp : %s", scheme.CStr());
			return false;
		}

		auto signalling_socket_pool = GetRtspcProvider()->GetSignallingSocketPool();
		if (signalling_socket_pool == nullptr)
		{
			// Provider is not initialized
			logte("Could not get socket from socket pool");
			return false;
		}

		// 554 is default port of RTSP
		auto socket_address = ov::SocketAddress::CreateAndGetFirst(_curr_url->Host(), _curr_url->Port() == 0 ? 554 : _curr_url->Port());

		// Connect
		_signalling_socket = signalling_socket_pool->AllocSocket(socket_address.GetFamily());
		if (_signalling_socket == nullptr)
		{
			SetState(State::ERROR);
			logte("To create client socket is failed.");

			_signalling_socket = nullptr;
			return false;
		}

		_signalling_socket->MakeBlocking();

		auto error = _signalling_socket->Connect(socket_address, 3000);
		if (error != nullptr)
		{
			SetState(State::ERROR);
			logte("Cannot connect to server (%s) : %s:%d", error->GetMessage().CStr(), _curr_url->Host().CStr(), _curr_url->Port());
			return false;
		}

		SetState(State::CONNECTED);

		return true;
	}

	bool RtspcStream::RequestDescribe()
	{
		if (GetState() != State::CONNECTED)
		{
			return false;
		}

		auto describe = std::make_shared<RtspMessage>(RtspMethod::DESCRIBE, GetNextCSeq(), _curr_url->ToUrlString(true));
		describe->AddHeaderField(std::make_shared<RtspHeaderField>(RtspHeaderFieldType::Accept, "application/sdp"));
		describe->AddHeaderField(std::make_shared<RtspHeaderField>(RtspHeaderFieldType::UserAgent, RTSP_USER_AGENT_NAME));

		logti("Request Describe : %s", describe->DumpHeader().CStr());

		std::shared_ptr<RtspMessage> reply = nullptr;
		for (int i = 0; i < 2; i++)
		{
			if (SendRequestMessage(describe) == false)
			{
				SetState(State::ERROR);
				logte("Could not request DESCIBE to RTSP server (%s)", _curr_url->ToUrlString().CStr());
				return false;
			}

			reply = ReceiveResponse(describe->GetCSeq(), 3000);
			if (reply == nullptr)
			{
				SetState(State::ERROR);
				logte("No response(CSeq : %u) was received from the rtsp server(%s)", describe->GetCSeq(), _curr_url->ToUrlString().CStr());
				return false;
			}
			// Unauthorized, try to authenticate
			else if (reply->GetStatusCode() == 401)
			{
				// Authorization has been failed
				if (i == 1)
				{
					SetState(State::ERROR);
					logte("Rtsp server(%s) rejected the describe request : %d(%s) | ID/Password may be incorrect.", _curr_url->ToUrlString().CStr(), reply->GetStatusCode(), reply->GetReasonPhrase().CStr());
					return false;
				}

				auto authenticate_field = reply->GetHeaderFieldAs<RtspHeaderWWWAuthenticateField>(RtspHeaderField::FieldTypeToString(RtspHeaderFieldType::WWWAuthenticate));
				if (authenticate_field == nullptr)
				{
					SetState(State::ERROR);
					logte("Rtsp server(%s) rejected the describe request : %d(%s)", _curr_url->ToUrlString().CStr(), reply->GetStatusCode(), reply->GetReasonPhrase().CStr());
					return false;
				}

				// Add authorization field
				if (authenticate_field->GetScheme() == RtspHeaderWWWAuthenticateField::Scheme::Basic)
				{
					_authorization_field = RtspHeaderAuthorizationField::CreateRtspBasicAuthorizationField(_curr_url->Id(), _curr_url->Password());
					describe->AddHeaderField(_authorization_field);

					// Try to send again
					continue;
				}
				else if (authenticate_field->GetScheme() == RtspHeaderWWWAuthenticateField::Scheme::Digest)
				{
					_authorization_field = RtspHeaderAuthorizationField::CreateRtspDigestAuthorizationField(_curr_url->Id(), _curr_url->Password(),
																											describe->GetMethodStr(), describe->GetRequestUri(),
																											authenticate_field->GetRealm(), authenticate_field->GetNonce());
					describe->AddHeaderField(_authorization_field);

					// Try to send again
					continue;
				}
				else
				{
					SetState(State::ERROR);
					logte("Rtsp server(%s) rejected the describe request : %d(%s)", _curr_url->ToUrlString().CStr(), reply->GetStatusCode(), reply->GetReasonPhrase().CStr());
					return false;
				}
			}
			else if (reply->GetStatusCode() != 200)
			{
				SetState(State::ERROR);
				logte("Rtsp server(%s) rejected the describe request : %d(%s)", _curr_url->ToUrlString().CStr(), reply->GetStatusCode(), reply->GetReasonPhrase().CStr());
				return false;
			}
			else
			{
				// Success
				break;
			}
		}

		logti("Response Describe : %s", reply->DumpHeader().CStr());

		// Content-Base
		auto content_base_field = reply->GetHeaderField(RtspHeaderField::FieldTypeToString(RtspHeaderFieldType::ContentBase));
		if (content_base_field != nullptr)
		{
			_content_base = content_base_field->GetValue();

			// If surfix of content_base is not '/', then add '/'
			if (_content_base.IsEmpty() == false && _content_base.Right(1) != "/")
			{
				_content_base.Append("/");
			}
		}

		// Session
		auto session_field = reply->GetHeaderFieldAs<RtspHeaderSessionField>(RtspHeaderField::FieldTypeToString(RtspHeaderFieldType::Session));
		if (session_field == nullptr)
		{
			_rtsp_session_id = "";
		}
		else
		{
			// Session  = "Session" ":" session-id [ ";" "timeout" "=" delta-seconds ]
			_rtsp_session_id = session_field->GetSessionId();
			// timeout
			[[maybe_unused]] auto timeout_delta_seconds = session_field->GetTimeoutDeltaSeconds();
		}

		if (reply->GetBody() == nullptr)
		{
			SetState(State::ERROR);
			logte("There is no SDP in the describe response. Url(%s) CSeq(%d)", _curr_url->ToUrlString().CStr(), describe->GetCSeq());
			return false;
		}

		// Parse SDP to add track information
		if (_sdp.FromString(reply->GetBody()->ToString()) == false)
		{
			SetState(State::ERROR);
			logte("Parsing of SDP received from rtsp url (%s)failed. ", _curr_url->ToUrlString().CStr());
			return false;
		}

		logti("SDP : %s\n", reply->GetBody()->ToString().CStr());

		ov::Node::Start();

		SetState(State::DESCRIBED);

		return true;
	}

	bool RtspcStream::RequestSetup()
	{
		if (GetState() != State::DESCRIBED)
		{
			return false;
		}

		int interleaved_channel = 0;

		_rtp_rtcp = std::make_shared<RtpRtcp>(RtpRtcpInterface::GetSharedPtr());

		auto media_desc_list = _sdp.GetMediaList();
		for (const auto &media_desc : media_desc_list)
		{
			if (media_desc->GetMediaType() == MediaDescription::MediaType::Application || media_desc->GetMediaType() == MediaDescription::MediaType::Unknown)
			{
				logtw("Ignored not supported media type : %s", media_desc->GetMediaTypeStr().CStr());
				continue;
			}

			{
				// Reject unsupported codec
				//TODO: I have no idea if rtsp server returns multiple payload types
				auto first_payload = media_desc->GetFirstPayload();
				if (first_payload == nullptr)
				{
					logte("Failed to get the first Payload type of peer sdp");
					return false;
				}

				switch (first_payload->GetCodec())
				{
					case PayloadAttr::SupportCodec::H264:
					case PayloadAttr::SupportCodec::H265:
					case PayloadAttr::SupportCodec::VP8:
					case PayloadAttr::SupportCodec::MPEG4_GENERIC:
					case PayloadAttr::SupportCodec::OPUS:
						break;

					default:
						logte("%s - Unsupported codec  : %s, it will be ignored", GetName().CStr(), first_payload->GetCodecStr().CStr());
						continue;
				}
			}

			auto control = media_desc->GetControl();
			if (control.IsEmpty())
			{
				SetState(State::ERROR);
				logte("Could not get control attribute in (%s) ", _curr_url->ToUrlString().CStr());
				return false;
			}

			auto control_url = GenerateControlUrl(control);
			if (control_url.IsEmpty())
			{
				SetState(State::ERROR);
				logte("Could not make control url with (%s) ", control.CStr());
				return false;
			}

			auto setup = std::make_shared<RtspMessage>(RtspMethod::SETUP, GetNextCSeq(), control_url);
			if (_authorization_field != nullptr)
			{
				// If authorization method is Digest, update the method and uri
				if (_authorization_field->GetScheme() == RtspHeaderWWWAuthenticateField::Scheme::Digest)
				{
					_authorization_field->UpdateDigestAuth(setup->GetMethodStr(), setup->GetRequestUri());
				}

				setup->AddHeaderField(_authorization_field);
			}

			// Now RtspcStream only supports RTP/AVP/TCP;unicast/interleaved(rtp+rtcp)
			// The chennel id can be used for demuxing, but since it is already demuxing in a different way, it is not saved.
			setup->AddHeaderField(std::make_shared<RtspHeaderField>(RtspHeaderFieldType::Transport,
																	ov::String::FormatString("RTP/AVP/TCP;unicast;interleaved=%d-%d;ssrc=%X", interleaved_channel, interleaved_channel + 1, ov::Random::GenerateUInt32())));
			if (_rtsp_session_id.IsEmpty() == false)
			{
				setup->AddHeaderField(std::make_shared<RtspHeaderField>(RtspHeaderFieldType::Session, _rtsp_session_id));
			}
			setup->AddHeaderField(std::make_shared<RtspHeaderField>(RtspHeaderFieldType::UserAgent, RTSP_USER_AGENT_NAME));

			if (SendRequestMessage(setup) == false)
			{
				SetState(State::ERROR);
				logte("Could not request setup to RTSP server (%s)", _curr_url->ToUrlString().CStr());
				return false;
			}

			logti("Request SETUP : %s", setup->DumpHeader().CStr());

			auto reply = ReceiveResponse(setup->GetCSeq(), 3000);
			if (reply == nullptr)
			{
				SetState(State::ERROR);
				logte("No response(CSeq : %u) was received from the rtsp server(%s)", setup->GetCSeq(), _curr_url->ToUrlString().CStr());
				return false;
			}
			else if (reply->GetStatusCode() != 200)
			{
				SetState(State::ERROR);
				logte("Rtsp server(%s) rejected the setup request : %d(%s)", _curr_url->ToUrlString().CStr(), reply->GetStatusCode(), reply->GetReasonPhrase().CStr());
				return false;
			}

			logti("Response SETUP : %s", reply->DumpHeader().CStr());

			// Session
			auto session_field = reply->GetHeaderFieldAs<RtspHeaderSessionField>(RtspHeaderField::FieldTypeToString(RtspHeaderFieldType::Session));
			if (session_field == nullptr)
			{
				_rtsp_session_id = "";
			}
			else
			{
				// Session  = "Session" ":" session-id [ ";" "timeout" "=" delta-seconds ]
				_rtsp_session_id = session_field->GetSessionId();
				// timeout
				_rtsp_session_timeout_sec = session_field->GetTimeoutDeltaSeconds();
				if (_rtsp_session_timeout_sec == 0)
				{
					_rtsp_session_timeout_sec = DEFAULT_RTSP_SESSION_TIMEOUT_SEC;
				}
			}

			// Transport
			auto transport_field = reply->GetHeaderFieldAs<RtspHeaderTransportField>(RtspHeaderField::FieldTypeToString(RtspHeaderFieldType::Transport));
			if (transport_field == nullptr)
			{
				SetState(State::ERROR);
				logte("There is no Transport header in the response from the RTSP server(%s)", _curr_url->ToUrlString().CStr());
				return false;
			}
			else
			{
				// Some rtsp server ignores this value, so it is unusable
				// transport_field->GetSsrc();
				if (transport_field->IsInterleavedParsed())
				{
					interleaved_channel = transport_field->GetInterleavedChannelStart();
				}
			}

			auto first_payload = media_desc->GetFirstPayload();
			if (first_payload == nullptr)
			{
				logte("Failed to get the first Payload type of peer sdp");
				return false;
			}

			// Make track
			auto track = std::make_shared<MediaTrack>();
			RtpDepacketizingManager::SupportedDepacketizerType depacketizer_type;

			track->SetId(interleaved_channel);
			track->SetTimeBase(1, first_payload->GetCodecRate());
			track->SetVideoTimestampScale(1.0);

			switch (first_payload->GetCodec())
			{
				case PayloadAttr::SupportCodec::H264:
					track->SetMediaType(cmn::MediaType::Video);
					track->SetCodecId(cmn::MediaCodecId::H264);
					track->SetOriginBitstream(cmn::BitstreamFormat::H264_RTP_RFC_6184);
					_h264_extradata_nalu = first_payload->GetH264ExtraDataAsAnnexB();
					depacketizer_type = RtpDepacketizingManager::SupportedDepacketizerType::H264;
					break;

				case PayloadAttr::SupportCodec::H265:
					track->SetMediaType(cmn::MediaType::Video);
					track->SetCodecId(cmn::MediaCodecId::H265);
					track->SetOriginBitstream(cmn::BitstreamFormat::H265_RTP_RFC_7798);
					// Get Extradata from the first payload(out of band)
					_h265_extradata_nalu = first_payload->GetH265ExtraDataAsAnnexB();
					depacketizer_type = RtpDepacketizingManager::SupportedDepacketizerType::H265;
					break;

					case PayloadAttr::SupportCodec::VP8:
					track->SetMediaType(cmn::MediaType::Video);
					track->SetCodecId(cmn::MediaCodecId::Vp8);
					track->SetOriginBitstream(cmn::BitstreamFormat::VP8_RTP_RFC_7741);
					depacketizer_type = RtpDepacketizingManager::SupportedDepacketizerType::VP8;
					break;

				case PayloadAttr::SupportCodec::MPEG4_GENERIC:
					track->SetMediaType(cmn::MediaType::Audio);
					track->SetCodecId(cmn::MediaCodecId::Aac);
					track->SetOriginBitstream(cmn::BitstreamFormat::AAC_MPEG4_GENERIC);
					track->GetChannel().SetCount(std::atoi(first_payload->GetCodecParams()));
					depacketizer_type = RtpDepacketizingManager::SupportedDepacketizerType::MPEG4_GENERIC_AUDIO;
					break;

				case PayloadAttr::SupportCodec::OPUS:
					track->SetMediaType(cmn::MediaType::Audio);
					track->SetCodecId(cmn::MediaCodecId::Opus);
					track->SetOriginBitstream(cmn::BitstreamFormat::OPUS_RTP_RFC_7587);
					track->GetChannel().SetCount(std::atoi(first_payload->GetCodecParams()));
					depacketizer_type = RtpDepacketizingManager::SupportedDepacketizerType::OPUS;
					break;

				default:
					logte("%s - Unsupported codec  : %s", GetName().CStr(), first_payload->GetCodecParams().CStr());
					continue;
			}

			// Add Depacketizer
			if (AddDepacketizer(interleaved_channel, depacketizer_type) == false)
			{
				logte("%s - Could not add depacketizer for channel %u codec  : %s", GetName().CStr(), interleaved_channel, first_payload->GetCodecParams().CStr());
				return false;
			}

			// Set Parameters
			if (depacketizer_type == RtpDepacketizingManager::SupportedDepacketizerType::MPEG4_GENERIC_AUDIO)
			{
				RtpDepacketizerMpeg4GenericAudio::Mode mpeg4_mode;
				if (first_payload->GetMpeg4GenericMode() == PayloadAttr::Mpeg4GenericMode::AAC_lbr)
				{
					mpeg4_mode = RtpDepacketizerMpeg4GenericAudio::Mode::AAC_lbr;
				}
				else if (first_payload->GetMpeg4GenericMode() == PayloadAttr::Mpeg4GenericMode::AAC_hbr)
				{
					mpeg4_mode = RtpDepacketizerMpeg4GenericAudio::Mode::AAC_hbr;
				}
				else
				{
					logte("%s - It is not supported MPEG4-GENERIC audio mode : %s", GetName().CStr(), first_payload->GetFmtp().CStr());
					return false;
				}

				auto mpeg4_size_length = first_payload->GetMpeg4GenericSizeLength();
				auto mpeg4_index_length = first_payload->GetMpeg4GenericIndexLength();
				auto mpeg4_index_delta_length = first_payload->GetMpeg4GenericIndexDeltaLength();
				auto mpeg4_config = first_payload->GetMpeg4GenericConfig();

				if (mpeg4_config == nullptr)
				{
					logte("%s - Could not parse MPEG4-GENERIC audio config : %s", GetName().CStr(), first_payload->GetFmtp().CStr());
					return false;
				}

				auto depacketizer = std::dynamic_pointer_cast<RtpDepacketizerMpeg4GenericAudio>(GetDepacketizer(interleaved_channel));
				if (depacketizer->SetConfigParams(mpeg4_mode, mpeg4_size_length, mpeg4_index_length, mpeg4_index_delta_length, mpeg4_config) == false)
				{
					logte("%s - Could not parse MPEG4-GENERIC audio config : %s", GetName().CStr(), first_payload->GetFmtp().CStr());
					return false;
				}
			}

			AddTrack(track);

			// Some RTSP servers ignore the ssrc of SETUP, so they use an interleaved channel instead.
			RtpRtcp::RtpTrackIdentifier rtp_track_id(track->GetId());
			rtp_track_id.interleaved_channel = interleaved_channel;

			_rtp_rtcp->AddRtpReceiver(track, rtp_track_id);
			RegisterRtpClock(track->GetId(), track->GetTimeBase().GetExpr());

			interleaved_channel += 2;
		}

		_rtp_rtcp->RegisterPrevNode(nullptr);
		_rtp_rtcp->RegisterNextNode(ov::Node::GetSharedPtr());
		_rtp_rtcp->Start();

		RegisterPrevNode(_rtp_rtcp);
		RegisterNextNode(nullptr);

		return true;
	}

	bool RtspcStream::RequestPlay()
	{
		if (GetState() != State::DESCRIBED)
		{
			return false;
		}

		auto play = std::make_shared<RtspMessage>(RtspMethod::PLAY, GetNextCSeq(), _curr_url->ToUrlString(true));
		if (_authorization_field != nullptr)
		{
			// If authorization method is Digest, update the method and uri
			if (_authorization_field->GetScheme() == RtspHeaderWWWAuthenticateField::Scheme::Digest)
			{
				_authorization_field->UpdateDigestAuth(play->GetMethodStr(), play->GetRequestUri());
			}

			play->AddHeaderField(_authorization_field);
		}

		if (_rtsp_session_id.IsEmpty() == false)
		{
			play->AddHeaderField(std::make_shared<RtspHeaderField>(RtspHeaderFieldType::Session, _rtsp_session_id));
		}
		play->AddHeaderField(std::make_shared<RtspHeaderField>(RtspHeaderFieldType::UserAgent, RTSP_USER_AGENT_NAME));

		if (SendRequestMessage(play) == false)
		{
			SetState(State::ERROR);
			logte("Could not request DESCIBE to RTSP server (%s)", _curr_url->ToUrlString().CStr());
			return false;
		}

		logti("Request PLAY : %s", play->DumpHeader().CStr());

		auto reply = ReceiveResponse(play->GetCSeq(), 3000);

		if (reply == nullptr)
		{
			SetState(State::ERROR);
			logte("No response(CSeq : %u) was received from the rtsp server(%s)", play->GetCSeq(), _curr_url->ToUrlString().CStr());
			return false;
		}
		else if (reply->GetStatusCode() != 200)
		{
			SetState(State::ERROR);
			logte("Rtsp server(%s) rejected the describe request : %d(%s)", _curr_url->ToUrlString().CStr(), reply->GetStatusCode(), reply->GetReasonPhrase().CStr());
			return false;
		}

		logti("Response PLAY : %s", reply->DumpHeader().CStr());

		SetState(State::PLAYING);

		_ping_timer.Start();

		return true;
	}

	bool RtspcStream::RequestStop()
	{
		if (GetState() != State::PLAYING)
		{
			return false;
		}

		auto teardown = std::make_shared<RtspMessage>(RtspMethod::TEARDOWN, GetNextCSeq(), _curr_url->ToUrlString(true));
		if (_authorization_field != nullptr)
		{
			// If authorization method is Digest, update the method and uri
			if (_authorization_field->GetScheme() == RtspHeaderWWWAuthenticateField::Scheme::Digest)
			{
				_authorization_field->UpdateDigestAuth(teardown->GetMethodStr(), teardown->GetRequestUri());
			}

			teardown->AddHeaderField(_authorization_field);
		}

		if (_rtsp_session_id.IsEmpty() == false)
		{
			teardown->AddHeaderField(std::make_shared<RtspHeaderField>(RtspHeaderFieldType::Session, _rtsp_session_id));
		}
		teardown->AddHeaderField(std::make_shared<RtspHeaderField>(RtspHeaderFieldType::UserAgent, RTSP_USER_AGENT_NAME));

		if (SendRequestMessage(teardown) == false)
		{
			SetState(State::ERROR);
			logte("Could not request Stop to RTSP server (%s)", _curr_url->ToUrlString().CStr());
			return false;
		}

		auto reply = ReceiveResponse(teardown->GetCSeq(), 3000);
		if (reply == nullptr)
		{
			SetState(State::ERROR);
			logte("No response(CSeq : %u) was received from the rtsp server(%s)", teardown->GetCSeq(), _curr_url->ToUrlString().CStr());
			return false;
		}
		else if (reply->GetStatusCode() != 200)
		{
			SetState(State::ERROR);
			logte("Rtsp server(%s) rejected the describe request : %d(%s)", _curr_url->ToUrlString().CStr(), reply->GetStatusCode(), reply->GetReasonPhrase().CStr());
			return false;
		}

		return true;
	}

	bool RtspcStream::Ping()
	{
		// GET_PARAMETER
		auto get_parameter = std::make_shared<RtspMessage>(RtspMethod::GET_PARAMETER, GetNextCSeq(), _curr_url->ToUrlString(true));
		if (_authorization_field != nullptr)
		{
			// If authorization method is Digest, update the method and uri
			if (_authorization_field->GetScheme() == RtspHeaderWWWAuthenticateField::Scheme::Digest)
			{
				_authorization_field->UpdateDigestAuth(get_parameter->GetMethodStr(), get_parameter->GetRequestUri());
			}

			get_parameter->AddHeaderField(_authorization_field);
		}

		if (_rtsp_session_id.IsEmpty() == false)
		{
			get_parameter->AddHeaderField(std::make_shared<RtspHeaderField>(RtspHeaderFieldType::Session, _rtsp_session_id));
		}

		if (SendRequestMessage(get_parameter, false) == false)
		{
			SetState(State::ERROR);
			logte("Could not request GET_PARAMETER to RTSP server (%s)", _curr_url->ToUrlString().CStr());
			return false;
		}

		logti("Request GET_PARAMETER : %s", get_parameter->DumpHeader().CStr());

		return true;
	}

	int32_t RtspcStream::GetNextCSeq()
	{
		return _cseq++;
	}

	bool RtspcStream::SubscribeResponse(const std::shared_ptr<RtspMessage> &request_message)
	{
		std::lock_guard<std::mutex> lock(_response_subscriptions_lock);
		_response_subscriptions.emplace(request_message->GetCSeq(), std::make_shared<ResponseSubscription>(request_message));
		return true;
	}

	std::shared_ptr<RtspcStream::ResponseSubscription> RtspcStream::PopResponseSubscription(uint32_t cseq)
	{
		std::lock_guard<std::mutex> lock(_response_subscriptions_lock);
		auto it = _response_subscriptions.find(cseq);
		if (it == _response_subscriptions.end())
		{
			logtt("There is no request message to receive a response. (CSeq : %u)", cseq);
			return nullptr;
		}

		auto request_response = it->second;

		// Remove waited request message
		_response_subscriptions.erase(it);

		return request_response;
	}

	bool RtspcStream::SendRequestMessage(const std::shared_ptr<RtspMessage> &message, bool wait_for_response)
	{
		if (wait_for_response == true)
		{
			// Add to RequestedMap to receive reply
			SubscribeResponse(message);
		}

		// Send
		return _signalling_socket->Send(message->GetMessage());
	}

	std::shared_ptr<RtspMessage> RtspcStream::ReceiveResponse(uint32_t cseq, uint64_t timeout_ms)
	{
		auto request_response = PopResponseSubscription(cseq);
		if (request_response == nullptr)
		{
			return nullptr;
		}

		// When the stream is playing, another thread receives a message and notifies it.
		if (GetState() == State::PLAYING)
		{
			return request_response->WaitForResponse(timeout_ms);
		}
		// Otherwise, the response must be received directly from the socket.
		else
		{
			auto reply = ReceiveMessage(timeout_ms);
			// If the stream is not in the playing state, the client cannot receive unexpected CSeq.
			if (reply == nullptr)
			{
				// timed out
				return nullptr;
			}
			else if (reply->GetCSeq() != cseq)
			{
				logte("Unexpected CSeq : %u (expected : %u)", reply->GetCSeq(), cseq);
				return nullptr;
			}

			return reply;
		}

		return nullptr;
	}

	std::shared_ptr<RtspMessage> RtspcStream::ReceiveMessage(int64_t timeout_msec)
	{
		ov::StopWatch stop_watch;

		stop_watch.Start();
		while (true)
		{
			if (ReceivePacket(false, timeout_msec) == false)
			{
				return nullptr;
			}

			if (_rtsp_demuxer.IsAvailableMessage())
			{
				return _rtsp_demuxer.PopMessage();
			}

			if (stop_watch.IsElapsed(timeout_msec))
			{
				return nullptr;
			}
		}

		return nullptr;
	}

	bool RtspcStream::ReceivePacket(bool non_block, int64_t timeout_msec)
	{
		uint8_t buffer[65535];
		size_t read_bytes = 0ULL;

		if (non_block == false && timeout_msec != 0)
		{
			struct timeval tv = {timeout_msec / 1000, (timeout_msec % 1000) * 1000};
			_signalling_socket->SetRecvTimeout(tv);
		}

		auto error = _signalling_socket->Recv(buffer, 65535, &read_bytes, non_block);
		if (read_bytes == 0)
		{
			if (error != nullptr)
			{
				logte("[%s/%s] An error occurred while receiving packet: %s", GetApplicationName(), GetName().CStr(), error->What());
				SetState(State::ERROR);
				return false;
			}
			else
			{
				if (non_block == true)
				{
					// retry later
					return true;
				}
				else
				{
					// timeout
					return false;
				}
			}
		}

		// Since the response to the Play request and part of the interleaved data can be received at once,
		// use _rtsp_demuxer to prevent the packet from being missed, regardless of the current state.
		if (_rtsp_demuxer.AppendPacket(buffer, read_bytes) == false)
		{
			logte("[%s/%s] An error occurred while parsing packet: Invalid packet", GetApplicationName(), GetName().CStr());
			return false;
		}

		return true;
	}

	int RtspcStream::GetFileDescriptorForDetectingEvent()
	{
		return _signalling_socket->GetSocket().GetNativeHandle();
	}

	PullStream::ProcessMediaResult RtspcStream::ProcessMediaPacket()
	{
		// Ping
		if (_ping_timer.IsElapsed((_rtsp_session_timeout_sec / 2) * 1000))
		{
			_ping_timer.Update();
			Ping();
		}

		// Receive Packet
		auto result = ReceivePacket(true);
		if (result == false)
		{
			logte("%s/%s(%u) - Could not receive packet : err(%d)", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), static_cast<uint8_t>(result));
			SetState(State::ERROR);
			return ProcessMediaResult::PROCESS_MEDIA_FAILURE;
		}

		while (true)
		{
			if (_rtsp_demuxer.IsAvailableMessage())
			{
				auto rtsp_message = _rtsp_demuxer.PopMessage();
				if (rtsp_message->GetMessageType() == RtspMessageType::RESPONSE)
				{
					// Find Request
					auto subscription = PopResponseSubscription(rtsp_message->GetCSeq());
					if (subscription == nullptr)
					{
						// Non subscription message, ignore
						logti("Received Message : %s", rtsp_message->DumpHeader().CStr());
						continue;
					}

					subscription->OnResponseReceived(rtsp_message);
				}
				else if (rtsp_message->GetMessageType() == RtspMessageType::REQUEST)
				{
					//TODO(Getroot): What kind of request message will be received?
					logti("%s", rtsp_message->DumpHeader().CStr());
				}
				else
				{
					// Error
					logte("%s/%s(%u) - Unknown rtsp message received", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId());
					SetState(State::ERROR);
					return ProcessMediaResult::PROCESS_MEDIA_FAILURE;
				}
			}
			else if (_rtsp_demuxer.IsAvailableData())
			{
				// In an interleaved session, the server sends both messages and data in the same session.
				// Check if there are available messages and interleaved data

				// RTP or RTCP
				auto rtsp_data = _rtsp_demuxer.PopData();

				// RtpRtcpInterface(RtspcStream) <--> [RTP_RTCP Node] <--> [*Edge Node(RtspcStream)] ---Send--> {Socket}
				//							        					     					    <--Recv--- {Socket}
				SendDataToPrevNode(rtsp_data);
			}
			else
			{
				return ProcessMediaResult::PROCESS_MEDIA_TRY_AGAIN;
			}
		}

		return ProcessMediaResult::PROCESS_MEDIA_SUCCESS;
	}

	// From RtpRtcp node
	void RtspcStream::OnRtpFrameReceived(const std::vector<std::shared_ptr<RtpPacket>> &rtp_packets)
	{
		auto first_rtp_packet = rtp_packets.front();
		auto channel = first_rtp_packet->GetRtspChannel();
		logtt("%s", first_rtp_packet->Dump().CStr());

		auto track = GetTrack(channel);
		if (track == nullptr)
		{
			logte("%s - Could not find track : channel_id(%u)", GetName().CStr(), channel);
			return;
		}

		auto depacketizer = GetDepacketizer(channel);
		if (depacketizer == nullptr)
		{
			logte("%s - Could not find depacketizer : channel_id(%u)", GetName().CStr(), channel);
			return;
		}

		std::vector<std::shared_ptr<ov::Data>> payload_list;
		for (const auto &packet : rtp_packets)
		{
			auto payload = std::make_shared<ov::Data>(packet->Payload(), packet->PayloadSize());
			payload_list.push_back(payload);
		}

		auto bitstream = depacketizer->ParseAndAssembleFrame(payload_list);
		if (bitstream == nullptr)
		{
			logte("%s - Could not depacketize packet : channel_id(%u)", GetName().CStr(), channel);
			return;
		}

		cmn::BitstreamFormat bitstream_format;
		cmn::PacketType packet_type;

		switch (track->GetCodecId())
		{
			case cmn::MediaCodecId::H265:
				// Our H265 depacketizer always converts packet to H265
				bitstream_format = cmn::BitstreamFormat::H265_ANNEXB;
				packet_type = cmn::PacketType::NALU;
				break;
			case cmn::MediaCodecId::H264:
				// Our H264 depacketizer always converts packet to AnnexB
				bitstream_format = cmn::BitstreamFormat::H264_ANNEXB;
				packet_type = cmn::PacketType::NALU;
				break;

			case cmn::MediaCodecId::Opus:
				bitstream_format = cmn::BitstreamFormat::OPUS;
				packet_type = cmn::PacketType::RAW;
				break;

			// Our AAC depacketizer always converts packet to ADTS
			case cmn::MediaCodecId::Aac:
				bitstream_format = cmn::BitstreamFormat::AAC_ADTS;
				packet_type = cmn::PacketType::RAW;
				break;

			case cmn::MediaCodecId::Vp8:
				bitstream_format = cmn::BitstreamFormat::VP8;
				packet_type = cmn::PacketType::RAW;
				break;

			// It can't be reached here because it has already failed in GetDepacketizer.
			default:
				return;
		}

		int64_t adjusted_timestamp;
		if (AdjustRtpTimestamp(channel, first_rtp_packet->Timestamp(), std::numeric_limits<uint32_t>::max(), adjusted_timestamp) == false)
		{
			logtt("not yet received sr packet : %u", first_rtp_packet->Ssrc());
			// Prevents the stream from being deleted because there is no input data
			MonitorInstance->IncreaseBytesIn(*Stream::GetSharedPtr(), bitstream->GetLength());
			return;
		}
		

		logtt("Channel(%d) Payload Type(%d) Ssrc(%u) Timestamp(%u) PTS(%lld) Time scale(%f) Adjust Timestamp(%f)",
			  channel, first_rtp_packet->PayloadType(), first_rtp_packet->Ssrc(), first_rtp_packet->Timestamp(), adjusted_timestamp, track->GetTimeBase().GetExpr(), static_cast<double>(adjusted_timestamp) * track->GetTimeBase().GetExpr());

		auto frame = std::make_shared<MediaPacket>(GetMsid(),
												   track->GetMediaType(),
												   track->GetId(),
												   bitstream,
												   adjusted_timestamp,
												   adjusted_timestamp,
												   -1LL,
												   MediaPacketFlag::Unknown,
												   bitstream_format,
												   packet_type);

		logtt("Send Frame : track_id(%d) codec_id(%d) bitstream_format(%d) packet_type(%d) data_length(%d) pts(%u)", track->GetId(), track->GetCodecId(), bitstream_format, packet_type, bitstream->GetLength(), first_rtp_packet->Timestamp());

		// Send SPS/PPS if stream is H264
		if (_sent_sequence_header == false && track->GetCodecId() == cmn::MediaCodecId::H264 && _h264_extradata_nalu != nullptr)
		{
			auto media_packet = std::make_shared<MediaPacket>(GetMsid(),
															  track->GetMediaType(),
															  track->GetId(),
															  _h264_extradata_nalu,
															  adjusted_timestamp,
															  adjusted_timestamp,
															  -1LL,
															  MediaPacketFlag::Unknown,
															  cmn::BitstreamFormat::H264_ANNEXB,
															  cmn::PacketType::NALU);
			SendFrame(media_packet);
			_sent_sequence_header = true;
		}
		// Send VPS/SPS/PPS if stream is H265
		else if (_sent_sequence_header == false && track->GetCodecId() == cmn::MediaCodecId::H265 && _h265_extradata_nalu != nullptr)
		{
			auto media_packet = std::make_shared<MediaPacket>(GetMsid(),
															  track->GetMediaType(),
															  track->GetId(),
															  _h265_extradata_nalu,
															  adjusted_timestamp,
															  adjusted_timestamp,
															  -1LL,
															  MediaPacketFlag::Unknown,
															  cmn::BitstreamFormat::H265_ANNEXB,
															  cmn::PacketType::NALU);
			SendFrame(media_packet);
			_sent_sequence_header = true;
		}

		SendFrame(frame);
	}

	// From RtpRtcp node
	void RtspcStream::OnRtcpReceived(const std::shared_ptr<RtcpInfo> &rtcp_info)
	{
		// RTCP Channel is RTP Channel + 1
		auto channel = rtcp_info->GetRtspChannel() - 1;
		// Receive Sender Report
		if (rtcp_info->GetPacketType() == RtcpPacketType::SR)
		{
			auto sr = std::dynamic_pointer_cast<SenderReport>(rtcp_info);
			UpdateSenderReportTimestamp(channel, sr->GetMsw(), sr->GetLsw(), sr->GetTimestamp());
		}
	}

	ov::String RtspcStream::GenerateControlUrl(ov::String control)
	{
		ov::String prefix = "rtsp://";

		// If contorl is absolute URL, the use it as it is
		if (control.Left(prefix.GetLength()).UpperCaseString() == prefix.UpperCaseString())
		{
			return control;
		}

		// Check content_base
		if (_content_base.IsEmpty() == false)
		{
			return ov::String::FormatString("%s%s", _content_base.CStr(), control.CStr());
		}

		ov::String control_url;
		control_url = ov::String::FormatString("%s/%s", _curr_url->ToUrlString(false).CStr(), control.CStr());
		if (_curr_url->HasQueryString())
		{
			control_url.AppendFormat("?%s", _curr_url->Query().CStr());
		}

		return control_url;
	}

	// ov::Node Interface
	// RtpRtcp <-> Edge(this)
	bool RtspcStream::OnDataReceivedFromPrevNode(NodeType from_node, const std::shared_ptr<ov::Data> &data)
	{
		if (ov::Node::GetNodeState() != ov::Node::NodeState::Started)
		{
			logtt("Node has not started, so the received data has been canceled.");
			return false;
		}

		// Make RTSP interleaved data
		if (from_node == NodeType::Rtcp)
		{
			uint8_t channel_id = 0;

			auto rtcp_packet = _rtp_rtcp->GetLastSentRtcpPacket();
			auto rtcp_info = rtcp_packet->GetRtcpInfo();

			auto rtp_ssrc = rtcp_info->GetRtpSsrc();
			channel_id = _ssrc_channel_id_map[rtp_ssrc] + 1;  // RTCP Channel ID is rtp channel id + 1

			auto channel_data = std::make_shared<ov::Data>();
			// $ + 1 bytes channel id + length + payload
			// 4 + payload length
			channel_data->SetLength(4);
			auto ptr = channel_data->GetWritableDataAs<uint8_t>();

			ptr[0] = '$';
			ptr[1] = channel_id;
			ByteWriter<uint16_t>::WriteBigEndian(&ptr[2], data->GetLength());
			channel_data->Append(data);

			return _signalling_socket->Send(channel_data);
		}
		else
		{
			// RtspcStream doen't send rtp packet
			return false;
		}

		return false;
	}

	// RtspcStream Node has not a lower node so it will not be called
	bool RtspcStream::OnDataReceivedFromNextNode(NodeType from_node, const std::shared_ptr<const ov::Data> &data)
	{
		return true;
	}
}  // namespace pvd
