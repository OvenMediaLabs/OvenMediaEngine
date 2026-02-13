//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./flv_audio_parser.h"

#include <modules/bitstream/aac/audio_specific_config.h>

#include "./flv_private.h"

namespace modules
{
	namespace flv
	{
		bool IsValidSoundFormat(flv::SoundFormat format)
		{
			switch (format)
			{
				OV_CASE_RETURN(flv::SoundFormat::LPcmPlatformEndian, true);
				OV_CASE_RETURN(flv::SoundFormat::AdPcm, true);
				OV_CASE_RETURN(flv::SoundFormat::Mp3, true);
				OV_CASE_RETURN(flv::SoundFormat::LPcmLittleEndian, true);
				OV_CASE_RETURN(flv::SoundFormat::Nellymoser16KMono, true);
				OV_CASE_RETURN(flv::SoundFormat::Nellymoser8KMono, true);
				OV_CASE_RETURN(flv::SoundFormat::Nellymoser, true);
				OV_CASE_RETURN(flv::SoundFormat::G711ALaw, true);
				OV_CASE_RETURN(flv::SoundFormat::G711MuLaw, true);
				OV_CASE_RETURN(flv::SoundFormat::ExHeader, true);
				OV_CASE_RETURN(flv::SoundFormat::Aac, true);
				OV_CASE_RETURN(flv::SoundFormat::Speex, true);
				OV_CASE_RETURN(flv::SoundFormat::Mp3_8K, true);
				OV_CASE_RETURN(flv::SoundFormat::Native, true);
			}

			return false;
		}

		bool AudioParser::ParseLegacyAAC(ov::BitReader &reader, const std::shared_ptr<AudioData> &audio_data)
		{
			audio_data->aac_packet_type = reader.ReadU8As<AACPacketType>();
			logtp("[legacy AAC] aacPacketType (8 bits): %s (%d, 0x%02X)",
				  EnumToString(audio_data->aac_packet_type), audio_data->aac_packet_type, audio_data->aac_packet_type);
			audio_data->payload = GetPayload(false, reader, 0, 0);
			logtp("[legacy AAC] payload size: %zu bytes", audio_data->payload->GetLength());

			if (audio_data->aac_packet_type == AACPacketType::SequenceHeader)
			{
				logtp("[legacy AAC] SequenceHeader found, parsing DecoderConfigurationRecord");

				auto header = std::make_shared<AudioSpecificConfig>();

				if (header->Parse(audio_data->payload) == false)
				{
					logte("[legacy AAC] Failed to parse AudioSpecificConfig");
					return false;
				}

				audio_data->header		= header;
				audio_data->header_data = audio_data->payload;
			}

			return true;
		}

