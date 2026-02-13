//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <arpa/inet.h>
#include <base/ovlibrary/ovlibrary.h>
#include <modules/containers/flv_v2/flv_parser.h>

#include <map>
#include <memory>
#include <vector>

namespace modules::rtmp
{
	static constexpr const auto HANDSHAKE_VERSION			   = 0x03;

	static constexpr const auto TIME_SCALE					   = 1000;
	static constexpr const auto EXTENDED_TIMESTAMP_VALUE	   = 0x00FFFFFF;
	static constexpr const auto EXTENDED_TIMESTAMP_SIZE		   = 4;

	// Basic header is 3 bytes when `chunk_stream_id == 0b000001`
	static constexpr const auto MAX_BASIC_HEADER_SIZE		   = 3;

	// Message header is 11 bytes when `format_type == MessageHeaderType::T0`
	static constexpr const auto MAX_MESSAGE_HEADER_SIZE		   = (11 + EXTENDED_TIMESTAMP_SIZE);

	static constexpr const auto MAX_TYPE0_PACKET_HEADER_SIZE   = (MAX_BASIC_HEADER_SIZE + 11 + EXTENDED_TIMESTAMP_SIZE);
	static constexpr const auto MAX_TYPE1_PACKET_HEADER_SIZE   = (MAX_BASIC_HEADER_SIZE + 7 + EXTENDED_TIMESTAMP_SIZE);
	static constexpr const auto MAX_TYPE2_PACKET_HEADER_SIZE   = (MAX_BASIC_HEADER_SIZE + 3 + EXTENDED_TIMESTAMP_SIZE);
	static constexpr const auto MAX_TYPE3_PACKET_HEADER_LENGTH = (MAX_BASIC_HEADER_SIZE + 0 + EXTENDED_TIMESTAMP_SIZE);

	static constexpr const auto MAX_PACKET_HEADER_SIZE		   = ov::cexpr::Max(
		MAX_TYPE0_PACKET_HEADER_SIZE,
		MAX_TYPE1_PACKET_HEADER_SIZE,
		MAX_TYPE2_PACKET_HEADER_SIZE,
		MAX_TYPE3_PACKET_HEADER_LENGTH);

	static constexpr const auto HANDSHAKE_PACKET_LENGTH = 1536;

	enum class ChunkStreamId : uint32_t
	{
		Urgent	= 0b00000010,  // 2
		Control = 0b00000011,  // 3
		Media	= 0b00001000,  // 8

		Mask	= 0b00111111,
	};

	// Overload operator to allow usage like `10 & ChunkStreamId::Mask`
	inline ov::UnderylingType<ChunkStreamId> operator&(ov::UnderylingType<ChunkStreamId> number, ChunkStreamId id)
	{
		return number & ov::ToUnderlyingType(id);
	}

	enum class MessageTypeID : uint8_t
	{
		Unknown					  = 0,

		SetChunkSize			  = 1,
		Abort					  = 2,
		Acknowledgement			  = 3,
		UserControl				  = 4,
		WindowAcknowledgementSize = 5,
		SetPeerBandwidth		  = 6,
		Audio					  = 8,
		Video					  = 9,
		Amf3Data				  = 15,
		Amf3SharedObject		  = 16,
		Amf3Command				  = 17,
		Amf0Data				  = 18,
		Amf0SharedObject		  = 19,
		Amf0Command				  = 20,
		Aggregate				  = 22,
	};
	constexpr const char *EnumToString(MessageTypeID type)
	{
		switch (type)
		{
			OV_CASE_RETURN_ENUM_STRING(MessageTypeID, Unknown);
			OV_CASE_RETURN_ENUM_STRING(MessageTypeID, SetChunkSize);
			OV_CASE_RETURN_ENUM_STRING(MessageTypeID, Abort);
			OV_CASE_RETURN_ENUM_STRING(MessageTypeID, Acknowledgement);
			OV_CASE_RETURN_ENUM_STRING(MessageTypeID, UserControl);
			OV_CASE_RETURN_ENUM_STRING(MessageTypeID, WindowAcknowledgementSize);
			OV_CASE_RETURN_ENUM_STRING(MessageTypeID, SetPeerBandwidth);
			OV_CASE_RETURN_ENUM_STRING(MessageTypeID, Audio);
			OV_CASE_RETURN_ENUM_STRING(MessageTypeID, Video);
			OV_CASE_RETURN_ENUM_STRING(MessageTypeID, Amf3Data);
			OV_CASE_RETURN_ENUM_STRING(MessageTypeID, Amf3SharedObject);
			OV_CASE_RETURN_ENUM_STRING(MessageTypeID, Amf3Command);
			OV_CASE_RETURN_ENUM_STRING(MessageTypeID, Amf0Data);
			OV_CASE_RETURN_ENUM_STRING(MessageTypeID, Amf0SharedObject);
			OV_CASE_RETURN_ENUM_STRING(MessageTypeID, Amf0Command);
			OV_CASE_RETURN_ENUM_STRING(MessageTypeID, Aggregate);
		}

		return "Invalid";
	}

