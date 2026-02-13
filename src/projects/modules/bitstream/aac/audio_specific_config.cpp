#include "audio_specific_config.h"

#include <base/ovlibrary/bit_reader.h>
#include <base/ovlibrary/ovlibrary.h>

#include "base/ovlibrary/memory_view.h"

#define OV_LOG_TAG "AACSpecificConfig"

#define _SET_PROBED_FREQUENCY_INDEX(frequency, max, min, index)           \
	if ((max > frequency) && (frequency >= min))                          \
	{                                                                     \
		_probed_sampling_frequency_index = AacSamplingFrequencies::index; \
		break;                                                            \
	}

#define _RETURN_IF_FAIL(expression) \
	if (expression == false)        \
	{                               \
		return false;               \
	}

bool AudioSpecificConfig::IsValid() const
{
	return (_audio_object_type != AudioObjectType::Null) &&
		   ((_sampling_frequency_index < AacSamplingFrequencies::RESERVED1) || (_probed_sampling_frequency_index < AacSamplingFrequencies::RESERVED1)) &&
		   (_channel_configuration < 15);
}

ov::String AudioSpecificConfig::GetCodecsParameter() const
{
	// "mp4a.oo[.A]"
	//   oo = OTI = [Object Type Indication]
	//   A = [Object Type Number]
	//
	// OTI == 40 (Audio ISO/IEC 14496-3)
	//   http://mp4ra.org/#/object_types
	//
	// OTN == profile_number
	//   https://developer.mozilla.org/en-US/docs/Web/Media/Formats/codecs_parameter#MPEG-4_audio
	return ov::String::FormatString("mp4a.40.%d", static_cast<int>(_audio_object_type));
}

bool AudioSpecificConfig::GetAudioObjectType(BitReader &reader, AudioObjectType &audio_object_type) const
{
	// Table 1.16 - Syntax of GetAudioObjectType()
	// GetAudioObjectType()
	// {
	//     audioObjectType; 5 uimsbf
	//     if (audioObjectType == 31) {
	//         audioObjectType = 32 + audioObjectTypeExt; 6 uimsbf
	//     }
	//     return audioObjectType;
	// }
	_RETURN_IF_FAIL(reader.ReadBits<AudioObjectType>(5, audio_object_type));
	if (audio_object_type == AudioObjectType::Escape)
	{
		uint8_t audio_object_type_ext;
		_RETURN_IF_FAIL(reader.ReadBits<uint8_t>(6, audio_object_type_ext));

		audio_object_type = static_cast<AudioObjectType>(32 + audio_object_type_ext);
	}

	return true;
}

uint32_t AudioSpecificConfig::CalculateFrameLength(AudioObjectType audio_object_type, bool frame_length_flag) const
{
	// frameLengthFlag
	//
	// Length of the frame, number of spectral lines, respective.
	// For all General Audio Object Types except AAC SSR and ER AAC LD:
	// If set to "0" a 1024/128 lines IMDCT is used and frameLength is set to
	// 1024, if set to "1" a 960/120 line IMDCT is used and frameLength is set
	// to 960.
	// For ER AAC LD: If set to "0" a 512 lines IMDCT is used and
	// frameLength is set to 512, if set to "1" a 480 line IMDCT is used and
	// frameLength is set to 480.
	// For AAC SSR: Must be set to "0". A 256/32 lines IMDCT (first or second value)
	// is distinguished by the value of window_sequence.
	switch (audio_object_type)
	{
		// For all General Audio Object Types except AAC SSR and ER AAC LD
		default:
			return frame_length_flag ? 960 : 1024;

		// For ER AAC LD
		case AudioObjectType::ErAacLd:
			return frame_length_flag ? 480 : 512;

		// For AAC SSR
		case AudioObjectType::AacSsr:
			OV_ASSERT(frame_length_flag == 0, "AAC SSR must be set to 0");

			// TODO: need to parse window_sequence
#ifdef DEBUG
			logtw("AAC SSR is not supported yet");
#endif	// DEBUG

			return 256;
	}
}

