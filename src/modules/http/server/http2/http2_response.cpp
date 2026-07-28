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
				// All streams on this connection share one HPACK encoder. Encoding this
				// block and putting its frames on the wire must not be split by another
				// stream, otherwise the peer replays the table updates in a different
				// order and resolves our indexes to the wrong entries.
				ov::LockGuard<ov::Mutex> block_lock(_hpack_encoder->GetHeaderBlockLock());

				std::shared_ptr<ov::Data> header_block = std::make_shared<ov::Data>(65535);

				// :status header field is must on top
				auto header_field = _hpack_encoder->Encode({":status", ov::Converter::ToString(static_cast<uint16_t>(GetStatusCode()))}, hpack::Encoder::EncodingType::LiteralWithIndexing);
				header_block->Append(header_field);

				for (const auto &[name, values] : GetResponseHeaderList())
				{
					for (const auto &value : values)
					{
						// https://httpwg.org/http2-spec/draft-ietf-httpbis-http2bis.html#section-8.2
						// Field names MUST be converted to lowercase when constructing an HTTP/2 message.
						auto name_lower = name.LowerCaseString();

						auto header_field = _hpack_encoder->Encode({name_lower, value}, hpack::Encoder::GetEncodingTypeOf(name_lower));
						header_block->Append(header_field);
					}
				}

				logtt("[Http2Response] Send header block : size(%zu)", header_block->GetLength());

				auto block_size = header_block->GetLength();
				auto fragment_size = std::min(block_size, static_cast<size_t>(MAX_HTTP2_HEADER_SIZE));

				// Headers frame
				auto headers_frame = std::make_shared<prot::h2::Http2HeadersFrame>(_stream_id);
				headers_frame->SetHeaderBlockFragment(header_block->Subdata(0, fragment_size));

				if (fragment_size == block_size)
				{
					headers_frame->SetEndHeaders();
				}

				if (_keep_stream == false && GetResponseDataSize() == 0)
				{
					headers_frame->SetEndStream();
				}

				// RFC 7540 §6.2/§6.10: no other frame may appear between HEADERS and its
				// CONTINUATION frames, so the whole sequence goes out as a single write.
				// ToData() returns a freshly allocated buffer, so appending to it is safe.
				auto wire_data = headers_frame->ToData();

				auto offset = fragment_size;
				while (offset < block_size)
				{
					fragment_size = std::min(block_size - offset, static_cast<size_t>(MAX_HTTP2_HEADER_SIZE));

					auto continuation_frame = std::make_shared<prot::h2::Http2ContinuationFrame>(_stream_id);
					continuation_frame->SetHeaderBlockFragment(header_block->Subdata(offset, fragment_size));

					offset += fragment_size;
					if (offset == block_size)
					{
						continuation_frame->SetEndHeaders();
					}

					wire_data->Append(continuation_frame->ToData());
				}

				if (HttpResponse::Send(wire_data) == false)
				{
					return -1;
				}

				return block_size;
			}

			int32_t Http2Response::SendPayload()
			{
				logtt("Trying to send datas...");

				uint32_t sent_bytes = 0;

				auto response_data_list = GetResponseDataList();
				for (size_t i = 0; i < response_data_list.size(); ++i)
				{
					const auto &data = response_data_list[i];
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
					if (_keep_stream == false && (i == response_data_list.size() - 1))
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