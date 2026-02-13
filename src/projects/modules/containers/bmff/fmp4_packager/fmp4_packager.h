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

		std::queue<std::shared_ptr<const MediaPacket>> _reserved_data_packets;

		std::map<int64_t, Marker> _markers;
		mutable std::shared_mutex _markers_guard;
	};
}