bool AudioSpecificConfig::GASpecificConfig(BitReader &reader)
{
	// Table 4.1 - Syntax of GASpecificConfig()
	//
	// GASpecificConfig (samplingFrequencyIndex,
	//                   channelConfiguration,
	//                   audioObjectType)
	//
	// {
	//     frameLengthFlag; 1 bslbf
	//     dependsOnCoreCoder; 1 bslbf
	//     if (dependsOnCoreCoder) {
	//         coreCoderDelay; 14 uimsbf
	//     }
	//     extensionFlag; 1 bslbf
	//     if (! channelConfiguration) {
	//         program_config_element ();
	//     }
	//     if ((audioObjectType == 6) || (audioObjectType == 20)) {
	//         layerNr; 3 uimsbf
	//     }
	//     if (extensionFlag) {
	//         if (audioObjectType == 22) {
	//             numOfSubFrame; 5 bslbf
	//             layer_length; 11 bslbf
	//         }
	//         if (audioObjectType == 17 || audioObjectType == 19 ||
	//         audioObjectType == 20 || audioObjectType == 23) {
	//             aacSectionDataResilienceFlag; 1 bslbf
	//             aacScalefactorDataResilienceFlag; 1 bslbf
	//             aacSpectralDataResilienceFlag; 1 bslbf
	//         }
	//         extensionFlag3; 1 bslbf
	//         if (extensionFlag3) {
	//             /* tbd in version 3 */
	//         }
	//     }
	// }

	bool frame_length_flag;
	_RETURN_IF_FAIL(reader.ReadBit(frame_length_flag));

	_frame_length = CalculateFrameLength(_audio_object_type, frame_length_flag);

	uint8_t depends_on_core_coder;
	_RETURN_IF_FAIL(reader.ReadBits(1, depends_on_core_coder));

	if (depends_on_core_coder)
	{
		[[maybe_unused]] uint16_t core_coder_delay;
		_RETURN_IF_FAIL(reader.ReadBits(14, core_coder_delay));
	}

	uint8_t extension_flag;
	_RETURN_IF_FAIL(reader.ReadBits(1, extension_flag));

	if (_channel_configuration == 0)
	{
		// Not implemented
		// program_config_element();
	}

	if (
		(_audio_object_type == AudioObjectType::AacScalable) ||
		(_audio_object_type == AudioObjectType::ErAacScalable))
	{
		[[maybe_unused]] uint8_t layer_nr;
		_RETURN_IF_FAIL(reader.ReadBits(3, layer_nr));
	}

	if (extension_flag)
	{
		if (_audio_object_type == AudioObjectType::ErBsac)
		{
			[[maybe_unused]] uint8_t num_of_sub_frame;
			_RETURN_IF_FAIL(reader.ReadBits(5, num_of_sub_frame));

			[[maybe_unused]] uint16_t layer_length;
			_RETURN_IF_FAIL(reader.ReadBits(11, layer_length));
		}
		else if ((_audio_object_type == AudioObjectType::ErAacLc) ||
				 (_audio_object_type == AudioObjectType::ErAacLtp) ||
				 (_audio_object_type == AudioObjectType::ErAacScalable) ||
				 (_audio_object_type == AudioObjectType::ErAacLd))
		{
			uint8_t aac_section_data_resilience_flag;
			_RETURN_IF_FAIL(reader.ReadBits(1, aac_section_data_resilience_flag));

			uint8_t aac_scalefactor_data_resilience_flag;
			_RETURN_IF_FAIL(reader.ReadBits(1, aac_scalefactor_data_resilience_flag));

			uint8_t aac_spectral_data_resilience_flag;
			_RETURN_IF_FAIL(reader.ReadBits(1, aac_spectral_data_resilience_flag));
		}

		uint8_t extension_flag3;
		_RETURN_IF_FAIL(reader.ReadBits(1, extension_flag3));

		if (extension_flag3)
		{
			// /* tbd in version 3 */
		}
	}

	return true;
}