		std::shared_ptr<AudioData> AudioParser::ProcessExAudioTagHeader(ov::BitReader &reader, SoundFormat sound_format, bool *process_audio_body)
		{
			auto audio_data				  = std::make_shared<AudioData>(_default_track_id, sound_format, true);

			*process_audio_body			  = true;

			// Interpret UB[4] bits as AudioPacketType instead of sound rate, size, and type.
			// audioPacketType = UB[4] as AudioPacketType	// at byte boundary after this read
			audio_data->audio_packet_type = reader.ReadAs<AudioPacketType>(4);
			logtp("audioPacketType (4 bits): %s (%d, 0x%04X)",
				  EnumToString(audio_data->audio_packet_type.value()),
				  audio_data->audio_packet_type.value(),
				  audio_data->audio_packet_type.value());

			uint32_t mod_ex_data_size					= 0;
			std::shared_ptr<const ov::Data> mod_ex_data = nullptr;

			// Process each ModEx data packet
			while (audio_data->audio_packet_type == AudioPacketType::ModEx)
			{
				// Determine the size of the packet ModEx data (ranging from 1 to 256 bytes)
				// modExDataSize = UI8 + 1
				mod_ex_data_size = reader.ReadU8() + 1;
				logtp("modExDataSize (8 bits): %u (0x%02X + 1)", mod_ex_data_size, (mod_ex_data_size - 1));

				// If maximum 8-bit size is not sufficient, use a 16-bit value
				if (mod_ex_data_size == 256)
				{
					// modExDataSize = UI16 + 1;
					logtp("modExDataSize (16 bits): %u (0x%04X + 1)", mod_ex_data_size, (mod_ex_data_size - 1));
				}

				// Fetch the packet ModEx data based on its determined size
				// modExData = UI8[modExDataSize]
				mod_ex_data = reader.ReadBytes(mod_ex_data_size);

				// fetch the AudioPacketModExType
				// audioPacketModExType = UB[4] as AudioPacketModExType
				{
					auto audio_packet_mod_ex_type		 = reader.ReadAs<AudioPacketModExType>(4);
					audio_data->audio_packet_mod_ex_type = audio_packet_mod_ex_type;
					logtp("audioPacketModExType (4 bits): %s (%d, 0x%01X)", EnumToString(audio_packet_mod_ex_type), audio_packet_mod_ex_type, audio_packet_mod_ex_type);
				}

				// Update audioPacketType
				// audioPacketType = UB[4] as AudioPacketType	// at byte boundary after this read
				audio_data->audio_packet_type = reader.ReadAs<AudioPacketType>(4);

				if (audio_data->audio_packet_mod_ex_type == AudioPacketModExType::TimestampOffsetNano)
				{
					// This block processes TimestampOffsetNano to enhance RTMP timescale
					// accuracy and compatibility with formats like MP4, M2TS, and Safari's
					// Media Source Extensions. It ensures precise synchronization without
					// altering core RTMP timestamps, applying only to the current media
					// message. These adjustments enhance synchronization and timing
					// accuracy in media messages while preserving the core RTMP timestamp
					// integrity.
					//
					// NOTE:
					// - 1 millisecond (ms) = 1,000,000 nanoseconds (ns).
					// - Maximum value representable with 20 bits is 1,048,575 ns
					//   (just over 1 ms), allowing precise sub-millisecond adjustments.
					// - modExData must be at least 3 bytes, storing values up to 999,999 ns.
					// audioTimestampNanoOffset = bytesToUI24(modExData)
					{
						auto audio_timestamp_nano_offset		= ov::BE24ToHost(reader.ReadU24());
						audio_data->audio_timestamp_nano_offset = audio_timestamp_nano_offset;
						logtp("audioTimestampNanoOffset (24 bits): %u (0x%06X)", audio_timestamp_nano_offset, audio_timestamp_nano_offset);
					}

					// TODO: Integrate this nanosecond offset into timestamp management
					// to accurately adjust the presentation time.
				}
			}

			if (audio_data->audio_packet_type == AudioPacketType::Multitrack)
			{
				_is_multitrack = true;
				logtp("isMultiTrack: true");
				// audioMultitrackType = UB[4] as AvMultitrackType
				_multitrack_type = reader.ReadAs<AvMultitrackType>(4);
				logtp("audioMultitrackType (4 bits): %s (%d, 0x%01X)", EnumToString(_multitrack_type), _multitrack_type, _multitrack_type);

				// Fetch AudioPacketType for all audio tracks in the audio message.
				// This fetch MUST not result in a AudioPacketType.Multitrack
				// audioPacketType = UB[4] as AudioPacketType
				audio_data->audio_packet_type = reader.ReadAs<AudioPacketType>(4);

				if (_multitrack_type != AvMultitrackType::ManyTracksManyCodecs)
				{
					// The tracks are encoded with the same codec. Fetch the FOURCC for them
					// audioFourCc = FOURCC as AudioFourCc
					{
						auto audio_fourcc		 = reader.ReadU32BEAs<AudioFourCc>();
						audio_data->audio_fourcc = audio_fourcc;
						logtp("audioFourCc (32 bits): %s (0x%08X)", FourCcToString(audio_fourcc), audio_fourcc);
					}
				}
			}
			else
			{
				// audioFourCc = FOURCC as AudioFourCc
				auto audio_fourcc		 = reader.ReadU32BEAs<AudioFourCc>();
				audio_data->audio_fourcc = audio_fourcc;
				logtp("audioFourCc (32 bits): %s (0x%08X)", FourCcToString(audio_fourcc), audio_fourcc);
			}

			return audio_data;
		}

