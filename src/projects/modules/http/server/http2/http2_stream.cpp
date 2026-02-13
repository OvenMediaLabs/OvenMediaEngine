//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#include "http2_stream.h"

#include "../http_connection.h"
#include "../http_server_private.h"

namespace http
{
	namespace svr
	{
		namespace h2
		{
			HttpStream::HttpStream(const std::shared_ptr<HttpConnection> &connection, uint32_t stream_id)
				: HttpExchange(connection), _stream_id(stream_id)
			{
				_request = std::make_shared<Http2Request>(GetConnection()->GetSocket(), GetConnection()->GetHpackDecoder());
				_request->SetConnectionType(ConnectionType::Http20);
				_request->SetTlsData(GetConnection()->GetTlsData());

				_response = std::make_shared<Http2Response>(stream_id, GetConnection()->GetSocket(), GetConnection()->GetHpackEncoder());
				_response->SetTlsData(GetConnection()->GetTlsData());
				_response->SetHeader("server", "OvenMediaEngine");
				_response->SetHeader("content-type", "text/html");

				// https://www.rfc-editor.org/rfc/rfc7540.html#section-5.1.1
				// A stream identifier of zero (0x0) is used for connection control messages
				if (_stream_id == 0)
				{
					SendInitialControlMessage();
				}
			}

			std::shared_ptr<HttpRequest> HttpStream::GetRequest() const
			{
				return _request;
			}
			std::shared_ptr<HttpResponse> HttpStream::GetResponse() const
			{
				return _response;
			}

			// Get Stream ID
			uint32_t HttpStream::GetStreamId() const
			{
				return _stream_id;
			}

			bool HttpStream::OnEndHeaders()
			{
				// Header Completed
				if (_request->AppendHeaderData(_header_block) <= 0)
				{
					return false;
				}

				if (IsWebSocketUpgradeRequest() == true)
				{
					if (AcceptWebSocketUpgrade() == false)
					{
						SetStatus(Status::Error);
						return -1;
					}

					SetStatus(Status::Upgrade);
					return true;
				}

				// HTTP/2 Connection is awalys keep-alive
				SetKeepAlive(true);

				// Notify to interceptor
				OnRequestPrepared();

				if (_headers_frame->IS_HTTP2_FRAME_FLAG_ON(Http2HeadersFrame::Flags::EndStream))
				{
					return OnEndStream();
				}
				else
				{
					// Continue to receive more data
					SetStatus(Status::Exchanging);
				}

				return true;
			}

			bool HttpStream::OnEndStream()
			{
				// End of Stream, that means no more data will be sent
				auto result = OnRequestCompleted();
				switch (result)
				{
					case InterceptorResult::Completed:
						SetStatus(Status::Completed);
						break;
					case InterceptorResult::Moved:
						SetStatus(Status::Moved);
						break;
					case InterceptorResult::NotFound:
						SetStatus(Status::Completed);
						break;
					case InterceptorResult::Error:
					default:
						SetStatus(Status::Error);
						return false;
				}

				return true;
			}

			bool HttpStream::SendInitialControlMessage()
			{
				logtt("Send Initial Control Message");

				// Settings Frame
				auto settings_frame = std::make_shared<Http2SettingsFrame>();
				// Max decoder table size, encoder(client) will use this value for encoder and notify by DecodeDynamicTableSizeUpdate in HPACK
				settings_frame->SetParameter(Http2SettingsFrame::Parameters::HeaderTableSize, MAX_HEADER_TABLE_SIZE); 
				settings_frame->SetParameter(Http2SettingsFrame::Parameters::MaxConcurrentStreams, 100);
				settings_frame->SetParameter(Http2SettingsFrame::Parameters::InitialWindowSize, 6291456);
				settings_frame->SetParameter(Http2SettingsFrame::Parameters::MaxHeaderListSize, 262144);

				auto result = _response->Send(settings_frame);

				// WindowUpdate Frame
				auto window_update_frame = std::make_shared<Http2WindowUpdateFrame>(0);
				window_update_frame->SetWindowSizeIncrement(6291456);

				result = result ? _response->Send(window_update_frame) : false;
				
				return result;
			}