bool AudioSpecificConfig::Parse(const std::shared_ptr<const ov::Data> &data)
{
	if (data->GetLength() < MIN_AAC_SPECIFIC_CONFIG_SIZE)
	{
		logte("The data inputted is too small for parsing (%d must be bigger than %d)", data->GetLength(), MIN_AAC_SPECIFIC_CONFIG_SIZE);
		return false;
	}

	BitReader reader(data->GetDataAs<uint8_t>(), data->GetLength());

	// audioObjectType = GetAudioObjectType();
	if (GetAudioObjectType(reader, _audio_object_type) == false)
	{
		return false;
	}

	// samplingFrequencyIndex; 4 bslbf
	// if ( samplingFrequencyIndex == 0xf ) {
	//     samplingFrequency; 24 uimsbf
	// }
	_RETURN_IF_FAIL(reader.ReadBits(4, _sampling_frequency_index));

	if (_sampling_frequency_index == AacSamplingFrequencies::ESCAPE_VALUE)
	{
		_RETURN_IF_FAIL(reader.ReadBits(24, _sampling_frequency));

		do
		{
			// Table 4.82 - Sampling frequency mapping
			//
			// +-------------------------+-------------------------------------------+
			// | Frequency range (in Hz) | Use tables for sampling frequency (in Hz) |
			// +-------------------------+-------------------------------------------+
			// |            f >= 92017   |                  96000                    |
			// |    92017 > f >= 75132   |                  88200                    |
			// |    75132 > f >= 55426   |                  64000                    |
			// |    55426 > f >= 46009   |                  48000                    |
			// |    46009 > f >= 37566   |                  44100                    |
			// |    37566 > f >= 27713   |                  32000                    |
			// |    27713 > f >= 23004   |                  24000                    |
			// |    23004 > f >= 18783   |                  22050                    |
			// |    18783 > f >= 13856   |                  16000                    |
			// |    13856 > f >= 11502   |                  12000                    |
			// |    11502 > f >= 9391    |                  11025                    |
			// |     9391 > f            |                  8000                     |
			// +-------------------------+-------------------------------------------+
			_SET_PROBED_FREQUENCY_INDEX(_sampling_frequency, UINT32_MAX, 92017, _96000);
			_SET_PROBED_FREQUENCY_INDEX(_sampling_frequency, 92016, 75132, _88200);
			_SET_PROBED_FREQUENCY_INDEX(_sampling_frequency, 75131, 55426, _64000);
			_SET_PROBED_FREQUENCY_INDEX(_sampling_frequency, 55425, 46009, _48000);
			_SET_PROBED_FREQUENCY_INDEX(_sampling_frequency, 46008, 37566, _44100);
			_SET_PROBED_FREQUENCY_INDEX(_sampling_frequency, 37565, 27713, _32000);
			_SET_PROBED_FREQUENCY_INDEX(_sampling_frequency, 27712, 23004, _24000);
			_SET_PROBED_FREQUENCY_INDEX(_sampling_frequency, 23003, 18783, _22050);
			_SET_PROBED_FREQUENCY_INDEX(_sampling_frequency, 18782, 13856, _16000);
			_SET_PROBED_FREQUENCY_INDEX(_sampling_frequency, 13855, 11502, _12000);
			_SET_PROBED_FREQUENCY_INDEX(_sampling_frequency, 11501, 9391, _11025);
			_SET_PROBED_FREQUENCY_INDEX(_sampling_frequency, 9390, 0, _8000);
		} while (false);
	}
	else
	{
		SetSamplingFrequencyIndex(_sampling_frequency_index);
	}

	//  channelConfiguration; 4 bslbf
	_RETURN_IF_FAIL(reader.ReadBits(4, _channel_configuration));
	// sbrPresentFlag = -1;
	// psPresentFlag = -1;
	// if ( audioObjectType == 5 ||
	//     audioObjectType == 29 ) {
	//     extensionAudioObjectType = 5;
	//     sbrPresentFlag = 1;
	//     if ( audioObjectType == 29 ) {
	//         psPresentFlag = 1;
	//     }
	//     extensionSamplingFrequencyIndex; 4 uimsbf
	//     if ( extensionSamplingFrequencyIndex == 0xf )
	//         extensionSamplingFrequency; 24 uimsbf
	//     audioObjectType = GetAudioObjectType();
	//     if ( audioObjectType == 22 )
	//         extensionChannelConfiguration; 4 uimsbf
	// }
	// else {
	//     extensionAudioObjectType = 0;
	// }
	// int32_t sbrPresentFlag = -1;
	// int32_t psPresentFlag = -1;
	// AudioObjectType extensionAudioObjectType;

	if ((_audio_object_type == AudioObjectType::Sbr) || (_audio_object_type == AudioObjectType::Ps))
	{
		// extensionAudioObjectType = AudioObjectType::Sbr;
		// sbrPresentFlag = 1;
		// if (audioObjectType == AudioObjectType::Ps) {
		//     psPresentFlag = 1;
		// }

		AacSamplingFrequencies extensionSamplingFrequencyIndex;
		_RETURN_IF_FAIL(reader.ReadBits(4, extensionSamplingFrequencyIndex));
		if (extensionSamplingFrequencyIndex == AacSamplingFrequencies::ESCAPE_VALUE)
		{
			[[maybe_unused]] uint32_t extensionSamplingFrequency;
			_RETURN_IF_FAIL(reader.ReadBits(24, _sampling_frequency));
		}

		_RETURN_IF_FAIL(GetAudioObjectType(reader, _audio_object_type));
		if (_audio_object_type == AudioObjectType::ErBsac)
		{
			[[maybe_unused]] uint8_t extensionChannelConfiguration;
			_RETURN_IF_FAIL(reader.ReadBits(4, extensionChannelConfiguration));
		}
	}
	else
	{
		// extensionAudioObjectType = AudioObjectType::Null;
	}

	// switch (audioObjectType) {
	//     case 1:
	//     case 2:
	//     case 3:
	//     case 4:
	//     case 6:
	//     case 7:
	//     case 17:
	//     case 19:
	//     case 20:
	//     case 21:
	//     case 22:
	//     case 23:
	//
	//     GASpecificConfig();
	//     break:
	switch (_audio_object_type)
	{
		case AudioObjectType::AacMain:
			[[fallthrough]];
		case AudioObjectType::AacLc:
			[[fallthrough]];
		case AudioObjectType::AacSsr:
			[[fallthrough]];
		case AudioObjectType::AacLtp:
			[[fallthrough]];
		case AudioObjectType::AacScalable:
			[[fallthrough]];
		case AudioObjectType::Twinvq:
			[[fallthrough]];
		case AudioObjectType::ErAacLc:
			[[fallthrough]];
		case AudioObjectType::ErAacLtp:
			[[fallthrough]];
		case AudioObjectType::ErAacScalable:
			[[fallthrough]];
		case AudioObjectType::ErTwinvq:
			[[fallthrough]];
		case AudioObjectType::ErBsac:
			[[fallthrough]];
		case AudioObjectType::ErAacLd:
			GASpecificConfig(reader);
			break;

		default:
			break;
	}

	// Set serialized data
	SetData(data->Clone());

	return true;
}

