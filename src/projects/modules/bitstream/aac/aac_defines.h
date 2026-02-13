#pragma once

#include <base/ovlibrary/ovlibrary.h>

// Table 1.3 – Audio Profiles definition
// @see ISO/IEC 14496-3 (2009), Information technology - Coding of audio-visual objects - Part 3: Audio
enum class AudioObjectType : uint8_t
{
	Null = 0,							  // Null
	AacMain = 1,						  // Main
	AacLc = 2,							  // Low Complexity
	AacSsr = 3,							  // Scalable Sample Rate
	AacLtp = 4,							  // Long Term Predictor
	Sbr = 5,							  // SBR Spectral Band Replication
	AacScalable = 6,					  // AAC Scalable
	Twinvq = 7,							  // Twin VQ Vector Quantizer
	Celp = 8,							  // Code Excited Linear Prediction
	Hvxc = 9,							  // Harmonic Vector eXcitation Coding
	Reserved10 = 10,					  // (reserved)
	Reserved11 = 11,					  // (reserved)
	Ttsi = 12,							  // Text to Speech Interface
	MainSynthetic = 13,					  // Main Synthetic
	WavetableSynthesis = 14,			  // Wavetable Synthesis
	GeneralMidi = 15,					  // General MIDI
	AlgorithmicSynthesisAndAudioFx = 16,  // Algorithmic Synthesis and Audio FX
	ErAacLc = 17,						  // Error Resilient (ER) AAC Low Complexity (LC)
	Reserved18 = 18,					  // (reserved)
	ErAacLtp = 19,						  // Error Resilient (ER) AAC Long Term Predictor (LTP)
	ErAacScalable = 20,					  // Error Resilient (ER) AAC scalable
	ErTwinvq = 21,						  // Error Resilient (ER) TwinVQ
	ErBsac = 22,						  // Error Resilient (ER) Bit Sliced Arithmetic Coding
	ErAacLd = 23,						  // Error Resilient (ER) AAC Low Delay
	ErCelp = 24,						  // Error Resilient (ER) Code Excited Linear Prediction
	ErHvxc = 25,						  // Error Resilient (ER) Harmonic Vector eXcitation Coding
	ErHiln = 26,						  // Error Resilient (ER) Harmonic and Individual Lines plus Noise
	ErParametric = 27,					  // Error Resilient (ER) Parametric
	Ssc = 28,							  // SinuSoidal Coding
	Ps = 29,							  // Parametric Stereo
	MpegSurround = 30,					  // MPEG Surround
	Escape = 31,						  // (escape)
	Layer1 = 32,						  // Layer-1 Audio
	Layer2 = 33,						  // Layer-2 Audio
	Layer3 = 34,						  // Layer-3 Audio
	Dst = 35,							  // Direct Stream Transfer
	Als = 36,							  // Audio Lossless Coding
	Sls = 37,							  // Scalable Lossless Coding
	SlsNonCore = 38,					  // Scalable Lossless Non-Core Audio
	ErAacEld = 39,						  // Error Resilient (ER) AAC Enhanced Low Delay
	SmrSimple = 40,						  // Symbolic Music Representation Simple
	SmrMain = 41,						  // Symbolic Music Representation Main
};
const char *StringFromAudioObjectType(AudioObjectType object_type);

enum class AacProfile : uint8_t
{
	Reserved = 3,

	// @see 7.1 Profiles, aac-iso-13818-7.pdf, page 40
	Main = 0,
	LC = 1,
	SSR = 2
};

// Table 1.18 – Sampling Frequency Index
//
// | samplingFrequencyIndex | Value        |
// +------------------------+--------------|
// | 0x0                    | 96000        |
// | 0x1                    | 88200        |
// | 0x2                    | 64000        |
// | 0x3                    | 48000        |
// | 0x4                    | 44100        |
// | 0x5                    | 32000        |
// | 0x6                    | 24000        |
// | 0x7                    | 22050        |
// | 0x8                    | 16000        |
// | 0x9                    | 12000        |
// | 0xa                    | 11025        |
// | 0xb                    | 8000         |
// | 0xc                    | 7350         |
// | 0xd                    | reserved     |
// | 0xe                    | reserved     |
// | 0xf                    | escape value |
//
enum class AacSamplingFrequencies : uint8_t
{
	_96000 = 0,
	_88200 = 1,
	_64000 = 2,
	_48000 = 3,
	_44100 = 4,
	_32000 = 5,
	_24000 = 6,
	_22050 = 7,
	_16000 = 8,
	_12000 = 9,
	_11025 = 10,
	_8000 = 11,
	_7350 = 12,
	RESERVED1 = 13,
	RESERVED2 = 14,
	ESCAPE_VALUE = 15
};
uint32_t GetAacSamplingFrequency(AacSamplingFrequencies sampling_frequency_index);
