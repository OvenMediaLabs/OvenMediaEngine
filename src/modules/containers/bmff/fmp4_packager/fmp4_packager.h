//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <optional>

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

		// Rotate the DRM key from the next segment on. Nothing is cut: where the next
		// segment starts on its own, the content version advances, the sample buffer is
		// rebuilt with the new key and the initialization section is regenerated with the
		// new tenc/pssh. Every segment therefore carries a single key, and no
		// discontinuity is signaled.
		void RequestKeyRotation(const CencProperty &cenc_property);

		// End timestamp of the last appended sample, the boundary position for
		// propagating a discontinuity to the other tracks
		double GetLastSampleEndTimestampMs() const;

		// Content version currently stamped on this track's segments. Advances on a
		// track configuration change and on a key rotation.
		uint32_t GetCurrentContentVersion() const;

		// The CENC key material a content version was packaged with, to advertise in the
		// playlist for that version's segments. Recorded when the version's
		// initialization section is created, so the answer does not depend on when the
		// segment is reported. Empty when the version is unknown (already pruned).
		std::optional<CencProperty> GetCencPropertyForVersion(uint32_t content_version) const;

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

		// A DRM key rotation requested from the stream, applied where the next segment
		// starts on its own
		std::optional<CencProperty> _pending_key_rotation;

		// CENC key material per content version, recorded when that version's
		// initialization section is created. Only ever queried for a version whose segment
		// has just been produced, so a fixed retention of the most recent versions is
		// enough; the playlist keeps its own copy for as long as it lists that version.
		static constexpr size_t kMaxRetainedCencVersions = 64;
		std::map<uint32_t, CencProperty> _cenc_property_by_version;

		std::queue<std::shared_ptr<const MediaPacket>> _reserved_data_packets;
	};
}