	enum class UserControlEventType : uint16_t
	{
		StreamBegin		 = 0,
		StreamEof		 = 1,
		StreamDry		 = 2,
		SetBufferLength	 = 3,
		StreamIsRecorded = 4,
		PingRequest		 = 6,
		PingResponse	 = 7,
		BufferEmpty		 = 31,
		BufferReady		 = 32,
	};
	static_assert(sizeof(UserControlEventType) == 2, "UserControlEventType size is not 2");
	constexpr const char *EnumToString(UserControlEventType type)
	{
		switch (type)
		{
			OV_CASE_RETURN_ENUM_STRING(UserControlEventType, StreamBegin);
			OV_CASE_RETURN_ENUM_STRING(UserControlEventType, StreamEof);
			OV_CASE_RETURN_ENUM_STRING(UserControlEventType, StreamDry);
			OV_CASE_RETURN_ENUM_STRING(UserControlEventType, SetBufferLength);
			OV_CASE_RETURN_ENUM_STRING(UserControlEventType, StreamIsRecorded);
			OV_CASE_RETURN_ENUM_STRING(UserControlEventType, PingRequest);
			OV_CASE_RETURN_ENUM_STRING(UserControlEventType, PingResponse);
			OV_CASE_RETURN_ENUM_STRING(UserControlEventType, BufferEmpty);
			OV_CASE_RETURN_ENUM_STRING(UserControlEventType, BufferReady);
		}

		return "Unknown";
	}

	enum class PeerBandwidthLimitType : uint8_t
	{
		Hard	= 0,
		Soft	= 1,
		Dynamic = 2,
	};

	enum class Command : uint16_t
	{
		Unknown,

		// NetConnection Commands
		Connect,	   // "connect"
		Call,		   // "call"
		Close,		   // "close"
		CreateStream,  // "createStream"

		// NetStream Commands
		Play,		   // "play"
		Play2,		   // "play2"
		DeleteStream,  // "deleteStream"
		CloseStream,   // "closeStream"
		ReceiveAudio,  // "receiveAudio"
		ReceiveVideo,  // "receiveVideo"
		Publish,	   // "publish"
		Seek,		   // "seek"
		Pause,		   // "pause"

		ReleaseStream,	// "releaseStream"
		FCPublish,		// "FCPublish"
		FCUnpublish,	// "FCUnpublish"
		SetChallenge,	// "setChallenge"
		SetDataFrame,	// "@setDataFrame"
		Ping,			// "ping"

		OnStatus,		// "onStatus"
		OnFCPublish,	// "onFCPublish"
		OnFCUnpublish,	// "onFCUnpublish"
		OnFI,			// "onFI"
		OnClientLogin,	// "onClientLogin"
		OnMetaData,		// "onMetaData"
		OnBWDone,		// "onBWDone"
		OnTextData,		// "onTextData"
		OnCuePoint,		// "onCuePoint"

