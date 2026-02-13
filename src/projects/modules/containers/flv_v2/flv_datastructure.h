//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

namespace modules
{
	namespace flv
	{
		//--------------------------------------------------------------------
		// Common Definitions
		//--------------------------------------------------------------------
		enum class AvMultitrackType : uint8_t
		{
			//
			// Used by audio and video pipeline
			//

			OneTrack = 0,
			ManyTracks = 1,
			ManyTracksManyCodecs = 2,

			//	3 - Reserved
			// ...
			// 15 - Reserved
		};
		constexpr const char *EnumToString(AvMultitrackType type)
		{
			switch (type)
			{
				OV_CASE_RETURN_ENUM_STRING(AvMultitrackType, OneTrack);
				OV_CASE_RETURN_ENUM_STRING(AvMultitrackType, ManyTracks);
				OV_CASE_RETURN_ENUM_STRING(AvMultitrackType, ManyTracksManyCodecs);
			}

			return "(Unknown)";
		}

		//--------------------------------------------------------------------
		// Related to Video
		//--------------------------------------------------------------------
		enum class VideoFrameType : uint8_t
		{
			// 0 - reserved
			KeyFrame = 1,			   // a seekable frame
			InterFrame = 2,			   // a non - seekable frame
			DisposableInterFrame = 3,  // H.263 only
			GeneratedKeyFrame = 4,	   // reserved for server use only

			// If videoFrameType is not ignored and is set to VideoFrameType.Command,
			// the payload will not contain video data. Instead, (Ex)VideoTagHeader
			// will be followed by a UI8, representing the following meanings:
			//
			//     0 = Start of client-side seeking video frame sequence
			//     1 = End of client-side seeking video frame sequence
			//
			// frameType is ignored if videoPacketType is VideoPacketType.MetaData
			Command = 5,  // video info / command frame

			// 6 = reserved
			// 7 = reserved
		};
		constexpr const char *EnumToString(VideoFrameType frame_type)
		{
			switch (frame_type)
			{
				OV_CASE_RETURN_ENUM_STRING(VideoFrameType, KeyFrame);
				OV_CASE_RETURN_ENUM_STRING(VideoFrameType, InterFrame);
				OV_CASE_RETURN_ENUM_STRING(VideoFrameType, DisposableInterFrame);
				OV_CASE_RETURN_ENUM_STRING(VideoFrameType, GeneratedKeyFrame);
				OV_CASE_RETURN_ENUM_STRING(VideoFrameType, Command);
			}

			return "(Unknown)";
		}

		enum class VideoCommand : uint8_t
		{
			StartSeek = 0,
			EndSeek = 1,

			// 0x03 = reserved
			// ...
			// 0xff = reserved
		};
		constexpr const char *EnumToString(VideoCommand command)
		{
			switch (command)
			{
				OV_CASE_RETURN_ENUM_STRING(VideoCommand, StartSeek);
				OV_CASE_RETURN_ENUM_STRING(VideoCommand, EndSeek);
			}

			return "(Unknown)";
		}

		enum class VideoCodecId : uint8_t
		{
			// These values remain as they were in the legacy [FLV] specification.
			// If the IsExVideoHeader flag is set, we switch into
			// FOURCC video mode defined in the VideoFourCc enumeration.
			// This means that VideoCodecId (UB[4] bits) is not interpreted
			// as a codec identifier. Instead, these UB[4] bits are
			// interpreted as VideoPacketType.

