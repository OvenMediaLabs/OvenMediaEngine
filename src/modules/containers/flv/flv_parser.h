//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

namespace flv
{
	constexpr int MIN_VIDEO_DATA_LENGTH = 5;
	constexpr int MIN_AUDIO_DATA_LENGTH = 2;

	// Video
	enum class VideoFrameType : uint8_t
	{
		Key = 1,			  // For AVC, a seekable frame
		Inter = 2,			  // For AVC, a non-seekable frame
		DisposableInter = 3,  // H.263 Only
		GeneratedKey = 4,	  // Reserved for server use only
		VideoInfoCommand = 5,
	};

	enum class VideoCodecId : uint8_t
	{
		JPEG = 1,
		SorensonH263 = 2,
		ScreenVideo = 3,
		On2Vp6 = 4,
		On2Vp6WithAlphaChannel = 5,
		ScreenVideoVersion2 = 6,
		AVC = 7,  // OME supports only this codec on RTMP
	};

	enum class AvcPacketType : uint8_t
	{
		SequenceHeader = 0,	 // AVCDecoderConfigurationRecord (ISO 14496-15, 5.2.4.1)
		NALU = 1,
		EndOfSequence = 2
	};

	class VideoData
	{
	public:
		bool Parse(const std::shared_ptr<const ov::Data> &data);

		VideoFrameType FrameType();
		VideoCodecId CodecId();
		AvcPacketType PacketType();
		int64_t CompositionTime();

		const uint8_t *Payload();
		size_t PayloadLength();

	private:
		VideoFrameType _frame_type;	 // UB[4]
		VideoCodecId _codec_id;		 // UB[4], Only support AVC

		// AVCVIDEOPACKET
		AvcPacketType _packet_type;	 // UI8
		int32_t _composition_time;	 // SI24

		// if FlvAvcPacketType == 0
		// 		AVCDecoderConfigurationRecord
		// else if FlvAvcPacketType == 1
		//		One or more NALUs
		// else if FlvAvcPacketType == 2
		//		Empty

		const uint8_t *_payload;
		size_t _payload_length;
	};

	// Audio
	enum class SoundFormat : uint8_t
	{
		LinearPCMPlatformEndian = 0,  // Linear PCM, platform endian
		ADPCM = 1,					  // ADPCM
		MP3 = 2,					  // MP3
		LinearPCMLittleEndian = 3,	  // Linear PCM, little endian
		Nellymoser16KHzMono = 4,	  // Nellymoser 16 kHz mono
		Nellymoser8KHzMono = 5,		  // Nellymoser 8 kHz mono
		Nellymoser = 6,				  // Nellymoser
		G711ALawPCM = 7,			  // G.711 A-low logarithmic PCM
		G711MULawPCM = 8,			  // G.711 mu-law logarithmic PCM
		Reserved = 9,				  // Reserved
		AAC = 10,					  // OME supports only this sound format
		Speex = 11,					  // Speex
		MP38KHz = 14,				  // MP3 8 kHz
		DeviceSpecificSound = 15,	  // Device-specific sound
	};

	enum class SoundRate : uint8_t
	{
		_5_5 = 0,  // 5.5 kHZ
		_11 = 1,   // 11 KHZ
		_22 = 2,   // 22 KHZ
		_44 = 3	   // 44 kHZ, AAC always 3
	};

	enum class SoundSize : uint8_t
	{
		_8Bit = 0,	 // 8-bit samples
		_16Bit = 1,	 // 16-bit samples
	};

	enum class SoundType : uint8_t
	{
		Mono = 0,
		Stereo = 1	// AAC always 1
	};

	enum class AACPacketType : uint8_t
	{
		SequenceHeader = 0,	 // AudioSpecificConfig (ISO 14496-3 1.6.2)
		Raw = 1
	};

	class AudioData
	{
	public:
		bool Parse(const std::shared_ptr<const ov::Data> &data);

		SoundFormat Format();
		SoundRate SampleRate();
		SoundSize SampleSize();
		SoundType Channel();
		AACPacketType PacketType();

		const uint8_t *Payload();
		size_t PayloadLength();

	private:
		SoundFormat _format;	 // UB[4]
		SoundRate _sample_rate;	 // UB[2]
		SoundSize _sample_size;	 // UB[1]
		SoundType _channel;		 // UB[1]

		// format == AACAUDIODATA
		AACPacketType _packet_type;	 // UI8

		// if packet_type == 0
		//		AudioSpecificConfig (ISO 14496-3 1.6.2)
		// else
		//		Raw AAC frame data
		const uint8_t *_payload;
		size_t _payload_length;
	};
}  // namespace flv