			#define PARSE_HTTP2_FRAME_AS(NAME, PARSED_FRAME, ORIGIN_FRAME, TARGET_FRAME_TYPE) \
				PARSED_FRAME = ORIGIN_FRAME->GetFrameAs<TARGET_FRAME_TYPE>();\
				if (PARSED_FRAME == nullptr)\
				{\
					logte("Failed to parse" NAME "frame : %s", frame->ToString().CStr());\
					return false;\
				}

			bool HttpStream::OnFrameReceived(const std::shared_ptr<Http2Frame> &frame)
			{
				std::shared_ptr<const Http2Frame> parsed_frame = frame;
				bool result = false;
				switch (frame->GetType())
				{
					case Http2Frame::Type::Data:
					{
						PARSE_HTTP2_FRAME_AS("Data", parsed_frame, frame, Http2DataFrame)
						result = OnDataFrameReceived(std::static_pointer_cast<const Http2DataFrame>(parsed_frame));
						break;
					}
					case Http2Frame::Type::Headers:
					{
						PARSE_HTTP2_FRAME_AS("Headers", parsed_frame, frame, Http2HeadersFrame)
						result = OnHeadersFrameReceived(std::static_pointer_cast<const Http2HeadersFrame>(parsed_frame));
						break;
					}
					case Http2Frame::Type::Priority:
					{
						PARSE_HTTP2_FRAME_AS("Priority", parsed_frame, frame, Http2PriorityFrame)
						result = OnPriorityFrameReceived(std::static_pointer_cast<const Http2PriorityFrame>(parsed_frame));
						break;
					}
					case Http2Frame::Type::RstStream:
					{
						PARSE_HTTP2_FRAME_AS("RstStream", parsed_frame, frame, Http2RstStreamFrame)
						result = OnRstStreamFrameReceived(std::static_pointer_cast<const Http2RstStreamFrame>(parsed_frame));
						break;
					}
					case Http2Frame::Type::Settings:
					{
						PARSE_HTTP2_FRAME_AS("Settings", parsed_frame, frame, Http2SettingsFrame)
						result = OnSettingsFrameReceived(std::static_pointer_cast<const Http2SettingsFrame>(parsed_frame));
						break;
					}
					case Http2Frame::Type::PushPromise:
					{
						// No need to parse server side
						break;
					}
					case Http2Frame::Type::Ping:
					{
						PARSE_HTTP2_FRAME_AS("Ping", parsed_frame, frame, Http2PingFrame)
						result = OnPingFrameReceived(std::static_pointer_cast<const Http2PingFrame>(parsed_frame));
						break;
					}
					case Http2Frame::Type::GoAway:
					{
						PARSE_HTTP2_FRAME_AS("GoAway", parsed_frame, frame, Http2GoAwayFrame)
						result = OnGoAwayFrameReceived(std::static_pointer_cast<const Http2GoAwayFrame>(parsed_frame));
						break;
					}
					case Http2Frame::Type::WindowUpdate:
					{
						PARSE_HTTP2_FRAME_AS("WindowUpdate", parsed_frame, frame, Http2WindowUpdateFrame)
						result = OnWindowUpdateFrameReceived(std::static_pointer_cast<const Http2WindowUpdateFrame>(parsed_frame));
						break;
					}
					case Http2Frame::Type::Continuation:
					{
						PARSE_HTTP2_FRAME_AS("Continuation", parsed_frame, frame, Http2ContinuationFrame)
						result = OnContinuationFrameReceived(std::static_pointer_cast<const Http2ContinuationFrame>(parsed_frame));
						break;
					}
					case Http2Frame::Type::Unknown:
					default:
						logte("Unknown frame type received");
						return false;
				}

				logtt("Frame Processing %s : %s", result?"Completed":"Error", parsed_frame->ToString().CStr());

				return true;
			}