			// 0 - Reserved
			// 1 - Reserved
			SorensonH263 = 2,
			Screen = 3,
			On2VP6 = 4,
			On2VP6A = 5,  // with alpha channel
			ScreenV2 = 6,
			Avc = 7,
			// 8 - Reserved
			// ...
			// 15 - Reserved
		};
		constexpr const char *EnumToString(VideoCodecId codec_id)
		{
			switch (codec_id)
			{
				OV_CASE_RETURN_ENUM_STRING(VideoCodecId, SorensonH263);
				OV_CASE_RETURN_ENUM_STRING(VideoCodecId, Screen);
				OV_CASE_RETURN_ENUM_STRING(VideoCodecId, On2VP6);
				OV_CASE_RETURN_ENUM_STRING(VideoCodecId, On2VP6A);
				OV_CASE_RETURN_ENUM_STRING(VideoCodecId, ScreenV2);
				OV_CASE_RETURN_ENUM_STRING(VideoCodecId, Avc);
			}

			return "(Unknown)";
		}

		enum class VideoPacketType : uint8_t
		{
			SequenceStart = 0,
			CodedFrames = 1,
			SequenceEnd = 2,

			// CompositionTime Offset is implicitly set to zero. This optimization
			// avoids transmitting an SI24 composition time value of zero over the wire.
			// See the ExVideoTagBody section below for corresponding pseudocode.
			CodedFramesX = 3,

			// ExVideoTagBody does not contain video data. Instead, it contains
			// an AMF-encoded metadata. Refer to the Metadata Frame section for
			// an illustration of its usage. For example, the metadata might include
			// HDR information. This also enables future possibilities for expressing
			// additional metadata meant for subsequent video sequences.
			//
			// If VideoPacketType.Metadata is present, the FrameType flags
			// at the top of this table should be ignored.
			Metadata = 4,

			// Carriage of bitstream in MPEG-2 TS format
			//
			// PacketTypeSequenceStart and PacketTypeMPEG2TSSequenceStart
			// are mutually exclusive
			MPEG2TSSequenceStart = 5,

			// Turns on video multitrack mode
			Multitrack = 6,

			// ModEx is a special signal within the VideoPacketType enum that
			// serves to both modify and extend the behavior of the current packet.
			// When this signal is encountered, it indicates the presence of
			// additional modifiers or extensions, requiring further processing to
			// adjust or augment the packet's functionality. ModEx can be used to
			// introduce new capabilities or modify existing ones, such as
			// enabling support for high-precision timestamps or other advanced
			// features that enhance the base packet structure.
			ModEx = 7,

			//  8 - Reserved
			// ...
			// 14 - reserved
			// 15 - reserved
		};
		constexpr const char *EnumToString(VideoPacketType type)
		{
			switch (type)
			{
				OV_CASE_RETURN_ENUM_STRING(VideoPacketType, SequenceStart);
				OV_CASE_RETURN_ENUM_STRING(VideoPacketType, CodedFrames);
				OV_CASE_RETURN_ENUM_STRING(VideoPacketType, SequenceEnd);
				OV_CASE_RETURN_ENUM_STRING(VideoPacketType, CodedFramesX);
				OV_CASE_RETURN_ENUM_STRING(VideoPacketType, Metadata);
				OV_CASE_RETURN_ENUM_STRING(VideoPacketType, MPEG2TSSequenceStart);
				OV_CASE_RETURN_ENUM_STRING(VideoPacketType, Multitrack);
				OV_CASE_RETURN_ENUM_STRING(VideoPacketType, ModEx);
			}

			return "(Unknown)";
		}

		enum class VideoPacketModExType : uint8_t
		{
			TimestampOffsetNano = 0,

			// ...
			// 14 - reserved
			// 15 - reserved
		};
		constexpr const char *EnumToString(VideoPacketModExType type)
		{
			switch (type)
			{
				OV_CASE_RETURN_ENUM_STRING(VideoPacketModExType, TimestampOffsetNano);
			}

			return "(Unknown)";
		}

