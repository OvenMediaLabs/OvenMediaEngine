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
#include "base/ovlibrary/tsa/mutex.h"

class AudioTrack
{
public:
	AudioTrack();

	void SetSampleRate(int32_t samplerate);
	void SetSampleFormat(cmn::AudioSample::Format format);
	int32_t GetSampleRate() const;
	cmn::AudioSample GetSample() const;

	void SetChannel(cmn::AudioChannel channel);
	void SetChannelLayout(cmn::AudioChannel::Layout channel_layout);
	void SetChannelCount(uint32_t channel_count);
	cmn::AudioChannel GetChannel() const;
	bool IsValidChannel() const;


	void SetAudioSamplesPerFrame(int nbsamples);
	int GetAudioSamplesPerFrame() const;

protected:
	mutable ov::SharedMutex _audio_mutex;	

	// sample format, sample rate
	cmn::AudioSample _sample OV_GUARDED_BY(_audio_mutex);

	cmn::AudioChannel _channel_layout OV_GUARDED_BY(_audio_mutex);

	// time_scale
	std::atomic<double> _audio_timescale;

	// Sample Count per frame
	std::atomic<int> _audio_samples_per_frame;
};