		// Response strings
		AckResult,	// "_result"
		AckError,	// "_error"
	};
	constexpr Command ToCommand(const char *name)
	{
		OV_IF_RETURN(ov::cexpr::StrCmp(name, "connect") == 0, Command::Connect);
		OV_IF_RETURN(ov::cexpr::StrCmp(name, "call") == 0, Command::Call);
		OV_IF_RETURN(ov::cexpr::StrCmp(name, "close") == 0, Command::Close);
		OV_IF_RETURN(ov::cexpr::StrCmp(name, "createStream") == 0, Command::CreateStream);
		OV_IF_RETURN(ov::cexpr::StrCmp(name, "play") == 0, Command::Play);
		OV_IF_RETURN(ov::cexpr::StrCmp(name, "play2") == 0, Command::Play2);
		OV_IF_RETURN(ov::cexpr::StrCmp(name, "deleteStream") == 0, Command::DeleteStream);
		OV_IF_RETURN(ov::cexpr::StrCmp(name, "closeStream") == 0, Command::CloseStream);
		OV_IF_RETURN(ov::cexpr::StrCmp(name, "receiveAudio") == 0, Command::ReceiveAudio);
		OV_IF_RETURN(ov::cexpr::StrCmp(name, "receiveVideo") == 0, Command::ReceiveVideo);
		OV_IF_RETURN(ov::cexpr::StrCmp(name, "publish") == 0, Command::Publish);
		OV_IF_RETURN(ov::cexpr::StrCmp(name, "seek") == 0, Command::Seek);
		OV_IF_RETURN(ov::cexpr::StrCmp(name, "pause") == 0, Command::Pause);
		OV_IF_RETURN(ov::cexpr::StrCmp(name, "releaseStream") == 0, Command::ReleaseStream);
		OV_IF_RETURN(ov::cexpr::StrCmp(name, "FCPublish") == 0, Command::FCPublish);
		OV_IF_RETURN(ov::cexpr::StrCmp(name, "FCUnpublish") == 0, Command::FCUnpublish);
		OV_IF_RETURN(ov::cexpr::StrCmp(name, "setChallenge") == 0, Command::SetChallenge);
		OV_IF_RETURN(ov::cexpr::StrCmp(name, "@se == 0tDataFrame"), Command::SetDataFrame);
		OV_IF_RETURN(ov::cexpr::StrCmp(name, "ping") == 0, Command::Ping);
		OV_IF_RETURN(ov::cexpr::StrCmp(name, "onStatus") == 0, Command::OnStatus);
		OV_IF_RETURN(ov::cexpr::StrCmp(name, "onFCPublish") == 0, Command::OnFCPublish);
		OV_IF_RETURN(ov::cexpr::StrCmp(name, "onFCUnpublish") == 0, Command::OnFCUnpublish);
		OV_IF_RETURN(ov::cexpr::StrCmp(name, "onFI") == 0, Command::OnFI);
		OV_IF_RETURN(ov::cexpr::StrCmp(name, "onClientLogin") == 0, Command::OnClientLogin);
		OV_IF_RETURN(ov::cexpr::StrCmp(name, "onMetaData") == 0, Command::OnMetaData);
		OV_IF_RETURN(ov::cexpr::StrCmp(name, "onBWDone") == 0, Command::OnBWDone);
		OV_IF_RETURN(ov::cexpr::StrCmp(name, "onTextData") == 0, Command::OnTextData);
		OV_IF_RETURN(ov::cexpr::StrCmp(name, "onCuePoint") == 0, Command::OnCuePoint);
		OV_IF_RETURN(ov::cexpr::StrCmp(name, "_result") == 0, Command::AckResult);
		OV_IF_RETURN(ov::cexpr::StrCmp(name, "_error") == 0, Command::AckError);

		return Command::Unknown;
	}
	constexpr const char *EnumToString(const Command command)
	{
		switch (command)
		{
			OV_CASE_RETURN(Command::Unknown, nullptr);
			OV_CASE_RETURN(Command::Connect, "connect");
			OV_CASE_RETURN(Command::Call, "call");
			OV_CASE_RETURN(Command::Close, "close");
			OV_CASE_RETURN(Command::CreateStream, "createStream");
			OV_CASE_RETURN(Command::Play, "play");
			OV_CASE_RETURN(Command::Play2, "play2");
			OV_CASE_RETURN(Command::DeleteStream, "deleteStream");
			OV_CASE_RETURN(Command::CloseStream, "closeStream");
			OV_CASE_RETURN(Command::ReceiveAudio, "receiveAudio");
			OV_CASE_RETURN(Command::ReceiveVideo, "receiveVideo");
			OV_CASE_RETURN(Command::Publish, "publish");
			OV_CASE_RETURN(Command::Seek, "seek");
			OV_CASE_RETURN(Command::Pause, "pause");
			OV_CASE_RETURN(Command::ReleaseStream, "releaseStream");
			OV_CASE_RETURN(Command::FCPublish, "FCPublish");
			OV_CASE_RETURN(Command::FCUnpublish, "FCUnpublish");
			OV_CASE_RETURN(Command::SetChallenge, "setChallenge");
			OV_CASE_RETURN(Command::SetDataFrame, "@setDataFrame");
			OV_CASE_RETURN(Command::Ping, "ping");
			OV_CASE_RETURN(Command::OnStatus, "onStatus");
			OV_CASE_RETURN(Command::OnFCPublish, "onFCPublish");
			OV_CASE_RETURN(Command::OnFCUnpublish, "onFCUnpublish");
			OV_CASE_RETURN(Command::OnFI, "onFI");
			OV_CASE_RETURN(Command::OnClientLogin, "onClientLogin");
			OV_CASE_RETURN(Command::OnMetaData, "onMetaData");
			OV_CASE_RETURN(Command::OnBWDone, "onBWDone");
			OV_CASE_RETURN(Command::OnTextData, "onTextData");
			OV_CASE_RETURN(Command::OnCuePoint, "onCuePoint");
			OV_CASE_RETURN(Command::AckResult, "_result");
			OV_CASE_RETURN(Command::AckError, "_error");
		}

		return nullptr;
	}

