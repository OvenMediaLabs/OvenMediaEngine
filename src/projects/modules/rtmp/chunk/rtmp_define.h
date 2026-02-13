//==============================================================================
//
//  RtmpProvider
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <arpa/inet.h>
#include <base/ovlibrary/ovlibrary.h>
#include <string.h>

#include <map>
#include <memory>
#include <vector>

#define RTMP_MAX_PACKET_SIZE (20 * 1024 * 1024)	 // 20M

#define RTMP_TIME_SCALE 1000

#define RTMP_HANDSHAKE_VERSION 0x03
#define RTMP_HANDSHAKE_PACKET_SIZE 1536

#define RTMP_CHUNK_BASIC_HEADER_SIZE_MAX 3
#define RTMP_CHUNK_MSG_HEADER_SIZE_MAX (11 + 4)	 // header + extended timestamp
#define RTMP_PACKET_HEADER_SIZE_MAX (RTMP_CHUNK_BASIC_HEADER_SIZE_MAX + RTMP_CHUNK_MSG_HEADER_SIZE_MAX)

#define RTMP_EXTEND_TIMESTAMP 0x00ffffff
#define RTMP_EXTEND_TIMESTAMP_SIZE 4

enum class RtmpChunkStreamId : uint32_t
{
	Urgent = 2,
	Control = 3,
	Media = 8,

	Mask = 0b00111111,
};

// Overload operator to allow usage like `10 & RtmpChunkStreamId::Mask`
inline ov::UnderylingType<RtmpChunkStreamId> operator&(ov::UnderylingType<RtmpChunkStreamId> number, RtmpChunkStreamId id)
{
	return number & ov::ToUnderlyingType(id);
}

enum class RtmpMessageTypeID : uint8_t
{
	Unknown = 0,

	SetChunkSize = 1,
	Abort = 2,
	Acknowledgement = 3,
	UserControl = 4,
	WindowAcknowledgementSize = 5,
	SetPeerBandwidth = 6,
	Audio = 8,
	Video = 9,
	Amf3Data = 15,
	Amf3SharedObject = 16,
	Amf3Command = 17,
	Amf0Data = 18,
	Amf0SharedObject = 19,
	Amf0Command = 20,
	Aggregate = 22,
};

#define _DECLARE_ENUM_TO_STRING(type_id) \
	case RtmpMessageTypeID::type_id:     \
		return #type_id

inline constexpr const char *StringFromRtmpMessageTypeID(RtmpMessageTypeID type)
{
	switch (type)
	{
		_DECLARE_ENUM_TO_STRING(Unknown);
		_DECLARE_ENUM_TO_STRING(SetChunkSize);
		_DECLARE_ENUM_TO_STRING(Abort);
		_DECLARE_ENUM_TO_STRING(Acknowledgement);
		_DECLARE_ENUM_TO_STRING(UserControl);
		_DECLARE_ENUM_TO_STRING(WindowAcknowledgementSize);
		_DECLARE_ENUM_TO_STRING(SetPeerBandwidth);
		_DECLARE_ENUM_TO_STRING(Audio);
		_DECLARE_ENUM_TO_STRING(Video);
		_DECLARE_ENUM_TO_STRING(Amf3Data);
		_DECLARE_ENUM_TO_STRING(Amf3SharedObject);
		_DECLARE_ENUM_TO_STRING(Amf3Command);
		_DECLARE_ENUM_TO_STRING(Amf0Data);
		_DECLARE_ENUM_TO_STRING(Amf0SharedObject);
		_DECLARE_ENUM_TO_STRING(Amf0Command);
		_DECLARE_ENUM_TO_STRING(Aggregate);
	}

	return "Invalid";
}

enum class UserControlMessageId : uint16_t
{
	StreamBegin = 0,
	StreamEof = 1,
	StreamDry = 2,
	SetBufferLength = 3,
	StreamIsRecorded = 4,
	PingRequest = 6,
	PingResponse = 7,
	BufferEmpty = 31,
	BufferReady = 32,
};

enum class RtmpCommand : uint16_t
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
	OnUserDataEvent,// "onUserDataEvent" 
	OnUserData,		// "onUserData"

	// Response strings
	AckResult,	// "_result"
	AckError,	// "_error"
};

constexpr bool ConstExprStrCmp(const char *str1, const char *str2)
{
	while (*str1 && (*str1 == *str2))
	{
		++str1;
		++str2;
	}

	return *str1 == *str2;
}

