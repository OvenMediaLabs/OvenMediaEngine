//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#include "http2_response.h"
#include "../http_server_private.h"

namespace http
{
	namespace svr
	{
		namespace h2
		{
			// Constructor
			Http2Response::Http2Response(uint32_t stream_id, const std::shared_ptr<ov::ClientSocket> &client_socket, const std::shared_ptr<hpack::Encoder> &hpack_encoder)
				: HttpResponse(client_socket)
			{
				_stream_id = stream_id;
				_hpack_encoder = hpack_encoder;
			}

			bool Http2Response::Send(const std::shared_ptr<prot::h2::Http2Frame> &frame)
			{
				return HttpResponse::Send(frame->ToData());
			}

			void Http2Response::SetKeepStream(bool keep_stream)
			{
				_keep_stream = keep_stream;
			}
			
			bool Http2Response::Send(const std::shared_ptr<prot::h2::Http2DataFrame> &data_frame, bool end_stream)
			{
				auto frame = std::make_shared<prot::h2::Http2DataFrame>(_stream_id);
				frame->SetData(data_frame->GetData());
				if (end_stream == true)
				{
					frame->SetEndStream();
				}

				return Send(frame);
			}

			int32_t Http2Response::SendHeader()
			{
				std::shared_ptr<ov::Data> header_block = std::make_shared<ov::Data>(65535);
				size_t sent_size = 0;

				// :status header field is must on top
				auto header_field = _hpack_encoder->Encode({":status", ov::Converter::ToString(static_cast<uint16_t>(GetStatusCode()))}, hpack::Encoder::EncodingType::LiteralWithIndexing);
				header_block->Append(header_field);

				for (const auto &[name, values] : GetResponseHeaderList())
				{
					for (const auto &value : values)
					{
						// https://httpwg.org/http2-spec/draft-ietf-httpbis-http2bis.html#section-8.2
						// Field names MUST be converted to lowercase when constructing an HTTP/2 message.
						auto header_field = _hpack_encoder->Encode({name.LowerCaseString(), value}, hpack::Encoder::EncodingType::LiteralWithIndexing);
						header_block->Append(header_field);
					}
				}

				logtt("[Http2Response] Send header block : size(%u)", header_block->GetLength());

				std::shared_ptr<ov::Data> head_block_fragment;
				bool fragmented = false;
				
				if (header_block->GetLength() > MAX_HTTP2_HEADER_SIZE)
				{
					head_block_fragment = header_block->Subdata(0, MAX_HTTP2_HEADER_SIZE);
					fragmented = true;
				}
				else
				{
					head_block_fragment = header_block;
					fragmented = false;
				}
				
				// Send Headers frame
				auto headers_frame = std::make_shared<prot::h2::Http2HeadersFrame>(_stream_id);
				headers_frame->SetHeaderBlockFragment(head_block_fragment);

				// Set flags
				if (fragmented == false)
				{
					headers_frame->SetEndHeaders();
				}

				if (_keep_stream == false && GetResponseDataSize() == 0)
				{
					headers_frame->SetEndStream();
				}

				if (Send(headers_frame) == false)
				{
					return -1;
				}

				sent_size += head_block_fragment->GetLength();

				// Send Continuation frames if header block is fragmented
				if (fragmented == true)
				{
					auto remaining_size = header_block->GetLength() - MAX_HTTP2_HEADER_SIZE;
					auto remaining_data = header_block->Subdata(MAX_HTTP2_HEADER_SIZE, remaining_size);

					while (remaining_size > 0)
					{
						auto fragment_size = remaining_size > MAX_HTTP2_HEADER_SIZE ? MAX_HTTP2_HEADER_SIZE : remaining_size;
						auto fragment_data = remaining_data->Subdata(0, fragment_size);

						auto continuation_frame = std::make_shared<prot::h2::Http2ContinuationFrame>(_stream_id);
						continuation_frame->SetHeaderBlockFragment(fragment_data);

						if (remaining_size - fragment_size == 0)
						{
							continuation_frame->SetEndHeaders();
						}

						if (Send(continuation_frame) == false)
						{
							return -1;
						}

						sent_size += fragment_size;
						remaining_size -= fragment_size;
						remaining_data = remaining_data->Subdata(fragment_size, remaining_size);
					}
				}

				return sent_size;
			}

			int32_t Http2Response::SendPayload()
			{
				logtt("Trying to send datas...");

				uint32_t sent_bytes = 0;

				for (const auto &data : GetResponseDataList())
				{
					size_t offset = 0;
					auto data_fragment = data;
					while (offset + MAX_HTTP2_DATA_SIZE < data->GetLength())
					{
						data_fragment = data->Subdata(offset, MAX_HTTP2_DATA_SIZE);

						auto payload_frame = std::make_shared<prot::h2::Http2DataFrame>(_stream_id);
						payload_frame->SetData(data_fragment);

						if (Send(payload_frame) == false)
						{
							logte("Failed to send payload");
							ResetResponseData();
							return -1;
						}

						offset += MAX_HTTP2_DATA_SIZE;
					}

					// Last fragment
					auto payload_frame = std::make_shared<prot::h2::Http2DataFrame>(_stream_id);
					data_fragment = data->Subdata(offset);
					payload_frame->SetData(data_fragment);

					// End Stream
					if (_keep_stream == false && (&data == &GetResponseDataList().back()))
					{
						payload_frame->SetEndStream();
					}

					if (Send(payload_frame) == false)
					{
						logte("Failed to send payload");
						ResetResponseData();
						return -1;
					}

					sent_bytes += data->GetLength();
				}

				ResetResponseData();

				logtt("All datas are sent...");

				return sent_bytes;
			}

		} // namespace h2
	} // namespace svr
} // namespace http