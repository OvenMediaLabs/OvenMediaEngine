//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/media_track.h>
#include <base/mediarouter/media_buffer.h>
#include <base/modules/marker/marker_box.h>

#include "../bmff_packager.h"
#include "fmp4_storage.h"

namespace bmff
{
	class FMP4Packager : public Packager, public MarkerBox
	{
	public:
		struct Config
		{
			double chunk_duration_ms = 500.0;
			double segment_duration_ms = 6000.0;
			
			CencProperty cenc_property;
		};

		FMP4Packager(const std::shared_ptr<FMP4Storage> &storage, const std::shared_ptr<const MediaTrack> &media_track, const std::shared_ptr<const MediaTrack> &data_track, const Config &config);

		~FMP4Packager();

		// Generate Initialization FMP4Segment
		bool CreateInitializationSegment();

		// Switch to a new version of the track at a runtime configuration change.
		// Flushes the buffered samples, completes the in-progress segment, regenerates
		// the initialization segment, and waits for a keyframe to start the new content.
		bool UpdateTrack(const std::shared_ptr<const MediaTrack> &media_track);

		// Another track of the stream changes at the given timestamp; cut a segment
		// boundary there so that every rendition carries an aligned discontinuity.
		// Video cuts at the first keyframe from the boundary, audio at the next frame.
		// If a sample beyond the boundary arrived before this track's own change
		// event, the cut would fire first and add an extra discontinuity domain;
		// current runtime-change sources cannot interleave that way.
		void RequestCutForDiscontinuity(double boundary_timestamp_ms);

		// End timestamp of the last appended sample, the boundary position for
		// propagating a discontinuity to the other tracks
		double GetLastSampleEndTimestampMs() const;

		// Generate Media FMP4Segment
		bool AppendSample(const std::shared_ptr<const MediaPacket> &media_packet);

		// Reserve Data Packet
		// If the data frame is within the time interval of the fragment, it is added.
		bool ReserveDataPacket(const std::shared_ptr<const MediaPacket> &media_packet);

		// Flush all samples
		bool Flush();

	private:
		const Config &GetConfig() const;

		MarkerBox::SegmentationInfo _segmentation_info;
		std::optional<MarkerBox::SegmentationInfo> GetSegmentationInfo() const override;

		std::shared_ptr<bmff::Samples> GetDataSamples(int64_t start_timestamp, int64_t end_timestamp);

		bool StoreInitializationSection(const std::shared_ptr<ov::Data> &segment);

		std::shared_ptr<const MediaPacket> ConvertBitstreamFormat(const std::shared_ptr<const MediaPacket> &media_packet);

		bool WriteFtypBox(ov::ByteStream &data_stream) override;

		Config _config;
		std::shared_ptr<FMP4Storage> _storage = nullptr;

		double _target_chunk_duration_ms = 0.0;

		// After a track change, video samples are dropped until the first keyframe so
		// that the discontinuity segment always starts independent
		bool _waiting_for_keyframe = false;
		uint32_t _dropped_samples_while_waiting = 0;

		// Negative when no cut is pending
		double _pending_cut_timestamp_ms = -1.0;
		// Timestamp of the last boundary this packager handled (own track change or
		// an applied cut), to ignore re-propagation of the same boundary event
		double _last_boundary_timestamp_ms = -1.0;

		std::queue<std::shared_ptr<const MediaPacket>> _reserved_data_packets;
	};
}
