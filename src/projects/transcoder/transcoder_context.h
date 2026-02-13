//==============================================================================
//
//  TranscodeContext
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <stdint.h>

#include "base/mediarouter/media_type.h"
extern "C"
{
#include <libavformat/avformat.h>
}

class MediaFrame
{
public:
	MediaFrame() = default;
	~MediaFrame() {
		if(_priv_data)
		{
			av_frame_unref(_priv_data);
			av_frame_free(&_priv_data);
			_priv_data = nullptr;
		}
	}

	void SetSourceId(int32_t source_id)
	{
		_source_id = source_id;
	}

	int32_t GetSourceId() const
	{
		return _source_id;
	}

	void SetMediaType(cmn::MediaType media_type)
	{
		_media_type = media_type;
	}

	cmn::MediaType GetMediaType() const
	{
		return _media_type;
	}

	void SetTrackId(int32_t track_id)
	{
		_track_id = track_id;
	}

	int32_t GetTrackId() const
	{
		return _track_id;
	}

	int64_t GetPts() const
	{
		return _pts;
	}

	void SetPts(int64_t pts)
	{
		_pts = pts;

		if(_priv_data) {
			_priv_data->pts = pts;
			_priv_data->pkt_dts = pts;
		}
	}

	int64_t GetDuration() const
	{
		return _duration;
	}

	void SetDuration(int64_t duration)
	{
		_duration = duration;

		if (_priv_data)
		{
			_priv_data->pkt_duration = duration;
		}
	}

	void SetWidth(int32_t width)
	{
		_width = width;
	}

	int32_t GetWidth() const
	{
		return _width;
	}

	void SetHeight(int32_t height)
	{
		_height = height;
	}

	int32_t GetHeight() const
	{
		return _height;
	}

	void SetFormat(int32_t format)
	{
		_format = format;
	}

	int32_t GetFormat() const
	{
		return _format;
	}

	template <typename T>
	T GetFormat() const
	{
		return static_cast<T>(_format);
	}

	int32_t GetBytesPerSample() const
	{
		return _bytes_per_sample;
	}

	void SetBytesPerSample(int32_t bytes_per_sample)
	{
		_bytes_per_sample = bytes_per_sample;
	}

	int32_t GetNbSamples() const
	{
		return _nb_samples;
	}

	void SetNbSamples(int32_t nb_samples)
	{
		_nb_samples = nb_samples;

		if(_priv_data) {
			_priv_data->nb_samples = nb_samples;
		}
	}

	cmn::AudioChannel &GetChannels()
	{
		return _channels;
	}

	void SetChannels(cmn::AudioChannel channels)
	{
		_channels = channels;
	}

	uint32_t GetChannelCount() const
	{
		return _channels.GetCounts();
	}

	int32_t GetSampleRate() const
	{
		return _sample_rate;
	}

	void SetSampleRate(int32_t sample_rate)
	{
		_sample_rate = sample_rate;
	}

	void SetFlags(int32_t flags)
	{
		_flags = flags;
	}

	int32_t GetFlags() const
	{
		return _flags;
	}

	void FillZeroData()
	{
		if(!_priv_data) {
			return;
		}

		for (int i = 0; i < AV_NUM_DATA_POINTERS; i++)
		{
			if(_priv_data->linesize[i] > 0)
			{
				memset(_priv_data->data[i], 0, _priv_data->linesize[i]);
			}	
		}
	}

	void SetCodecDeviceId(cmn::DeviceId id)
	{
		_codec_device_id = id;
	}

	cmn::DeviceId GetCodecDeviceId() const
	{
		return _codec_device_id;
	}

	void SetCodecModuleId(cmn::MediaCodecModuleId id)
	{
		_codec_module_id = id;
	}

	cmn::MediaCodecModuleId GetCodecModuleId() const
	{
		return _codec_module_id;
	}


	// This function should only be called before filtering 
	std::shared_ptr<MediaFrame> CloneFrame(bool deep_copy = false)
	{
		auto frame = std::make_shared<MediaFrame>();

		if(_priv_data != nullptr)
		{
			// Create a new frame that references the same data as src
			auto clone_priv_data = ::av_frame_clone(_priv_data);
			if (clone_priv_data != nullptr)
			{
				if (deep_copy == true)
				{
					::av_frame_make_writable(clone_priv_data);
				}

				frame->SetPrivData(clone_priv_data);
			}
		}

		frame->SetMediaType(_media_type);
		frame->SetSourceId(_source_id);

		if (_media_type == cmn::MediaType::Video)
		{
			frame->SetWidth(_width);
			frame->SetHeight(_height);
			frame->SetFormat(_format);
			frame->SetPts(_pts);
			frame->SetDuration(_duration);
		}
		else if (_media_type == cmn::MediaType::Audio)
		{
			frame->SetFormat(_format);
			frame->SetBytesPerSample(_bytes_per_sample);
			frame->SetNbSamples(_nb_samples);
			frame->SetChannels(_channels);
			frame->SetSampleRate(_sample_rate);
			frame->SetPts(_pts);
			frame->SetDuration(_duration);
		}
		else
		{
			OV_ASSERT2(false);
			return nullptr;
		}
		return frame;
	}

	void SetPrivData(AVFrame* ptr) {
		_priv_data = ptr;
	}
	AVFrame* GetPrivData() const {
		return _priv_data;
	}

	ov::String GetInfoString() {
		ov::String info;

		info.AppendFormat("TrackID(%d) ", GetTrackId());
		info.AppendFormat("SourceId(%u) ", _source_id);
		info.AppendFormat("Type(%s) ", cmn::GetMediaTypeString(GetMediaType()));
		info.AppendFormat("PTS(%" PRId64 ") ", GetPts());
		info.AppendFormat("Duration(%" PRId64 ") ", GetDuration());
		if(_priv_data != nullptr) {
			info.AppendFormat("NbSamples(%d) ", GetNbSamples());
		}

		return info;
	}

private:
	AVFrame *_priv_data = nullptr;

	// This shows the ID of the module that made the media frame. It can be a decoder or a filter. 
	// The encoder uses this value to check if the filter has changed.
	int32_t _source_id = 0;

	// Common
	cmn::MediaType _media_type = cmn::MediaType::Unknown;
	int32_t _flags = 0;	 // Key, non-Key
	int32_t _track_id = 0;
	int64_t _pts = 0LL;
	int64_t _duration = 0LL;

	cmn::MediaCodecModuleId _codec_module_id = cmn::MediaCodecModuleId::None;
	cmn::DeviceId _codec_device_id = 0;

	// Video 
	int32_t _width = 0;
	int32_t _height = 0;
	int32_t _format = 0;

	// Autio 
	int32_t _bytes_per_sample = 0;
	int32_t _nb_samples = 0;
	cmn::AudioChannel _channels;
	int32_t _sample_rate = 0;
};