	enum class HandshakeState
	{
		WaitingForC0,
		WaitingForC1,
		WaitingForC2,
		Completed,
	};

	struct MediaInfo
	{
		// If `codec_id` is `std::nullopt`, it means that the codec ID
		// was not found in the metadata or other sources
		std::optional<cmn::MediaCodecId> codec_id;
		ov::String codec_raw;

		virtual void Reset()
		{
			codec_id.reset();
			codec_raw.Clear();
		}

		// Returns 'true' if the codec information was obtained from
		// metadata or other sources.
		bool HasCodec() const
		{
			return codec_id.has_value();
		}

		// Returns `true` if the codec information was obtained from
		// metadata or other sources and the codec is supported by RTMP.
		bool IsSupportedCodec() const
		{
			return HasCodec() && (codec_id.value() != cmn::MediaCodecId::None);
		}
	};

	// This structure contains the media information created from
	// the metadata received when `SetDataFrame`/`OnMetaData` is received.
	struct VideoMediaInfo : public MediaInfo
	{
		std::optional<int> width;
		std::optional<int> height;
		std::optional<float> framerate;
		std::optional<int> bitrate;

		void Reset() override
		{
			MediaInfo::Reset();
			width.reset();
			height.reset();
			framerate.reset();
			bitrate.reset();
		}
	};

	struct AudioMediaInfo : public MediaInfo
	{
		std::optional<cmn::AudioChannel::Layout> channel_layout;
		std::optional<int> bits;
		std::optional<int> samplerate;
		std::optional<int> sampleindex;
		std::optional<int> bitrate;

		void Reset() override
		{
			MediaInfo::Reset();
			channel_layout.reset();
			bits.reset();
			samplerate.reset();
			sampleindex.reset();
			bitrate.reset();
		}
	};
}  // namespace modules::rtmp
