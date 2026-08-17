//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <base/ovlibrary/byte_io.h>
#include "../http2_frame.h"

namespace http
{
	namespace prot
	{
		namespace h2
		{
			class Http2SettingsFrame : public Http2Frame
			{
			public:
				enum class Flags : uint8_t
				{
					None = 0x00,
					Ack = 0x01,
				};

				enum class Parameters : uint16_t
				{
					HeaderTableSize = 0x1,
					EnablePush = 0x2,
					MaxConcurrentStreams = 0x3,
					InitialWindowSize = 0x4,
					MaxFrameSize = 0x5,
					MaxHeaderListSize = 0x6,
				};

				// Make by itself
				Http2SettingsFrame()
					// https://datatracker.ietf.org/doc/html/rfc7540#section-6.5
					// SETTINGS frames always apply to a connection, never a single stream. 
					// The stream identifier for a SETTINGS frame MUST be zero (0x00).
					: Http2Frame(0)
				{
					SetType(Http2Frame::Type::Settings);
					SetFlags(0);
				}

				Http2SettingsFrame(const std::shared_ptr<Http2Frame> &frame)
					: Http2Frame(frame)
				{
				}

				// To String
				ov::String ToString() const override
				{
					ov::String str;
					
					str = Http2Frame::ToString();

					str += "\n";
					str += "[Settings Frame]\n";

					// Print Flags
					str += ov::String::FormatString("Flags: Ack(%s)\n", 
						ov::Converter::ToString(CHECK_HTTP2_FRAME_FLAG(Flags::Ack)).CStr());

					// Print parameters
					for (const auto &param : _parameters)
					{
						str += ov::String::FormatString("%s: %u\n", GetStringFromSettingsParamId(param.first).CStr(), param.second);
					}

					return str;
				}

				void SetAck()
				{
					TURN_ON_HTTP2_FRAME_FLAG(Flags::Ack);
				}

				bool IsAck() const
				{
					return CHECK_HTTP2_FRAME_FLAG(Flags::Ack);
				}

				void SetParameter(Parameters parameter, uint32_t value)
				{
					_parameters[static_cast<uint16_t>(parameter)] = value;
				}

				// Get Parameter
				// Returns {exist, value}
				std::tuple<bool, uint32_t> GetParameter(Parameters parameter) const
				{
					auto itr = _parameters.find(static_cast<uint16_t>(parameter));
					if (itr == _parameters.end())
					{
						return {false, 0};
					}

					return {true, itr->second};
				}

				// To data
				const std::shared_ptr<const ov::Data> GetPayload() const override
				{
					if (Http2Frame::GetPayload() != nullptr)
					{
						return Http2Frame::GetPayload();
					}

					if (_parameters.size() == 0)
					{
						return nullptr;
					}

					auto payload = std::make_shared<ov::Data>();
					ov::ByteStream stream(payload.get()); 

					for (auto &parameter : _parameters)
					{
						stream.WriteBE16(parameter.first);
						stream.WriteBE32(parameter.second);
					}

					return payload;
				}

				// Get string from parameter
				static const ov::String GetStringFromSettingsParamId(uint16_t param)
				{
					switch (param)
					{
						case static_cast<uint16_t>(Parameters::HeaderTableSize):
							return "HeaderTableSize";
						case static_cast<uint16_t>(Parameters::EnablePush):
							return "EnablePush";
						case static_cast<uint16_t>(Parameters::MaxConcurrentStreams):
							return "MaxConcurrentStreams";
						case static_cast<uint16_t>(Parameters::InitialWindowSize):
							return "InitialWindowSize";
						case static_cast<uint16_t>(Parameters::MaxFrameSize):
							return "MaxFrameSize";
						case static_cast<uint16_t>(Parameters::MaxHeaderListSize):
							return "MaxHeaderListSize";
						default:
							return "Unknown";
					}
				}

			private:
				bool ParsePayload() override
				{
					if (GetType() != Http2Frame::Type::Settings)
					{
						return false;
					}

					// SETTINGS frames always apply to a connection, never a single stream.
					// The stream identifier for a SETTINGS frame MUST be zero (0x0).  If an
					// endpoint receives a SETTINGS frame whose stream identifier field is
					// anything other than 0x0, the endpoint MUST respond with a connection
					// error (Section 5.4.1) of type PROTOCOL_ERROR.
					if (GetStreamId() != 0)
					{
						return false;
					}

					// The payload of a SETTINGS frame consists of zero or more parameters,
					auto payload = GetPayload();
					if (payload == nullptr)
					{
						return true;
					}
					
					// Parse each parameter in the payload
					auto payload_data = payload->GetDataAs<uint8_t>();
					auto payload_size = payload->GetLength();
					size_t payload_offset = 0;

					if (payload_size % 6 != 0)
					{
						return false;
					}

					while (payload_offset < payload_size)
					{
						auto parameter = ByteReader<uint16_t>::ReadBigEndian(payload_data + payload_offset);
						payload_offset += sizeof(uint16_t);

						auto value = ByteReader<uint32_t>::ReadBigEndian(payload_data + payload_offset);
						payload_offset += sizeof(uint32_t);

						_parameters[parameter] = value;
					}

					return true;
				}

				// Parameters
				std::map<uint16_t, uint32_t> _parameters;
			};
		}
	}
}