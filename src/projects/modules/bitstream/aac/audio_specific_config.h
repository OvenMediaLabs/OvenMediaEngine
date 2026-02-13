#pragma once

// The Audio Specific Config is the global header for MPEG-4 Audio:
//////////////////////////////////////////////////////////////////////////////////////////
// Pseudo Code
//////////////////////////////////////////////////////////////////////////////////////////
// AudioSpecificConfig ()
// {
// 	audioObjectType; 5 bslbf
// 	samplingFrequencyIndex; 4 bslbf
// 	if ( samplingFrequencyIndex==0xf )
// 	samplingFrequency; 24 uimsbf
// 	channelConfiguration; 4 bslbf
// 	if ( audioObjectType == 1 || audioObjectType == 2 || audioObjectType == 3 || audioObjectType == 4 || audioObjectType == 6 || audioObjectType == 7 )
// 		GASpecificConfig();
// 	if ( audioObjectType == 8 )
// 		CelpSpecificConfig();
// 	if ( audioObjectType == 9 )
// 		HvxcSpecificConfig();
// 	if ( audioObjectType == 12 )
// 		TTSSpecificConfig();
// 	if ( audioObjectType == 13 || audioObjectType == 14 || audioObjectType == 15 || audioObjectType==16)
// 		StructuredAudioSpecificConfig();
// 	if ( audioObjectType == 17 || audioObjectType == 19 || audioObjectType == 20 || audioObjectType == 21 || audioObjectType == 22 ||
// 		 audioObjectType == 23 )
// 		GASpecificConfig();
// 	if ( audioObjectType == 24)
// 		ErrorResilientCelpSpecificConfig();
// 	if ( audioObjectType == 25)
// 		ErrorResilientHvxcSpecificConfig();
// 	if ( audioObjectType == 26 || audioObjectType == 27)
// 		ParametricSpecificConfig();
// 	if ( audioObjectType == 17 || audioObjectType == 19 || audioObjectType == 20 || audioObjectType == 21 || audioObjectType == 22 ||
// 		 audioObjectType == 23 || audioObjectType == 24 || audioObjectType == 25 ||	audioObjectType == 26 || audioObjectType == 27 )
// 	{
// 		epConfig; 2 bslbf
// 		if ( epConfig == 2 || epConfig == 3 )
// 		{
// 			ErrorProtectionSpecificConfig();
// 		}

// 		//ISO/IEC 14496-3:2001(E)	22 © ISO/IEC 2001— All rights reserved
// 		if ( epConfig == 3 ) {
// 			directMapping; 1 bslbf
// 			if ( ! directMapping ) {
// 				/* tbd */
// 			}
// 		}
// 	}
// }
// GASpecificConfig ( samplingFrequencyIndex, channelConfiguration, audioObjectType )
// {
// 	FrameLength; 1 bslbf
// 	DependsOnCoreCoder; 1 bslbf
// 	if ( dependsOnCoreCoder )
//	{
// 		coreCoderDelay; 14 uimsbf
// 	}
// 	ExtensionFlag 1 bslbf
// 	if ( ! ChannelConfiguration )
//	{
// 		program_config_element ();
// 	}
// 	if ( extensionFlag )
//	{
// 		if ( AudioObjectType==22 )
//		{
// 			numOfSubFrame 5 bslbf
// 			layer_length 11 bslbf
// 		}
// 		If(AudioObjectType==17 || AudioObjectType == 18 || AudioObjectType == 19 || AudioObjectType == 20 || AudioObjectType == 21 ||
// 		   AudioObjectType == 23)
//		{
// 			AacSectionDataResilienceFlag; 1 bslbf
// 			AacScalefactorDataResilienceFlag; 1 bslbf
// 			AacSpectralDataResilienceFlag; 1 bslbf
// 		}
// 		extensionFlag3; 1 bslbf
// 		if ( extensionFlag3 ) {
// 			/* tbd in version 3 */
// 		}
// 	}
// }
//
//////////////////////////////////////////////////////////////////////////////////////////

#include <base/info/decoder_configuration_record.h>
#include <base/ovlibrary/ovlibrary.h>

#include "aac_defines.h"

#define MIN_AAC_SPECIFIC_CONFIG_SIZE 2

// Based on ISO14496-3
class AudioSpecificConfig : public DecoderConfigurationRecord
{
public:
	bool IsValid() const override;
	// Instance can be initialized by putting raw data in AudioSpecificConfig.
	bool Parse(const std::shared_ptr<const ov::Data> &data) override;
	bool Equals(const std::shared_ptr<DecoderConfigurationRecord> &other) override;

	std::shared_ptr<const ov::Data> Serialize() override;

	AudioObjectType ObjectType() const;
	AacSamplingFrequencies SamplingFrequencyIndex() const;
	AacSamplingFrequencies ProbeAacSamplingFrequencyIndex() const;
	uint32_t Samplerate() const;
	uint8_t Channel() const;

	uint32_t FrameLength() const;

	AacProfile GetAacProfile() const;
	ov::String GetInfoString() const;

	void SetObjectType(AudioObjectType object_type);
	void SetSamplingFrequencyIndex(AacSamplingFrequencies sampling_frequency_index);
	void SetChannel(uint8_t channel);

	// Helpers
	ov::String GetCodecsParameter() const override;

protected:
	// Table 1.16 - Syntax of GetAudioObjectType()
	bool GetAudioObjectType(BitReader &reader, AudioObjectType &audio_object_type) const;

	uint32_t CalculateFrameLength(AudioObjectType audio_object_type, bool frame_length_flag) const;

	// Table 4.1 - Syntax of GASpecificConfig()
	bool GASpecificConfig(BitReader &reader);

private:
	AudioObjectType _audio_object_type = AudioObjectType::Null;								  // 5 bits
	AacSamplingFrequencies _sampling_frequency_index = AacSamplingFrequencies::ESCAPE_VALUE;  // 4 bits
	AacSamplingFrequencies _probed_sampling_frequency_index = AacSamplingFrequencies::ESCAPE_VALUE;
	uint32_t _sampling_frequency = 0;  // 24 bits

	// 0 : Defined in AOT Specifc Config
	// 1 : 1 channel: front-center
	// 2 : 2 channels: front-left, front-right
	// 3 : 3 channels: front-center, front-left, front-right
	// 4 : 4 channels: front-center, front-left, front-right, back-center
	// ...
	// 8-15 : Reserved
	uint8_t _channel_configuration = 15;  // 4 bits

	uint32_t _frame_length = 0;
};