		std::shared_ptr<AudioData> AudioParser::ProcessExAudioTagBody(ov::BitReader &reader, bool process_audio_body, std::shared_ptr<AudioData> audio_data)
		{
			uint24_t size_of_audio_track					   = 0;
			[[maybe_unused]] size_t size_of_audio_track_offset = 0;

			while (process_audio_body)
			{
				if (_is_multitrack)
				{
					if (_multitrack_type == AvMultitrackType::ManyTracksManyCodecs)
					{
						// Each track has a codec assigned to it. Fetch the FOURCC for the next track.
						// audioFourCc = FOURCC as AudioFourCc
						auto audio_fourcc		 = reader.ReadU32BEAs<AudioFourCc>();
						audio_data->audio_fourcc = audio_fourcc;
						logtp("[MultiTrack/ManyTracksManyCodecs] audioFourCc (32 bits): %s (0x%08X)", FourCcToString(audio_fourcc), audio_fourcc);
					}

					// Track Ordering:
					//
					// For identifying the highest priority (a.k.a., default track)
					// or highest quality track, it is RECOMMENDED to use trackId
					// set to zero. For tracks of lesser priority or quality, use
					// multiple instances of trackId with ascending numerical values.
					// The concept of priority or quality can have multiple
					// interpretations, including but not limited to bitrate,
					// resolution, default angle, and language. This recommendation
					// serves as a guideline intended to standardize track numbering
					// across various applications.
					// audioTrackId = UI8
					{
						auto audio_track_id	 = reader.ReadU8();
						audio_data->track_id = audio_track_id;
						logtp("[MultiTrack] audioTrackId (8 bits): %u", audio_track_id);
					}

					if (_multitrack_type != AvMultitrackType::OneTrack)
					{
						// The `sizeOfAudioTrack` specifies the size in bytes of the
						// current track that is being processed. This size starts
						// counting immediately after the position where the `sizeOfAudioTrack`
						// value is located. You can use this value as an offset to locate the
						// next audio track in a multitrack system. The data pointer is
						// positioned immediately after this field. Depending on the MultiTrack
						// type, the offset points to either a `fourCc` or a `trackId.`
						// sizeOfAudioTrack = UI24
						size_of_audio_track = reader.ReadU24BE();
						logtp("[MultiTrack] sizeOfAudioTrack (24 bits): %u", size_of_audio_track);

						// Record the offset of the sizeOfVideoTrack field
						// to calculate the size of payload
						size_of_audio_track_offset = reader.GetByteOffset();
					}
				}

				if (audio_data->audio_packet_type == AudioPacketType::MultichannelConfig)
				{
					//
					// Specify a speaker for a channel as it appears in the bitstream.
					// This is needed if the codec is not self-describing for channel mapping
					//

					// set audio channel order
					// audioChannelOrder = UI8 as AudioChannelOrder
					{
						auto audio_channel_order		= reader.ReadU8As<AudioChannelOrder>();
						audio_data->audio_channel_order = audio_channel_order;
						logtp("audioChannelOrder (8 bits): %s (%d, 0x%02X)",
							  EnumToString(audio_channel_order), audio_channel_order, audio_channel_order);
					}

					// number of channels
					// channelCount = UI8
					auto channel_count		  = reader.ReadU8();
					audio_data->channel_count = channel_count;

					if (audio_data->audio_channel_order == AudioChannelOrder::Custom)
					{
						// Each entry specifies the speaker layout (see AudioChannel enum above
						// for layout definition) in the order that it appears in the bitstream.
						// First entry (i.e., index 0) specifies the speaker layout for channel 1.
						// Subsequent entries specify the speaker layout for the next channels
						// (e.g., second entry for channel 2, third entry for channel 3, etc.).
						// audioChannelMapping = UI8[channelCount] as AudioChannel
						{
							auto audio_channel_mapping		  = reader.ReadAs<AudioChannel>(channel_count);
							audio_data->audio_channel_mapping = audio_channel_mapping;
							logtp("audioChannelMapping (%d bits): %s (%d, 0x%02X)",
								  channel_count * 8,
								  EnumToString(audio_channel_mapping), audio_channel_mapping, audio_channel_mapping);
						}
					}

					if (audio_data->audio_channel_order == AudioChannelOrder::Native)
					{
						// audioChannelFlags indicates which channels are present in the
						// multi-channel stream. You can perform a Bitwise AND
						// (i.e., audioChannelFlags & AudioChannelMask.xxx) to see if a
						// specific audio channel is present
						// audioChannelFlags = UI32
						audio_data->audio_channel_flags = reader.ReadU32BE();
						logtp("audioChannelFlags (32 bits): 0x%08X", audio_data->audio_channel_flags.value());
					}
				}

				if (audio_data->audio_packet_type == AudioPacketType::SequenceEnd)
				{
					// signals end of sequence
					logtp("[SequenceEnd]");
				}

				if (audio_data->audio_packet_type == AudioPacketType::SequenceStart)
				{
#if DEBUG
					auto current_bit_offset = reader.GetBitOffset();
#endif	// DEBUG
					if (audio_data->audio_fourcc == AudioFourCc::Aac)
					{
						// The AAC audio specific config (a.k.a., AacSequenceHeader) is
						// defined in ISO/IEC 14496-3.
						// aacHeader = [AacSequenceHeader]
						logtp("[SequenceStart] AacSequenceHeader");
					}

					if (audio_data->audio_fourcc == AudioFourCc::Flac)
					{
						// FlacSequenceHeader layout is:
						//
						// The bytes 0x66 0x4C 0x61 0x43 ("fLaC" in ASCII) signature
						//
						// Followed by a metadata block (called the STREAMINFO block) as described
						// in section 7 of the FLAC specification. The STREAMINFO block contains
						// information about the whole sequence, such as sample rate, number of
						// channels, total number of samples, etc. It MUST be present as the first
						// metadata block in the sequence. The FLAC audio specific bitstream format
						// is defined at <https://xiph.org/flac/format.html>
						// flacHeader = [FlacSequenceHeader]
						logtp("[SequenceStart] FlacSequenceHeader");
					}

					if (audio_data->audio_fourcc == AudioFourCc::Opus)
					{
						// Opus Sequence header (a.k.a., ID header):
						// - The Opus sequence start is also known as the ID header.
						// - It contains essential information needed to initialize
						//   the decoder and understand the stream format.
						// - For detailed structure, refer to RFC 7845, Section 5.1:
						//   <https://datatracker.ietf.org/doc/html/rfc7845#section-5.1>
						//
						// If the Opus sequence start payload is empty, use the
						// AudioPacketType.MultichannelConfig signal for channel
						// mapping when present; otherwise, default to mono/stereo mode.
						// opusHeader = [OpusSequenceHeader]
						logtp("[SequenceStart] OpusSequenceHeader");
					}

#if DEBUG
					[[maybe_unused]] auto used_bits = reader.GetBitOffset() - current_bit_offset;
					logtp("[SequenceStart] ConfigurationRecord: %zu bytes (%zu bits)", (used_bits + 7) / 8, used_bits);
#endif	// DEBUG
				}

				if (audio_data->audio_packet_type == AudioPacketType::CodedFrames)
				{
					if (audio_data->audio_fourcc == AudioFourCc::Ac3 || audio_data->audio_fourcc == AudioFourCc::Eac3)
					{
						// Body contains audio data as defined by the bitstream syntax
						// in the ATSC standard for Digital Audio Compression (AC-3, E-AC-3)
						// ac3Data = [Ac3CodedData]
						logtp("[CodedFrames] Ac3CodedData");
					}

					if (audio_data->audio_fourcc == AudioFourCc::Opus)
					{
						// Body contains Opus packets. The layout is one Opus
						// packet for each of N different streams, where N is
						// typically one for mono or stereo, but MAY be greater
						// than one for multichannel audio. The value N is
						// specified in the ID header (Opus sequence start) or
						// via the AudioPacketType.MultichannelConfig signal, and
						// is fixed over the entire length of the Opus sequence.
						// The first (N - 1) Opus packets, if any, are packed one
						// after another using the self-delimiting framing from
						// Appendix B of [RFC6716]. The remaining Opus packet is
						// packed at the end of the Ogg packet using the regular,
						// undelimited framing from Section 3 of [RFC6716]. All
						// of the Opus packets in a single audio packet MUST be
						// constrained to have the same duration.
						// opusData = [OpusCodedData]
						logtp("[CodedFrames] OpusCodedData");
					}

					if (audio_data->audio_fourcc == AudioFourCc::Mp3)
					{
						// An Mp3 audio stream is built up from a succession of smaller
						// parts called frames. Each frame is a data block with its own header
						// and audio information
						// mp3Data = [Mp3CodedData]
						logtp("[CodedFrames] Mp3CodedData");
					}

					if (audio_data->audio_fourcc == AudioFourCc::Aac)
					{
						// The AAC audio specific bitstream format is defined in ISO/IEC 14496-3.
						// aacData = [AacCodedData]
						logtp("[CodedFrames] AacCodedData");
					}

					if (audio_data->audio_fourcc == AudioFourCc::Flac)
					{
						// The audio data is composed of one or more audio frames. Each frame
						// consists of a frame header, which contains a sync code and information
						// about the frame, such as the block size, sample rate, number of
						// channels, et cetera. The Flac audio specific bitstream format
						// is defined at <https://xiph.org/flac/format.html>
						// flacData = [FlacCodedData]
						logtp("[CodedFrames] FlacCodedData");
					}
					else
					{
						// Not supported audio_packet_type
						return nullptr;
					}
				}

				if (
					_is_multitrack &&
					_multitrack_type != AvMultitrackType::OneTrack &&
					// positionDataPtrToNextAudioTrack(sizeOfAudioTrack)
					//
					// The position has been updated to the next audio track by the reader
					true)
				{
					_data_list.push_back(audio_data);

					audio_data = std::make_shared<AudioData>(_default_track_id, audio_data->sound_format, true);
					continue;
				}

				// done processing audio message
				break;
			}

			return audio_data;
		}

