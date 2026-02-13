#include "aac_defines.h"

#define OBJECT_TYPE_DESC_CASE(value, description) \
	case AudioObjectType::value:                  \
		return description

const char *StringFromAudioObjectType(AudioObjectType object_type)
{
	switch (object_type)
	{
		OBJECT_TYPE_DESC_CASE(Null, "Null");
		OBJECT_TYPE_DESC_CASE(AacMain, "AAC main");
		OBJECT_TYPE_DESC_CASE(AacLc, "AAC LC");
		OBJECT_TYPE_DESC_CASE(AacSsr, "AAC SSR");
		OBJECT_TYPE_DESC_CASE(AacLtp, "AAC LTP");
		OBJECT_TYPE_DESC_CASE(Sbr, "SBR");
		OBJECT_TYPE_DESC_CASE(AacScalable, "AAC Scalable");
		OBJECT_TYPE_DESC_CASE(Twinvq, "TwinVQ");
		OBJECT_TYPE_DESC_CASE(Celp, "CELP");
		OBJECT_TYPE_DESC_CASE(Hvxc, "HVXC");
		OBJECT_TYPE_DESC_CASE(Reserved10, "(reserved-10)");
		OBJECT_TYPE_DESC_CASE(Reserved11, "(reserved-11)");
		OBJECT_TYPE_DESC_CASE(Ttsi, "TTSI");
		OBJECT_TYPE_DESC_CASE(MainSynthetic, "Main synthetic");
		OBJECT_TYPE_DESC_CASE(WavetableSynthesis, "Wavetable synthesis");
		OBJECT_TYPE_DESC_CASE(GeneralMidi, "General MIDI");
		OBJECT_TYPE_DESC_CASE(AlgorithmicSynthesisAndAudioFx, "Algorithmic Synthesis and Audio FX");
		OBJECT_TYPE_DESC_CASE(ErAacLc, "ER AAC LC");
		OBJECT_TYPE_DESC_CASE(Reserved18, "(reserved-18)");
		OBJECT_TYPE_DESC_CASE(ErAacLtp, "ER AAC LTP");
		OBJECT_TYPE_DESC_CASE(ErAacScalable, "ER AAC Scalable");
		OBJECT_TYPE_DESC_CASE(ErTwinvq, "ER TwinVQ");
		OBJECT_TYPE_DESC_CASE(ErBsac, "ER BSAC");
		OBJECT_TYPE_DESC_CASE(ErAacLd, "ER AAC LD");
		OBJECT_TYPE_DESC_CASE(ErCelp, "ER CELP");
		OBJECT_TYPE_DESC_CASE(ErHvxc, "ER HVXC");
		OBJECT_TYPE_DESC_CASE(ErHiln, "ER HILN");
		OBJECT_TYPE_DESC_CASE(ErParametric, "ER Parametric");
		OBJECT_TYPE_DESC_CASE(Ssc, "SSC");
		OBJECT_TYPE_DESC_CASE(Ps, "PS");
		OBJECT_TYPE_DESC_CASE(MpegSurround, "MPEG Surround");
		OBJECT_TYPE_DESC_CASE(Escape, "(escape)");
		OBJECT_TYPE_DESC_CASE(Layer1, "Layer-1");
		OBJECT_TYPE_DESC_CASE(Layer2, "Layer-2");
		OBJECT_TYPE_DESC_CASE(Layer3, "Layer-3");
		OBJECT_TYPE_DESC_CASE(Dst, "DST");
		OBJECT_TYPE_DESC_CASE(Als, "ALS");
		OBJECT_TYPE_DESC_CASE(Sls, "SLS");
		OBJECT_TYPE_DESC_CASE(SlsNonCore, "SLS non-core");
		OBJECT_TYPE_DESC_CASE(ErAacEld, "ER AAC ELD");
		OBJECT_TYPE_DESC_CASE(SmrSimple, "SMR Simple");
		OBJECT_TYPE_DESC_CASE(SmrMain, "SMR Main");
	}

	return "Unknown";
}

#define _SAMPLE_RATE_CASE(rate, value) \
	case AacSamplingFrequencies::rate: \
		return value;

uint32_t GetAacSamplingFrequency(AacSamplingFrequencies sampling_frequency_index)
{
	switch (sampling_frequency_index)
	{
		_SAMPLE_RATE_CASE(_96000, 96000);
		_SAMPLE_RATE_CASE(_88200, 88200);
		_SAMPLE_RATE_CASE(_64000, 64000);
		_SAMPLE_RATE_CASE(_48000, 48000);
		_SAMPLE_RATE_CASE(_44100, 44100);
		_SAMPLE_RATE_CASE(_32000, 32000);
		_SAMPLE_RATE_CASE(_24000, 24000);
		_SAMPLE_RATE_CASE(_22050, 22050);
		_SAMPLE_RATE_CASE(_16000, 16000);
		_SAMPLE_RATE_CASE(_12000, 12000);
		_SAMPLE_RATE_CASE(_11025, 11025);
		_SAMPLE_RATE_CASE(_8000, 8000);
		_SAMPLE_RATE_CASE(_7350, 7350);
		case AacSamplingFrequencies::RESERVED1:
			[[fallthrough]];
		case AacSamplingFrequencies::RESERVED2:
			break;

		case AacSamplingFrequencies::ESCAPE_VALUE:
			// ESCAPE_VALUE must be handled in the if statement above
			OV_ASSERT2(false);
			break;
	}

	return 0;
}
