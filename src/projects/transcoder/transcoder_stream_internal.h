//==============================================================================
//
//  TranscoderStreamInternal
//
//  Created by Keukhan
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/application.h>

#include <stdint.h>
#include <memory>
#include <queue>
#include <vector>

#include "base/info/stream.h"
#include "base/mediarouter/media_type.h"
#include "transcoder_context.h"

class TranscoderStreamInternal
{
public:
	TranscoderStreamInternal();
	~TranscoderStreamInternal();

public:
	MediaTrackId NewTrackId();
	std::atomic<MediaTrackId> _last_track_index = 0;

	static ov::String ProfileToSerialize(const uint32_t track_id, const cfg::vhost::app::oprf::VideoProfile &profile);
	static ov::String ProfileToSerialize(const uint32_t track_id, const cfg::vhost::app::oprf::AudioProfile &profile);
	static ov::String ProfileToSerialize(const uint32_t track_id, const cfg::vhost::app::oprf::ImageProfile &profile);
	static ov::String ProfileToSerialize(const uint32_t track_id, const cfg::vhost::app::oprf::SpeechToTextProfile &profile);
	static ov::String ProfileToSerialize(const uint32_t track_id);
	
	static cmn::Timebase GetDefaultTimebaseByCodecId(cmn::MediaCodecId codec_id);

	std::shared_ptr<MediaTrack> CreateOutputTrack(const std::shared_ptr<MediaTrack> &input_track, const cfg::vhost::app::oprf::VideoProfile &profile);
	std::shared_ptr<MediaTrack> CreateOutputTrack(const std::shared_ptr<MediaTrack> &input_track, const cfg::vhost::app::oprf::AudioProfile &profile);
	std::shared_ptr<MediaTrack> CreateOutputTrack(const std::shared_ptr<MediaTrack> &input_track, const cfg::vhost::app::oprf::ImageProfile &profile);
	std::shared_ptr<MediaTrack> CreateOutputTrack(const std::shared_ptr<MediaTrack> &input_track, const cfg::vhost::app::oprf::SpeechToTextProfile &profile);
	std::shared_ptr<MediaTrack> CreateOutputTrackDataType(const std::shared_ptr<MediaTrack> &input_track);	

	bool IsMatchesBypassCondition(const std::shared_ptr<MediaTrack> &input_track, const cfg::vhost::app::oprf::VideoProfile &profile);
	bool IsMatchesBypassCondition(const std::shared_ptr<MediaTrack> &input_track, const cfg::vhost::app::oprf::AudioProfile &profile);

	double GetProperFramerate(const std::shared_ptr<MediaTrack> &ref_track);
	static double MeasurementToRecommendFramerate(double framerate);

	void UpdateOutputTrackPassthrough(const std::shared_ptr<MediaTrack> &output_track, std::shared_ptr<MediaFrame> buffer);
	void UpdateOutputTrackByDecodedFrame(const std::shared_ptr<MediaTrack> &output_track, const std::shared_ptr<MediaTrack> &input_track, std::shared_ptr<MediaFrame> buffer);


	// This is used to check if only keyframes can be decoded.
	// If the output profile has only image encoding options, keyframes can be decoded to use the CPU efficiently.
	bool IsKeyframeOnlyDecodable(const std::map<ov::String, std::shared_ptr<info::Stream>> &streams);

	void GetCountByEncodingType(const std::map<ov::String, std::shared_ptr<info::Stream>> &streams,
								  uint32_t &video, uint32_t &video_bypass,
								  uint32_t &audio, uint32_t &audio_bypass,
								  uint32_t &image, uint32_t &data);

public:
	// This is used to check if the input track has changed during the update.
	bool StoreTracks(std::shared_ptr<info::Stream> stream);
	std::map<int32_t, std::shared_ptr<MediaTrack>> &GetStoredTracks();
	bool CompareTracksForSeamlessTransition(std::map<int32_t, std::shared_ptr<MediaTrack>> prev_tracks, std::map<int32_t, std::shared_ptr<MediaTrack>> new_tracks);

	std::map<int32_t, std::shared_ptr<MediaTrack>> _store_tracks;	
};
