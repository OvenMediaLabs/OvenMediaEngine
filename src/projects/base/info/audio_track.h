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
#include <shared_mutex>

#include "base/common_types.h"

class AudioTrack
{
public:
	AudioTrack();

	void SetSampleRate(int32_t samplerate);
	void SetSampleFormat(cmn::AudioSample::Format format);
	int32_t GetSampleRate() const;
	const cmn::AudioSample &GetSample() const;

	void SetChannel(cmn::AudioChannel channel);
	void SetChannelLayout(cmn::AudioChannel::Layout channel_layout);
	void SetChannelCount(uint32_t channel_count);
	const cmn::AudioChannel &GetChannel() const;
	bool IsValidChannel() const;


	void SetAudioSamplesPerFrame(int nbsamples);
	int GetAudioSamplesPerFrame() const;

protected:
	mutable std::shared_mutex _amutex;	

	// sample format, sample rate
	cmn::AudioSample _sample;

	cmn::AudioChannel _channel_layout;

	// time_scale
	double _audio_timescale;

	// Sample Count per frame
	int _audio_samples_per_frame;
};