			bool HttpStream::OnHeadersFrameReceived(const std::shared_ptr<const Http2HeadersFrame> &frame)
			{
				if (_header_block == nullptr)
				{
					_header_block = std::make_shared<ov::Data>();
				}

				_header_block->Append(frame->GetHeaderBlockFragment());
				_headers_frame = frame;

				if (frame->IS_HTTP2_FRAME_FLAG_ON(Http2HeadersFrame::Flags::EndHeaders))
				{
					return OnEndHeaders();
				}

				return true;
			}

			// Data frame received
			bool HttpStream::OnDataFrameReceived(const std::shared_ptr<const Http2DataFrame> &frame)
			{
				OnDataReceived(frame->GetData());
				if (frame->IS_HTTP2_FRAME_FLAG_ON(Http2DataFrame::Flags::EndStream))
				{
					return OnEndStream();
				}

				return true;
			}

			bool HttpStream::OnPriorityFrameReceived(const std::shared_ptr<const Http2PriorityFrame> &frame)
			{
				// Nothing to do yet, just ignore
				return true;
			}

			bool HttpStream::OnRstStreamFrameReceived(const std::shared_ptr<const Http2RstStreamFrame> &frame)
			{
				logtt("%s", frame->ToString().CStr());
				SetStatus(HttpExchange::Status::Error);
				return true;
			}

			bool HttpStream::OnSettingsFrameReceived(const std::shared_ptr<const Http2SettingsFrame> &frame)
			{
				if (frame->IsAck() == false)
				{
					// Apply SETTINGS_HEADER_TABLE_SIZE to HPACK encoder
					auto [exist, size] = frame->GetParameter(Http2SettingsFrame::Parameters::HeaderTableSize);
					if (exist)
					{
						auto hpack_encoder = GetConnection()->GetHpackEncoder();
						hpack_encoder->UpdateDynamicTableSize(std::min(size, MAX_HEADER_TABLE_SIZE));
					}
					
					// Settings Frame
					auto settings_frame = std::make_shared<Http2SettingsFrame>();
					settings_frame->SetAck();
					return _response->Send(settings_frame);
				}

				return true;
			}

			bool HttpStream::OnPushPromiseFrameReceived(const std::shared_ptr<const Http2PushPromiseFrame> &frame)
			{
				// Server should not receive PushPromise frame, just ignore
				return true;
			}

			bool HttpStream::OnPingFrameReceived(const std::shared_ptr<const Http2PingFrame> &frame)
			{
				if (frame->IsAck() == false)
				{
					// PING Frame
					auto ping_frame = std::make_shared<Http2PingFrame>(frame->GetStreamId());
					ping_frame->SetAck();
					ping_frame->SetOpaqueData(frame->GetOpaqueData());
					return _response->Send(ping_frame);
				}

				return true;
			}

			bool HttpStream::OnWindowUpdateFrameReceived(const std::shared_ptr<const Http2WindowUpdateFrame> &frame)
			{
				// Nothing to do yet, just ignore
				return true;
			}

			bool HttpStream::OnGoAwayFrameReceived(const std::shared_ptr<const Http2GoAwayFrame> &frame)
			{
				logtt("%s", frame->ToString().CStr());
				SetStatus(HttpExchange::Status::Error);
				return true;
			}

			bool HttpStream::OnContinuationFrameReceived(const std::shared_ptr<const Http2ContinuationFrame> &frame)
			{
				if (_header_block == nullptr)
				{
					// It must be created by headers frame
					return false;
				}

				_header_block->Append(frame->GetHeaderBlockFragment());

				if (frame->IS_HTTP2_FRAME_FLAG_ON(Http2ContinuationFrame::Flags::EndHeaders))
				{
					return OnEndHeaders();
				}

				return true;
			}
		}  // namespace h2
	}	   // namespace svr
}  // namespace http