		bool AudioParser::Parse(ov::BitReader &reader)
		{
			try
			{
				// https://veovera.org/docs/enhanced/enhanced-rtmp-v2#enhanced-audio

				// soundFormat = UB[4] as SoundFormat
				auto sound_format = reader.ReadAs<SoundFormat>(4);

				_is_ex_header	  = (sound_format == SoundFormat::ExHeader);

				if (_is_ex_header)
				{
					logtp("Enhanced audio header found");

					bool process_audio_body;

					auto audio_data = ProcessExAudioTagHeader(reader, sound_format, &process_audio_body);

					audio_data		= ProcessExAudioTagBody(reader, process_audio_body, audio_data);

					if (audio_data == nullptr)
					{
						return false;
					}

					_data_list.push_back(audio_data);
				}
				else
				{
					logtp("Legacy audio header found");

					auto audio_data		   = std::make_shared<AudioData>(_default_track_id, sound_format, false);

					// See AudioTagHeader of the legacy [FLV] specification for for detailed format
					// of the four bits used for soundRate/soundSize/soundType
					//
					// NOTE: soundRate, soundSize and soundType formats have not changed.
					// if (soundFormat == SoundFormat.ExHeader) we switch into FOURCC audio mode
					// as defined below. This means that soundRate, soundSize and soundType
					// bits are not interpreted, instead the UB[4] bits are interpreted as an
					// AudioPacketType
					// soundRate = UB[2]
					// soundSize = UB[1]
					// soundType = UB[1]
					audio_data->sound_rate = reader.ReadAs<SoundRate>(2);
					audio_data->sound_size = reader.ReadAs<SoundSize>(1);
					audio_data->sound_type = reader.ReadAs<SoundType>(1);

					if (audio_data->sound_format != SoundFormat::Aac)
					{
						logtw("Sound format %d (%s) is not supported", audio_data->sound_format, EnumToString(audio_data->sound_format));
						return false;
					}

					// AAC
					ParseLegacyAAC(reader, audio_data);

					_data_list.push_back(audio_data);
				}

				return true;
			}
			catch (const ov::BitReaderError &e)
			{
				switch (static_cast<ov::BitReaderError::Code>(e.GetCode()))
				{
					case ov::BitReaderError::Code::NotEnoughBitsToRead:
						logtw("Not enough bits to read: %s - probably malformed audio data", e.GetMessage().CStr());
						break;

					case ov::BitReaderError::Code::NotEnoughSpaceInBuffer:
						// Internal server error
						logte("Not enough space in audio buffer: %s", e.GetMessage().CStr());
						OV_ASSERT2(false);
						break;

					case ov::BitReaderError::Code::FailedToReadBits:
						// Internal server error
						logte("Failed to read bits for audio: %s", e.GetMessage().CStr());
						OV_ASSERT2(false);
						break;
				}
			}

			return false;
		}
	}  // namespace flv
}  // namespace modules