#define _DECLARE_STRING_TO_RTMP_COMMAND(enum_name, command) \
	do                                                      \
	{                                                       \
		if (ConstExprStrCmp(name, command))                 \
		{                                                   \
			return RtmpCommand::enum_name;                  \
		}                                                   \
	} while (false)

constexpr RtmpCommand RtmpCommandFromString(const char *name)
{
	_DECLARE_STRING_TO_RTMP_COMMAND(Connect, "connect");
	_DECLARE_STRING_TO_RTMP_COMMAND(Call, "call");
	_DECLARE_STRING_TO_RTMP_COMMAND(Close, "close");
	_DECLARE_STRING_TO_RTMP_COMMAND(CreateStream, "createStream");
	_DECLARE_STRING_TO_RTMP_COMMAND(Play, "play");
	_DECLARE_STRING_TO_RTMP_COMMAND(Play2, "play2");
	_DECLARE_STRING_TO_RTMP_COMMAND(DeleteStream, "deleteStream");
	_DECLARE_STRING_TO_RTMP_COMMAND(CloseStream, "closeStream");
	_DECLARE_STRING_TO_RTMP_COMMAND(ReceiveAudio, "receiveAudio");
	_DECLARE_STRING_TO_RTMP_COMMAND(ReceiveVideo, "receiveVideo");
	_DECLARE_STRING_TO_RTMP_COMMAND(Publish, "publish");
	_DECLARE_STRING_TO_RTMP_COMMAND(Seek, "seek");
	_DECLARE_STRING_TO_RTMP_COMMAND(Pause, "pause");
	_DECLARE_STRING_TO_RTMP_COMMAND(ReleaseStream, "releaseStream");
	_DECLARE_STRING_TO_RTMP_COMMAND(FCPublish, "FCPublish");
	_DECLARE_STRING_TO_RTMP_COMMAND(FCUnpublish, "FCUnpublish");
	_DECLARE_STRING_TO_RTMP_COMMAND(SetChallenge, "setChallenge");
	_DECLARE_STRING_TO_RTMP_COMMAND(SetDataFrame, "@setDataFrame");
	_DECLARE_STRING_TO_RTMP_COMMAND(Ping, "ping");
	_DECLARE_STRING_TO_RTMP_COMMAND(OnStatus, "onStatus");
	_DECLARE_STRING_TO_RTMP_COMMAND(OnFCPublish, "onFCPublish");
	_DECLARE_STRING_TO_RTMP_COMMAND(OnFCUnpublish, "onFCUnpublish");
	_DECLARE_STRING_TO_RTMP_COMMAND(OnFI, "onFI");
	_DECLARE_STRING_TO_RTMP_COMMAND(OnClientLogin, "onClientLogin");
	_DECLARE_STRING_TO_RTMP_COMMAND(OnMetaData, "onMetaData");
	_DECLARE_STRING_TO_RTMP_COMMAND(OnBWDone, "onBWDone");
	_DECLARE_STRING_TO_RTMP_COMMAND(OnTextData, "onTextData");
	_DECLARE_STRING_TO_RTMP_COMMAND(OnCuePoint, "onCuePoint");
	_DECLARE_STRING_TO_RTMP_COMMAND(OnUserDataEvent, "onUserDataEvent");
	_DECLARE_STRING_TO_RTMP_COMMAND(OnUserData, "onUserData");
	_DECLARE_STRING_TO_RTMP_COMMAND(AckResult, "_result");
	_DECLARE_STRING_TO_RTMP_COMMAND(AckError, "_error");

	return RtmpCommand::Unknown;
}

#define _DECLARE_RTMP_COMMAND_TO_STRING(enum_name, command) \
	case RtmpCommand::enum_name:                            \
		return command

