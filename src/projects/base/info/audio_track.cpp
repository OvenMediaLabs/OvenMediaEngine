//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "audio_track.h"

using namespace cmn;

AudioTrack::AudioTrack()
{
	_channel_layout.SetLayout(AudioChannel::Layout::LayoutUnknown);

	// The default frame size of the audio frame is fixed to 1024. 
	// If the frame size is different, settings must be made in an audio encoder or provider.
	_audio_samples_per_frame = 1024;
}

void AudioTrack::SetSampleRate(int32_t sample_rate)
{
	_sample.SetRate((AudioSample::Rate)sample_rate);
}

int32_t AudioTrack::GetSampleRate() const
{
	return (int32_t)_sample.GetRate();
}

AudioSample &AudioTrack::GetSample()
{
	return _sample;
}

const AudioSample &AudioTrack::GetSample() const
{
	return _sample;
}

AudioChannel &AudioTrack::GetChannel()
{
	return _channel_layout;
}

const AudioChannel &AudioTrack::GetChannel() const
{
	return _channel_layout;
}

void AudioTrack::SetChannel(AudioChannel channel)
{
	_channel_layout = channel;
}

void AudioTrack::SetAudioSamplesPerFrame(int nbsamples)
{
	_audio_samples_per_frame = nbsamples;
}

int AudioTrack::GetAudioSamplesPerFrame() const
{
	return _audio_samples_per_frame;
}
