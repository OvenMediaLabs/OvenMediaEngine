//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/mediarouter/media_buffer.h>

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

// Makes audio and the first video track start at that track's first keyframe,
// so their segment cadences start at the same point; any other video track
// starts at its own first keyframe. A stream that joined in the middle of a GOP
// drops the video before the keyframe and the audio before its DTS. A stream
// whose first video packet is a keyframe passes unchanged; audio arriving
// before that packet is only held until it arrives. Used only until
// IsAligned(), from the single thread a stream's packets are pushed on.
//
// Audio that itself starts later than the keyframe is not covered: each track
// counts its segments from its own first sample in duration mode, so that
// audio keeps its boundaries and numbers offset by the gap.
class LLHlsKeyframeStartAligner
{
public:
	// reference_video_track_id: the video track the audio start follows, -1 when
	// the stream has no video (audio then passes as it comes)
	// max_held_packets: the most audio packets held while waiting for the video
	LLHlsKeyframeStartAligner(int32_t reference_video_track_id,
							  const std::vector<uint32_t> &video_track_ids, const std::vector<uint32_t> &audio_track_ids,
							  size_t max_held_packets, const ov::String &log_context);

	// The packets to package now, in arrival order
	std::vector<std::shared_ptr<MediaPacket>> Push(const std::shared_ptr<MediaPacket> &media_packet, int64_t dts_us);

	// Every track has started, so later packets need no alignment
	bool IsAligned() const;

private:
	enum class AudioState
	{
		// No packet of the reference video yet
		Holding,
		// The reference video joined mid-GOP and waits for its keyframe
		Trimming,
		Open,
	};

	struct HeldPacket
	{
		std::shared_ptr<MediaPacket> packet;
		int64_t dts_us = 0;
	};

	struct VideoTrackState
	{
		bool started = false;
		size_t dropped_count = 0;
	};

	void PushVideo(const std::shared_ptr<MediaPacket> &media_packet, int64_t dts_us, std::vector<std::shared_ptr<MediaPacket>> &output);
	void PushAudio(const std::shared_ptr<MediaPacket> &media_packet, int64_t dts_us, std::vector<std::shared_ptr<MediaPacket>> &output);
	void Hold(const std::shared_ptr<MediaPacket> &media_packet, int64_t dts_us);
	void DropHeldBefore(int64_t dts_us);
	void ReleaseHeld(std::vector<std::shared_ptr<MediaPacket>> &output);

	int32_t _reference_video_track_id = -1;
	size_t _max_held_packets = 0;
	ov::String _log_context;

	AudioState _audio_state = AudioState::Holding;
	std::deque<HeldPacket> _held_audio;
	size_t _dropped_audio_count = 0;

	// The newest DTS of the reference video while it waits for its keyframe;
	// the keyframe can only come later
	int64_t _newest_video_dts_us = 0;
	// Where the stream started, set only when it was trimmed
	std::optional<int64_t> _start_dts_us;

	std::map<uint32_t, VideoTrackState> _video_tracks;
	// Audio tracks with no packet passed yet; a late packet of such a track may
	// still belong before the start, while DTS only increases after one passed
	std::set<uint32_t> _audio_tracks_not_passed;
};
