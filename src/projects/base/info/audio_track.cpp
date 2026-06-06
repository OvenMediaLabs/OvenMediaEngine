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
	ov::ScopedLock lock(_audio_mutex);
	_channel_layout.SetLayout(AudioChannel::Layout::LayoutUnknown);

	// The default frame size of the audio frame is fixed to 1024. 
	// If the frame size is different, settings must be made in an audio encoder or provider.
	_audio_samples_per_frame = 1024;
}

void AudioTrack::SetSampleRate(int32_t sample_rate)
{
	ov::ScopedLock lock(_audio_mutex);
	_sample.SetRate((AudioSample::Rate)sample_rate);
}

void AudioTrack::SetSampleFormat(AudioSample::Format format)
{
	ov::ScopedLock lock(_audio_mutex);
	_sample.SetFormat(format);
}

int32_t AudioTrack::GetSampleRate() const
{
	ov::SharedLockGuard lock(_audio_mutex);
	return ov::ToUnderlyingType(_sample.GetRate());
}

AudioSample AudioTrack::GetSample() const
{
	ov::SharedLockGuard lock(_audio_mutex);
	return _sample;
}


AudioChannel AudioTrack::GetChannel() const
{
	ov::SharedLockGuard lock(_audio_mutex);
	return _channel_layout;
}

void AudioTrack::SetChannel(AudioChannel channel)
{
	ov::ScopedLock lock(_audio_mutex);
	_channel_layout = channel;
}

void AudioTrack::SetChannelCount(uint32_t channel_count)
{
	ov::ScopedLock lock(_audio_mutex);

	switch (channel_count)
	{
		case 1:
			_channel_layout.SetLayout(AudioChannel::Layout::LayoutMono);
			break;
		case 2:
			_channel_layout.SetLayout(AudioChannel::Layout::LayoutStereo);
			break;
		default:
			_channel_layout.SetLayout(AudioChannel::Layout::LayoutUnknown);
			break;
	}
}

void AudioTrack::SetChannelLayout(AudioChannel::Layout channel_layout)
{
	ov::ScopedLock lock(_audio_mutex);
	_channel_layout.SetLayout(channel_layout);
}

bool AudioTrack::IsValidChannel() const
{
	ov::SharedLockGuard lock(_audio_mutex);
	return _channel_layout.IsValid();
}

void AudioTrack::SetAudioSamplesPerFrame(int nbsamples)
{
	_audio_samples_per_frame = nbsamples;
}

int AudioTrack::GetAudioSamplesPerFrame() const
{
	return _audio_samples_per_frame;
}