		enum class VideoFourCc : fourcc_t
		{
			//
			// Valid FOURCC values for signaling support of video codecs
			// in the enhanced FourCC pipeline. In this context, support
			// for a FourCC codec MUST be signaled via the enhanced
			// "connect" command.
			//
			Vp8 = ov::MakeFourCc("vp08"),
			Vp9 = ov::MakeFourCc("vp09"),
			Av1 = ov::MakeFourCc("av01"),
			Avc = ov::MakeFourCc("avc1"),
			Hevc = ov::MakeFourCc("hvc1"),
		};
		constexpr const char *EnumToString(VideoFourCc fourcc)
		{
			switch (fourcc)
			{
				OV_CASE_RETURN_ENUM_STRING(VideoFourCc, Vp8);
				OV_CASE_RETURN_ENUM_STRING(VideoFourCc, Vp9);
				OV_CASE_RETURN_ENUM_STRING(VideoFourCc, Av1);
				OV_CASE_RETURN_ENUM_STRING(VideoFourCc, Avc);
				OV_CASE_RETURN_ENUM_STRING(VideoFourCc, Hevc);
			}

			return "(Unknown)";
		}
		inline const char *FourCcToString(VideoFourCc fourcc)
		{
			return ov::FourCcToString(static_cast<fourcc_t>(fourcc));
		}

		//--------------------------------------------------------------------
		// Related to Audio
		//--------------------------------------------------------------------
		enum class SoundFormat : uint8_t
		{
			LPcmPlatformEndian = 0,
			AdPcm = 1,
			Mp3 = 2,
			LPcmLittleEndian = 3,
			Nellymoser16KMono = 4,
			Nellymoser8KMono = 5,
			Nellymoser = 6,
			G711ALaw = 7,
			G711MuLaw = 8,
			ExHeader = 9,  // new, used to signal FOURCC mode
			Aac = 10,
			Speex = 11,
			// 12 - reserved
			// 13 - reserved
			Mp3_8K = 14,
			Native = 15,  // Device specific sound
		};
		constexpr const char *EnumToString(SoundFormat format)
		{
			switch (format)
			{
				OV_CASE_RETURN_ENUM_STRING(SoundFormat, LPcmPlatformEndian);
				OV_CASE_RETURN_ENUM_STRING(SoundFormat, AdPcm);
				OV_CASE_RETURN_ENUM_STRING(SoundFormat, Mp3);
				OV_CASE_RETURN_ENUM_STRING(SoundFormat, LPcmLittleEndian);
				OV_CASE_RETURN_ENUM_STRING(SoundFormat, Nellymoser16KMono);
				OV_CASE_RETURN_ENUM_STRING(SoundFormat, Nellymoser8KMono);
				OV_CASE_RETURN_ENUM_STRING(SoundFormat, Nellymoser);
				OV_CASE_RETURN_ENUM_STRING(SoundFormat, G711ALaw);
				OV_CASE_RETURN_ENUM_STRING(SoundFormat, G711MuLaw);
				OV_CASE_RETURN_ENUM_STRING(SoundFormat, ExHeader);
				OV_CASE_RETURN_ENUM_STRING(SoundFormat, Aac);
				OV_CASE_RETURN_ENUM_STRING(SoundFormat, Speex);
				OV_CASE_RETURN_ENUM_STRING(SoundFormat, Mp3_8K);
				OV_CASE_RETURN_ENUM_STRING(SoundFormat, Native);
			}

			return "(Unknown)";
		}
		bool IsValidSoundFormat(flv::SoundFormat format);

		enum class SoundRate : uint8_t
		{
			_5_5 = 0,  // 5.5 kHZ
			_11 = 1,   // 11 KHZ
			_22 = 2,   // 22 KHZ
			_44 = 3	   // 44 kHZ, AAC always 3
		};
		constexpr const char *EnumToString(SoundRate rate)
		{
			switch (rate)
			{
				OV_CASE_RETURN(SoundRate::_5_5, "5.5 kHZ");
				OV_CASE_RETURN(SoundRate::_11, "11 kHZ");
				OV_CASE_RETURN(SoundRate::_22, "22 kHZ");
				OV_CASE_RETURN(SoundRate::_44, "44 kHZ");
			}

			return "(Unknown)";
		}