bool AudioSpecificConfig::Equals(const std::shared_ptr<DecoderConfigurationRecord> &other)
{
	if (other == nullptr)
	{
		return false;
	}

	auto other_config = std::dynamic_pointer_cast<AudioSpecificConfig>(other);
	if (other_config == nullptr)
	{
		return false;
	}

	if (ObjectType() != other_config->ObjectType())
	{
		return false;
	}

	if (SamplingFrequencyIndex() != other_config->SamplingFrequencyIndex())
	{
		return false;
	}

	if (Channel() != other_config->Channel())
	{
		return false;
	}

	return true;
}

// This is not used after Parse() is called. It is only used when values are set with Setter.
std::shared_ptr<const ov::Data> AudioSpecificConfig::Serialize()
{
	ov::BitWriter bits(2);

	bits.WriteBits(5, ov::ToUnderlyingType(_audio_object_type));
	bits.WriteBits(4, ov::ToUnderlyingType(_sampling_frequency_index));
	bits.WriteBits(4, _channel_configuration);
	// frame_length : only support 1024
	bits.WriteBits(1, 0);
	// dependsOnCoreCoder
	bits.WriteBits(1, 0);
	// extensionFlag
	bits.WriteBits(1, 0);

	return std::make_shared<ov::Data>(bits.GetData(), bits.GetDataSize());
}

AudioObjectType AudioSpecificConfig::ObjectType() const
{
	return _audio_object_type;
}

void AudioSpecificConfig::SetObjectType(AudioObjectType object_type)
{
	_audio_object_type = object_type;
}

uint32_t AudioSpecificConfig::Samplerate() const
{
	return _sampling_frequency;
}

AacSamplingFrequencies AudioSpecificConfig::SamplingFrequencyIndex() const
{
	return _sampling_frequency_index;
}

AacSamplingFrequencies AudioSpecificConfig::ProbeAacSamplingFrequencyIndex() const
{
	// If _sampling_frequency_index is valid, return it.
	if (_sampling_frequency_index < AacSamplingFrequencies::RESERVED1)
	{
		return _sampling_frequency_index;
	}

	// Return the probed frequency index
	return _probed_sampling_frequency_index;
}

void AudioSpecificConfig::SetSamplingFrequencyIndex(AacSamplingFrequencies sampling_frequency_index)
{
	_sampling_frequency_index = sampling_frequency_index;
	_probed_sampling_frequency_index = AacSamplingFrequencies::ESCAPE_VALUE;
	_sampling_frequency = GetAacSamplingFrequency(_sampling_frequency_index);
}

uint8_t AudioSpecificConfig::Channel() const
{
	return _channel_configuration;
}

void AudioSpecificConfig::SetChannel(uint8_t channel)
{
	_channel_configuration = channel;
}

uint32_t AudioSpecificConfig::FrameLength() const
{
	return _frame_length;
}

AacProfile AudioSpecificConfig::GetAacProfile() const
{
	switch (_audio_object_type)
	{
		case AudioObjectType::AacMain:
			return AacProfile::Main;

		case AudioObjectType::AacLc:
		case AudioObjectType::Sbr:
		case AudioObjectType::Ps:
			return AacProfile::LC;

		case AudioObjectType::AacSsr:
			return AacProfile::SSR;

		default:
			return AacProfile::Reserved;
	}
}

ov::String AudioSpecificConfig::GetInfoString() const
{
	ov::String out_str = ov::String::FormatString("\n[AudioSpecificConfig]\n");

	out_str.AppendFormat("\tObjectType(%d, %s)\n", ObjectType(), StringFromAudioObjectType(ObjectType()));
	out_str.AppendFormat("\tSamplingFrequency(%d)\n", SamplingFrequencyIndex());
	out_str.AppendFormat("\tChannel(%d)\n", Channel());

	return out_str;
}
