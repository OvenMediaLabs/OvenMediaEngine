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

#define CHECK_HTTP2_FRAME_FLAG(flag) ((GetFlags() & static_cast<uint8_t>(flag)) ? true:false)
#define IS_HTTP2_FRAME_FLAG_ON(flag) IsFlagOn(static_cast<uint8_t>(flag))
#define TURN_ON_HTTP2_FRAME_FLAG(flag) SetFlag(static_cast<uint8_t>(flag))

namespace http
{
	namespace prot
	{
		namespace h2
		{

			// https://datatracker.ietf.org/doc/html/rfc7540#section-4.1
			//
			// +-----------------------------------------------+
			// |                 Length (24)                   |
			// +---------------+---------------+---------------+
			// |   Type (8)    |   Flags (8)   |
			// +-+-------------+---------------+-------------------------------+
			// |R|                 Stream Identifier (31)                      |
			// +=+=============================================================+
			// |                   Frame Payload (0...)                      ...
			// +---------------------------------------------------------------+
			//                       Figure 1: Frame Layout

#define HTTP2_FRAME_HEADER_SIZE (3+1+1+4)

			// For HTTP/2.0
			class Http2Frame : public ov::EnableSharedFromThis<Http2Frame>
			{
			public:
				enum class ParsingState : uint8_t
				{
					Init,
					HeaderParsed,
					Completed,
					Error,
				};

				enum class Type : uint8_t
				{
					Data = 0x0,
					Headers = 0x1,
					Priority = 0x2,
					RstStream = 0x3,
					Settings = 0x4,
					PushPromise = 0x5,
					Ping = 0x6,
					GoAway = 0x7,
					WindowUpdate = 0x8,
					Continuation = 0x9,
					Unknown = 0xA,
				};

				Http2Frame();
				Http2Frame(uint32_t stream_id);

				// For frame parsing
				ssize_t AppendData(const std::shared_ptr<const ov::Data> &data);

				template<typename T>
				inline std::shared_ptr<T> GetFrameAs()
				{
					auto frame = std::make_shared<T>(GetSharedPtr());
					if (std::dynamic_pointer_cast<Http2Frame>(frame)->ParsePayload() == false)
					{
						return nullptr;
					}
					return frame;
				}

				virtual ov::String ToString() const;

				// Getters
				ParsingState GetParsingState() const noexcept;
				uint32_t GetLength() const;
				Type GetType() const noexcept;
				uint8_t GetFlags() const noexcept;
				bool IsFlagOn(uint8_t flag) const noexcept;
				uint32_t GetStreamId() const noexcept;
				virtual const std::shared_ptr<const ov::Data> GetPayload() const;

				// To data
				std::shared_ptr<ov::Data> ToData() const;

			protected:
				// Copy construcor only can be called in derived class
				Http2Frame(const std::shared_ptr<const Http2Frame> &frame);

				// Setters
				void SetType(Type type) noexcept;
				void SetFlags(uint8_t flags) noexcept;
				// Turn on flag
				void SetFlag(uint8_t flag) noexcept;
				void SetStreamId(uint32_t stream_id) noexcept;
				void SetPayload(const std::shared_ptr<const ov::Data> &payload) noexcept;

				// Set state
				void SetParsingState(ParsingState state) noexcept;
				
			private:
				bool ParseHeader();
				virtual bool ParsePayload() {return false;};

				uint32_t _length = 0;
				Type _type = Type::Unknown;
				uint8_t _flags = 0;
				uint32_t _stream_id = 0;
				std::shared_ptr<const ov::Data> _payload = nullptr;

				ParsingState _state = ParsingState::Init;
				// Temporary buffer to parse header
				ov::Data _packet_buffer;
			};
		}  // namespace h2
	} // namespace prot
}  // namespace http