		enum class SoundSize : uint8_t
		{
			_8Bit = 0,	 // 8-bit samples
			_16Bit = 1,	 // 16-bit samples
		};
		constexpr const char *EnumToString(SoundSize size)
		{
			switch (size)
			{
				OV_CASE_RETURN(SoundSize::_8Bit, "8-bit");
				OV_CASE_RETURN(SoundSize::_16Bit, "16-bit");
			}

			return "(Unknown)";
		}

		enum class SoundType : uint8_t
		{
			Mono = 0,
			Stereo = 1	// AAC always 1
		};
		constexpr const char *EnumToString(SoundType type)
		{
			switch (type)
			{
				OV_CASE_RETURN_ENUM_STRING(SoundType, Mono);
				OV_CASE_RETURN_ENUM_STRING(SoundType, Stereo);
			}

			return "(Unknown)";
		}

		enum class AACPacketType : uint8_t
		{
			SequenceHeader = 0,	 // AudioSpecificConfig (ISO 14496-3 1.6.2)
			Raw = 1
		};
		constexpr const char *EnumToString(AACPacketType type)
		{
			switch (type)
			{
				OV_CASE_RETURN_ENUM_STRING(AACPacketType, SequenceHeader);
				OV_CASE_RETURN_ENUM_STRING(AACPacketType, Raw);
			}

			return "(Unknown)";
		}

		enum class AudioPacketType : uint8_t
		{
			SequenceStart = 0,
			CodedFrames = 1,

			// RTMP includes a previously undocumented "audio silence" message.
			// This silence message is identified when an audio message contains
			// a zero-length payload, or more precisely, an empty audio message
			// without an AudioTagHeader, indicating a period of silence. The
			// action to take after receiving a silence message is system
			// dependent. The semantics of the silence message in the Flash
			// Media playback and timing model are as follows:
			//
			// - Ensure all buffered audio data is played out before entering the
			//	 silence period:
			//	 Make sure that any audio data currently in the buffer is fully
			//	 processed and played. This ensures a clean transition into the
			//	 silence period without cutting off any audio.
			//
			// - After playing all buffered audio data, flush the audio decoder:
			//	 Clear the audio decoder to reset its state and prepare it for new
			//	 input after the silence period.
			//
			// - During the silence period, the audio clock can't be used as the
			//	 master clock for synchronizing playback:
			//	 Switch to using the system's wall-clock time to maintain the correct
			//	 timing for video and other data streams.
			//
			// - Don't wait for audio frames for synchronized A+V playback:
			//	 Normally, audio frames drive the synchronization of audio and video
			//	 (A/V) playback. During the silence period, playback should not stall
			//	 waiting for audio frames. Video and other data streams should
			//	 continue to play based on the wall-clock time, ensuring smooth
			//	 playback without audio.
			//
			// AudioPacketType.SequenceEnd is to have no less than the same meaning as
			// a silence message. While it may seem redundant, we need to introduce
			// this enum to ensure we can signal the end of the audio sequence for any
			// audio track.
			SequenceEnd = 2,

			//	3 - Reserved

			MultichannelConfig = 4,

			// Turns on audio multitrack mode
			Multitrack = 5,

			// 6 - reserved

			// ModEx is a special signal within the AudioPacketType enum that
			// serves to both modify and extend the behavior of the current packet.
			// When this signal is encountered, it indicates the presence of
			// additional modifiers or extensions, requiring further processing to
			// adjust or augment the packet's functionality. ModEx can be used to
			// introduce new capabilities or modify existing ones, such as
			// enabling support for high-precision timestamps or other advanced
			// features that enhance the base packet structure.
			ModEx = 7,