inline constexpr const char *StringFromRtmpCommand(RtmpCommand command)
{
	switch (command)
	{
		_DECLARE_RTMP_COMMAND_TO_STRING(Unknown, nullptr);
		_DECLARE_RTMP_COMMAND_TO_STRING(Connect, "connect");
		_DECLARE_RTMP_COMMAND_TO_STRING(Call, "call");
		_DECLARE_RTMP_COMMAND_TO_STRING(Close, "close");
		_DECLARE_RTMP_COMMAND_TO_STRING(CreateStream, "createStream");
		_DECLARE_RTMP_COMMAND_TO_STRING(Play, "play");
		_DECLARE_RTMP_COMMAND_TO_STRING(Play2, "play2");
		_DECLARE_RTMP_COMMAND_TO_STRING(DeleteStream, "deleteStream");
		_DECLARE_RTMP_COMMAND_TO_STRING(CloseStream, "closeStream");
		_DECLARE_RTMP_COMMAND_TO_STRING(ReceiveAudio, "receiveAudio");
		_DECLARE_RTMP_COMMAND_TO_STRING(ReceiveVideo, "receiveVideo");
		_DECLARE_RTMP_COMMAND_TO_STRING(Publish, "publish");
		_DECLARE_RTMP_COMMAND_TO_STRING(Seek, "seek");
		_DECLARE_RTMP_COMMAND_TO_STRING(Pause, "pause");
		_DECLARE_RTMP_COMMAND_TO_STRING(ReleaseStream, "releaseStream");
		_DECLARE_RTMP_COMMAND_TO_STRING(FCPublish, "FCPublish");
		_DECLARE_RTMP_COMMAND_TO_STRING(FCUnpublish, "FCUnpublish");
		_DECLARE_RTMP_COMMAND_TO_STRING(SetChallenge, "setChallenge");
		_DECLARE_RTMP_COMMAND_TO_STRING(SetDataFrame, "@setDataFrame");
		_DECLARE_RTMP_COMMAND_TO_STRING(Ping, "ping");
		_DECLARE_RTMP_COMMAND_TO_STRING(OnStatus, "onStatus");
		_DECLARE_RTMP_COMMAND_TO_STRING(OnFCPublish, "onFCPublish");
		_DECLARE_RTMP_COMMAND_TO_STRING(OnFCUnpublish, "onFCUnpublish");
		_DECLARE_RTMP_COMMAND_TO_STRING(OnFI, "onFI");
		_DECLARE_RTMP_COMMAND_TO_STRING(OnClientLogin, "onClientLogin");
		_DECLARE_RTMP_COMMAND_TO_STRING(OnMetaData, "onMetaData");
		_DECLARE_RTMP_COMMAND_TO_STRING(OnBWDone, "onBWDone");
		_DECLARE_RTMP_COMMAND_TO_STRING(OnTextData, "onTextData");
		_DECLARE_RTMP_COMMAND_TO_STRING(OnCuePoint, "onCuePoint");
		_DECLARE_RTMP_COMMAND_TO_STRING(OnUserDataEvent, "onUserDataEvent");
		_DECLARE_RTMP_COMMAND_TO_STRING(OnUserData, "onUserData");
		_DECLARE_RTMP_COMMAND_TO_STRING(AckResult, "_result");
		_DECLARE_RTMP_COMMAND_TO_STRING(AckError, "_error");
	}

	return nullptr;
}

#define RTMP_DEFAULT_ACKNOWNLEDGEMENT_SIZE 2500000
#define RTMP_DEFAULT_PEER_BANDWIDTH 2500000
#define RTMP_DEFAULT_CHUNK_SIZE 128

#define RTMP_VIDEO_DATA_MIN_SIZE (1 + 1 + 3)  // control(1) + sequence(1) + offsettime(3)
#define RTMP_AAC_AUDIO_DATA_MIN_SIZE (1 + 1)  // control(1) + sequence(1)

#define RTMP_UNKNOWN_DEVICE_TYPE_STRING "unknown_device_type"
#define RTMP_DEFAULT_CLIENT_VERSION "rtmp client 1.0"  //"afcCli 11,0,100,1 (compatible; FMSc/1.0)"

enum class RtmpEncoderType : int32_t
{
	Custom = 0,	 // General Rtmp client (Client that cannot be distinguished from others)
	Xsplit,		 // XSplit
	OBS,		 // OBS
	Lavf,		 // Libavformat (lavf)
};

enum class RtmpCodecType : int32_t
{
	Unknown,
	H264,	//	H264/X264 avc1(7)
	AAC,	//	AAC          mp4a(10)
	MP3,	//	MP3(2)
	Speex,	//	SPEEX(11)
};

enum class RtmpHandshakeState
{
	Uninitialized = 0,
	C0,
	C1,
	S0,
	S1,
	S2,
	C2,
	Complete = C2,
};

struct RtmpMediaInfo
{
public:
	bool video_stream_coming = false;
	bool audio_stream_coming = false;

	RtmpCodecType video_codec_type = RtmpCodecType::H264;
	int video_width = 0;
	int video_height = 0;
	float video_framerate = 0;
	int video_bitrate = 0;

	RtmpCodecType audio_codec_type = RtmpCodecType::AAC;
	int audio_channels = 0;
	int audio_bits = 0;
	int audio_samplerate = 0;
	int audio_sampleindex = 0;
	int audio_bitrate = 0;

	uint32_t timestamp_scale = RTMP_TIME_SCALE;
	RtmpEncoderType encoder_type = RtmpEncoderType::Custom;
};
