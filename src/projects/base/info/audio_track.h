//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <modules/bitstream/aac/audio_specific_config.h>

#include "base/common_types.h"

class AudioTrack
{
public:
	AudioTrack();

	void SetSampleRate(int32_t samplerate);
	int32_t GetSampleRate() const;

	cmn::AudioSample &GetSample();
	const cmn::AudioSample &GetSample() const;

	void SetChannel(cmn::AudioChannel channel);
	cmn::AudioChannel &GetChannel();
	const cmn::AudioChannel &GetChannel() const;

	void SetAudioSamplesPerFrame(int nbsamples);
	int GetAudioSamplesPerFrame() const;

protected:
	// sample format, sample rate
	cmn::AudioSample _sample;

	// channel layout
	cmn::AudioChannel _channel_layout;

	// time_scale
	double _audio_timescale;

	// Sample Count per frame
	int _audio_samples_per_frame;
};