			// ...
			// 14 - reserved
			// 15 - reserved
		};
		constexpr const char *EnumToString(AudioPacketType type)
		{
			switch (type)
			{
				OV_CASE_RETURN_ENUM_STRING(AudioPacketType, SequenceStart);
				OV_CASE_RETURN_ENUM_STRING(AudioPacketType, CodedFrames);
				OV_CASE_RETURN_ENUM_STRING(AudioPacketType, SequenceEnd);
				OV_CASE_RETURN_ENUM_STRING(AudioPacketType, MultichannelConfig);
				OV_CASE_RETURN_ENUM_STRING(AudioPacketType, Multitrack);
				OV_CASE_RETURN_ENUM_STRING(AudioPacketType, ModEx);
			}

			return "(Unknown)";
		}

		enum class AudioPacketModExType : uint8_t
		{
			TimestampOffsetNano = 0,

			// ...
			// 14 - reserved
			// 15 - reserved
		};
		constexpr const char *EnumToString(AudioPacketModExType type)
		{
			switch (type)
			{
				OV_CASE_RETURN_ENUM_STRING(AudioPacketModExType, TimestampOffsetNano);
			}

			return "(Unknown)";
		}

		enum class AudioFourCc : fourcc_t
		{
			//
			// Valid FOURCC values for signaling support of audio codecs
			// in the enhanced FourCC pipeline. In this context, support
			// for a FourCC codec MUST be signaled via the enhanced
			// "connect" command.
			//

			// AC-3/E-AC-3 - <https://en.wikipedia.org/wiki/Dolby_Digital>
			Ac3 = ov::MakeFourCc("ac-3"),
			Eac3 = ov::MakeFourCc("ec-3"),

			// Opus audio - <https://opus-codec.org/>
			Opus = ov::MakeFourCc("Opus"),

			// Mp3 audio - <https://en.wikipedia.org/wiki/MP3>
			Mp3 = ov::MakeFourCc(".mp3"),

			// Free Lossless Audio Codec - <https://xiph.org/flac/format.html>
			Flac = ov::MakeFourCc("fLaC"),

			// Advanced Audio Coding - <https://en.wikipedia.org/wiki/Advanced_Audio_Coding>
			// The following AAC profiles, denoted by their object types, are supported
			// 1 = main profile
			// 2 = low complexity, a.k.a., LC
			// 5 = high efficiency / scale band replication, a.k.a., HE / SBR
			Aac = ov::MakeFourCc("mp4a"),
		};
		inline const char *EnumToString(AudioFourCc fourcc)
		{
			switch (fourcc)
			{
				OV_CASE_RETURN_ENUM_STRING(AudioFourCc, Ac3);
				OV_CASE_RETURN_ENUM_STRING(AudioFourCc, Eac3);
				OV_CASE_RETURN_ENUM_STRING(AudioFourCc, Opus);
				OV_CASE_RETURN_ENUM_STRING(AudioFourCc, Mp3);
				OV_CASE_RETURN_ENUM_STRING(AudioFourCc, Flac);
				OV_CASE_RETURN_ENUM_STRING(AudioFourCc, Aac);
			}

			return "(Unknown)";
		}
		inline const char *FourCcToString(AudioFourCc fourcc)
		{
			return ov::FourCcToString(static_cast<fourcc_t>(fourcc));
		}

		enum class AudioChannelOrder : uint8_t
		{
			//
			// Only the channel count is specified, without any further information
			// about the channel order
			//
			Unspecified = 0,

			//
			// The native channel order (i.e., the channels are in the same order in
			// which as defined in the AudioChannel enum).
			//
			Native = 1,

			//
			// The channel order does not correspond to any predefined
			// order and is stored as an explicit map.
			//
			Custom = 2

			//	3 - Reserved
			// ...
			// 15 - reserved
		};
		constexpr const char *EnumToString(AudioChannelOrder order)
		{
			switch (order)
			{
				OV_CASE_RETURN_ENUM_STRING(AudioChannelOrder, Unspecified);
				OV_CASE_RETURN_ENUM_STRING(AudioChannelOrder, Native);
				OV_CASE_RETURN_ENUM_STRING(AudioChannelOrder, Custom);
			}

			return "(Unknown)";
		}

		enum class AudioChannelMask : uint32_t
		{
			//
			// Mask used to indicate which channels are present in the stream.
			//

			// masks for commonly used speaker configurations
			// <https://en.wikipedia.org/wiki/Surround_sound#Standard_speaker_channels>
			FrontLeft = 0x000001,
			FrontRight = 0x000002,
			FrontCenter = 0x000004,
			LowFrequency1 = 0x000008,
			BackLeft = 0x000010,
			BackRight = 0x000020,
			FrontLeftCenter = 0x000040,
			FrontRightCenter = 0x000080,
			BackCenter = 0x000100,
			SideLeft = 0x000200,
			SideRight = 0x000400,
			TopCenter = 0x000800,
			TopFrontLeft = 0x001000,
			TopFrontCenter = 0x002000,
			TopFrontRight = 0x004000,
			TopBackLeft = 0x008000,
			TopBackCenter = 0x010000,
			TopBackRight = 0x020000,

			// Completes 22.2 multichannel audio, as
			// standardized in SMPTE ST2036-2-2008
			// see - <https://en.wikipedia.org/wiki/22.2_surround_sound>
			LowFrequency2 = 0x040000,
			TopSideLeft = 0x080000,
			TopSideRight = 0x100000,
			BottomFrontCenter = 0x200000,
			BottomFrontLeft = 0x400000,
			BottomFrontRight = 0x800000,
		};
		constexpr const char *EnumToString(AudioChannelMask mask)
		{
			switch (mask)
			{
				OV_CASE_RETURN_ENUM_STRING(AudioChannelMask, FrontLeft);
				OV_CASE_RETURN_ENUM_STRING(AudioChannelMask, FrontRight);
				OV_CASE_RETURN_ENUM_STRING(AudioChannelMask, FrontCenter);
				OV_CASE_RETURN_ENUM_STRING(AudioChannelMask, LowFrequency1);
				OV_CASE_RETURN_ENUM_STRING(AudioChannelMask, BackLeft);
				OV_CASE_RETURN_ENUM_STRING(AudioChannelMask, BackRight);
				OV_CASE_RETURN_ENUM_STRING(AudioChannelMask, FrontLeftCenter);
				OV_CASE_RETURN_ENUM_STRING(AudioChannelMask, FrontRightCenter);
				OV_CASE_RETURN_ENUM_STRING(AudioChannelMask, BackCenter);
				OV_CASE_RETURN_ENUM_STRING(AudioChannelMask, SideLeft);
				OV_CASE_RETURN_ENUM_STRING(AudioChannelMask, SideRight);
				OV_CASE_RETURN_ENUM_STRING(AudioChannelMask, TopCenter);
				OV_CASE_RETURN_ENUM_STRING(AudioChannelMask, TopFrontLeft);
				OV_CASE_RETURN_ENUM_STRING(AudioChannelMask, TopFrontCenter);
				OV_CASE_RETURN_ENUM_STRING(AudioChannelMask, TopFrontRight);
				OV_CASE_RETURN_ENUM_STRING(AudioChannelMask, TopBackLeft);
				OV_CASE_RETURN_ENUM_STRING(AudioChannelMask, TopBackCenter);
				OV_CASE_RETURN_ENUM_STRING(AudioChannelMask, TopBackRight);

				OV_CASE_RETURN_ENUM_STRING(AudioChannelMask, LowFrequency2);
				OV_CASE_RETURN_ENUM_STRING(AudioChannelMask, TopSideLeft);
				OV_CASE_RETURN_ENUM_STRING(AudioChannelMask, TopSideRight);
				OV_CASE_RETURN_ENUM_STRING(AudioChannelMask, BottomFrontCenter);
				OV_CASE_RETURN_ENUM_STRING(AudioChannelMask, BottomFrontLeft);
				OV_CASE_RETURN_ENUM_STRING(AudioChannelMask, BottomFrontRight);
			}

			return "(Unknown)";
		}

		enum class AudioChannel : uint8_t
		{
			//
			// Channel mappings enums
			//

			// commonly used speaker configurations
			// see - <https://en.wikipedia.org/wiki/Surround_sound#Standard_speaker_channels>
			FrontLeft = 0,	// i.e., FrontLeft is assigned to channel zero
			FrontRight,
			FrontCenter,
			LowFrequency1,
			BackLeft,
			BackRight,
			FrontLeftCenter,
			FrontRightCenter,
			BackCenter = 8,
			SideLeft,
			SideRight,
			TopCenter,
			TopFrontLeft,
			TopFrontCenter,
			TopFrontRight,
			TopBackLeft,
			TopBackCenter = 16,
			TopBackRight,

			// mappings to complete 22.2 multichannel audio, as
			// standardized in SMPTE ST2036-2-2008
			// see - <https://en.wikipedia.org/wiki/22.2_surround_sound>
			LowFrequency2 = 18,
			TopSideLeft,
			TopSideRight,
			BottomFrontCenter,
			BottomFrontLeft,
			BottomFrontRight = 23,

			//	 24 - Reserved
			// ...
			// 0xfd - reserved

			// Channel is empty and can be safely skipped.
			Unused = 0xfe,

			// Channel contains data, but its speaker configuration is unknown.
			Unknown = 0xff,
		};
		constexpr const char *EnumToString(AudioChannel channel)
		{
			switch (channel)
			{
				OV_CASE_RETURN_ENUM_STRING(AudioChannel, FrontLeft);
				OV_CASE_RETURN_ENUM_STRING(AudioChannel, FrontRight);
				OV_CASE_RETURN_ENUM_STRING(AudioChannel, FrontCenter);
				OV_CASE_RETURN_ENUM_STRING(AudioChannel, LowFrequency1);
				OV_CASE_RETURN_ENUM_STRING(AudioChannel, BackLeft);
				OV_CASE_RETURN_ENUM_STRING(AudioChannel, BackRight);
				OV_CASE_RETURN_ENUM_STRING(AudioChannel, FrontLeftCenter);
				OV_CASE_RETURN_ENUM_STRING(AudioChannel, FrontRightCenter);
				OV_CASE_RETURN_ENUM_STRING(AudioChannel, BackCenter);
				OV_CASE_RETURN_ENUM_STRING(AudioChannel, SideLeft);
				OV_CASE_RETURN_ENUM_STRING(AudioChannel, SideRight);
				OV_CASE_RETURN_ENUM_STRING(AudioChannel, TopCenter);
				OV_CASE_RETURN_ENUM_STRING(AudioChannel, TopFrontLeft);
				OV_CASE_RETURN_ENUM_STRING(AudioChannel, TopFrontCenter);
				OV_CASE_RETURN_ENUM_STRING(AudioChannel, TopFrontRight);
				OV_CASE_RETURN_ENUM_STRING(AudioChannel, TopBackLeft);
				OV_CASE_RETURN_ENUM_STRING(AudioChannel, TopBackCenter);
				OV_CASE_RETURN_ENUM_STRING(AudioChannel, TopBackRight);

				OV_CASE_RETURN_ENUM_STRING(AudioChannel, LowFrequency2);
				OV_CASE_RETURN_ENUM_STRING(AudioChannel, TopSideLeft);
				OV_CASE_RETURN_ENUM_STRING(AudioChannel, TopSideRight);
				OV_CASE_RETURN_ENUM_STRING(AudioChannel, BottomFrontCenter);
				OV_CASE_RETURN_ENUM_STRING(AudioChannel, BottomFrontLeft);
				OV_CASE_RETURN_ENUM_STRING(AudioChannel, BottomFrontRight);

				OV_CASE_RETURN_ENUM_STRING(AudioChannel, Unused);

				OV_CASE_RETURN_ENUM_STRING(AudioChannel, Unknown);
			}

			return "(Unknown)";
		}
	}  // namespace flv
}  // namespace modules
