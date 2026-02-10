//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/media_track.h>
#include <base/mediarouter/media_buffer.h>
#include <base/modules/marker/marker_box.h>
#include <base/modules/container/segment_storage.h>

#include "mpegts_packetizer.h"

namespace mpegts
{
    constexpr size_t SEGMENT_BUFFER_SIZE = 2000000;

    class Segment : public base::modules::Segment
    {
    public:
        Segment(int64_t segment_id, int64_t first_dts, double duration_ms)
        {
            _segment_id = segment_id;
            _first_dts = first_dts;
            _duration_ms = duration_ms;
        }

        int64_t GetNumber() const override
		{
			return _segment_id;
		}

        int64_t GetStartTimestamp() const override
        {
            return _first_dts;
        }

        double GetDurationMs() const override
        {
            return _duration_ms;
        }

        const std::shared_ptr<ov::Data> GetData() const override
		{
			if (_is_data_in_memory)
			{
				return _data;
			}

			if (_is_data_in_file)
			{
				// Read from file
				auto data = ov::LoadFromFile(_file_path);
				if (data == nullptr)
				{
					loge("MPEG-2 TS", "Segment::GetData - Failed to load data from file(%s)", _file_path.CStr());
				}
				return data;
			}

			return nullptr;
		}

        uint64_t GetDataLength() const override
        {
            auto data = GetData();
            return data == nullptr ? 0 : data->GetLength();
        }

        bool IsCompleted() const override
        {
            return true;
        }

        bool HasMarker() const override
		{
			return _markers.empty() == false;
		}

        const std::vector<std::shared_ptr<Marker>> &GetMarkers() const override
		{
			return _markers;
		}

        double GetTimebaseSeconds() const override
        {
            return 1.0 / mpegts::TIMEBASE_DBL;
        }

        bool AddPacketData(const std::shared_ptr<const ov::Data> &data)
        {
            if (_data == nullptr)
            {
                _data = std::make_shared<ov::Data>(SEGMENT_BUFFER_SIZE);
            }

            _data->Append(data);

			_is_data_in_memory = true;

            return true;
        }

        int64_t GetId() const
        {
            return _segment_id;
        }

		ov::String GetUrl() const override
		{
			return _url;
		}

		void SetUrl(const ov::String &url) override
		{
			_url = url;
		}

		ov::String GetFilePath() const
		{
			return _file_path;
		}

		void SetFilePath(const ov::String &file_path)
		{
			_file_path = file_path;

			_is_data_in_file = true;
		}

		void ResetData()
		{
			_data.reset();
			_data = nullptr;

			_is_data_in_memory = false;
		}

		bool IsDataInMemory() const
		{
			return _is_data_in_memory;
		}

		bool IsDataInFile() const
		{
			return _is_data_in_file;
		}

		void SetMarkers(const std::vector<std::shared_ptr<Marker>> &markers)
		{
			_markers = markers;
		}

    private:
        int64_t _segment_id = 0;
        int64_t _first_dts = -1;
        double _duration_ms = 0;
		ov::String _url;
        
		ov::String _file_path;
        std::shared_ptr<ov::Data> _data;

		bool _is_data_in_memory = false;
		bool _is_data_in_file = false;

		std::vector<std::shared_ptr<Marker>> _markers;
    };

    struct Sample
    {
        Sample(const std::shared_ptr<const MediaPacket> &media_packet, const std::shared_ptr<const ov::Data> &ts_packet_data, double timescale)
        {
            this->media_packet = media_packet;
            this->ts_packet_data = ts_packet_data;

			if (media_packet != nullptr)
			{
				_pts = (static_cast<double>(media_packet->GetPts()) / timescale * TIMEBASE_DBL);
				_dts = (static_cast<double>(media_packet->GetDts()) / timescale * TIMEBASE_DBL);
				_duration = (static_cast<double>(media_packet->GetDuration()) / timescale * TIMEBASE_DBL);
			}
        }

		int64_t _pts = -1;
		int64_t _dts = -1;
		int64_t _duration = -1;
		
        std::shared_ptr<const MediaPacket> media_packet = nullptr;
        std::shared_ptr<const ov::Data> ts_packet_data = nullptr;
    };

    class SampleBuffer
    {
    public:
        SampleBuffer(const std::shared_ptr<const MediaTrack> &track)
        {
            _track = track;
        }

        std::shared_ptr<const MediaTrack> GetTrack() const
        {
            return _track;
        }

        bool AddSample(const Sample &sample)
        {
            _samples.push(sample);

            _current_samples_count++;
            _current_samples_duration += sample._duration;

			_total_available_count++;
			_total_available_duration += sample._duration;

            return true;
        }
        
        uint64_t GetCurrentDuration() const
        {
            return _current_samples_duration;
        }

		double GetCurrentDurationMs() const
		{
			// return in milliseconds, sample duration is in 90kHz
			return static_cast<double>(GetCurrentDuration()) / TIMEBASE_DBL * 1000.0;
		}

        bool HasSegmentBoundary() const
        {
            return _segment_boundaries.empty() == false;
        }

        // number of boundary 
        size_t GetSegmentBoundaryCount() const
        {
            return _segment_boundaries.size();
        }

        void MarkSegmentBoundary()
        {
            SegmentBoundary boundary;
            boundary.sample_count = _current_samples_count;
            boundary.duration = _current_samples_duration;

            _segment_boundaries.push(boundary);

            _current_samples_count = 0;
            _current_samples_duration = 0;
        }

        uint64_t GetDurationUntilSegmentBoundary() const
        {
            if (HasSegmentBoundary() == false)
            {
                return 0;
            }

            return _segment_boundaries.front().duration;
        }

		double GetDurationUntilSegmentBoundaryMs() const
		{
			// return in milliseconds, sample duration is in 90kHz
			return static_cast<double>(GetDurationUntilSegmentBoundary()) / TIMEBASE_DBL * 1000.0;
		}

		uint64_t GetTotalAvailableDuration() const
		{
			return _total_available_duration;
		}

		double GetTotalAvailableDurationMs() const
		{
			// return in milliseconds, sample duration is in 90kHz
			return static_cast<double>(GetTotalAvailableDuration()) / TIMEBASE_DBL * 1000.0;
		}
        
        bool IsEmpty() const
        {
            return _samples.empty();
        }

        const Sample PopSample()
        {
            if (_samples.empty())
            {
                return Sample(nullptr, nullptr, 0);
            }

            auto sample = _samples.front();
            _samples.pop();

            _current_samples_count--;
            _current_samples_duration -= sample._duration;
			
			_total_available_count--;
			_total_available_duration -= sample._duration;

            _total_consumed_samples_count++;
            _total_consumed_samples_duration += sample._duration;
			
            return sample;
        }

        const Sample GetSample() const
        {
            if (_samples.empty())
            {
                return Sample(nullptr, nullptr, 0);
            }

            return _samples.front();
        }

        std::vector<Sample> PopSamplesUntilSegmentBoundary()
        {
            std::vector<Sample> samples;

            SegmentBoundary boundary = _segment_boundaries.front();
            _segment_boundaries.pop();

            //logt("DEBUG", "PopSamplesUntilSegmentBoundary : sample_count %u, duration_ms %u", boundary.sample_count, boundary.duration_ms);

            for (uint64_t i = 0; i < boundary.sample_count; i++)
            {
                if (_samples.empty())
                {
                    // would not happen
                    break;
                }

                auto sample = _samples.front();
                _samples.pop();

                samples.push_back(sample);
            }

            _total_consumed_samples_count += boundary.sample_count;
            _total_consumed_samples_duration += boundary.duration;

			_total_available_duration -= boundary.duration;
			_total_available_count -= boundary.sample_count;

            return samples;
        }

        uint64_t GetTotalConsumedDuration() const
        {
            return _total_consumed_samples_duration;
        }

		double GetTotalConsumedDurationMs() const
		{
			// return in milliseconds, sample duration is in 90kHz
			return static_cast<double>(GetTotalConsumedDuration()) / TIMEBASE_DBL * 1000.0;
		}

    private:
        std::shared_ptr<const MediaTrack> _track;
        std::queue<Sample> _samples;

        struct SegmentBoundary
        {
            uint64_t sample_count = 0;
            uint64_t duration = 0;
        };

        std::queue<SegmentBoundary> _segment_boundaries;

        uint64_t _current_samples_count = 0;
        uint64_t _current_samples_duration = 0;

		uint64_t _total_available_duration = 0;
		uint64_t _total_available_count = 0;

        uint64_t _total_consumed_samples_count = 0;
        uint64_t _total_consumed_samples_duration = 0;
    };

	class PackagerSink : public ov::EnableSharedFromThis<PackagerSink>
	{
	public:
		virtual void OnSegmentCreated(const ov::String &packager_id, const std::shared_ptr<base::modules::Segment> &segment) = 0;
		virtual void OnSegmentDeleted(const ov::String &packager_id, const std::shared_ptr<base::modules::Segment> &segment) = 0;
	};

    // Make mpeg-2 ts container

	// Segments are stored in the following locations:
	// DVR off, Retention 0 	: Buffer
	// DVR off, Retention > 0 	: Buffer --> Retention
	// DVR on, Retention > 0	: Buffer --> DVR(file) --> Retention(file) 
	// DVR on, Retention 0		: Buffer --> DVR(file)
    class Packager : public PacketizerSink, public MarkerBox, public base::modules::SegmentStorage
    {
    public:
        struct Config
        {
            uint32_t target_duration_ms = 6000;
			uint32_t max_segment_count = 10; // 10 segments, it will be saved in memory

			// Live Rewind
			ov::String dvr_storage_path;
			uint32_t dvr_window_ms = 0; // Rewind window in milliseconds,  1 hour (60000 * 60), it will be saved in files

			uint32_t segment_retention_count = 2; // This number of segments are retained event after the SegmentRemoved event occurs

			ov::String stream_id_meta;
        };

        Packager(const ov::String &packager_id, const Config &config);
        ~Packager();

		bool AddSink(const std::shared_ptr<PackagerSink> &sink);
        std::shared_ptr<const ov::Data> GetSegmentData(int64_t segment_id) const;

        ////////////////////////////////
        // PacketizerSink interface
        ////////////////////////////////

        // PAT, PMT, ...
        void OnPsi(const std::vector<std::shared_ptr<const MediaTrack>> &tracks, const std::vector<std::shared_ptr<mpegts::Packet>> &psi_packets) override;
        // PES packets for a frame
        void OnFrame(const std::shared_ptr<const MediaPacket> &media_packet, const std::vector<std::shared_ptr<mpegts::Packet>> &pes_packets) override;

		void Flush();
        ////////////////////////////////
        // SegmentStorage interface
        ////////////////////////////////

        std::shared_ptr<ov::Data> GetInitializationSection() const override
        {
            return nullptr;
        }

		// Get the segment data
		std::shared_ptr<base::modules::Segment> GetSegment(int64_t segment_number) const override;
        std::shared_ptr<base::modules::Segment> GetLastSegment() const override;
        uint64_t GetSegmentCount() const override;
        int64_t GetLastSegmentNumber() const override;

        // mpegts::Packager does not support partial segments
        std::shared_ptr<base::modules::PartialSegment> GetPartialSegment(int64_t segment_number, int64_t partial_number) const override
        {
            return nullptr;
        }
        std::tuple<int64_t, int64_t> GetLastPartialSegmentNumber() const override
        {
            return std::make_tuple(-1, -1);
        }
        uint64_t GetMaxPartialDurationMs() const override
        {
            return 0;
        }
		uint64_t GetMinPartialDurationMs() const override
        {
            return 0;
        }

        ov::String GetContainerExtension() const override 
        { 
            return "ts"; 
        }

    private:
        const Config &GetConfig() const;

        int64_t GetNextSegmentId();

        std::shared_ptr<ov::Data> MergeTsPacketData(const std::vector<std::shared_ptr<mpegts::Packet>> &ts_packets);
    
        std::shared_ptr<const MediaTrack> GetMediaTrack(uint32_t track_id) const;
        std::shared_ptr<SampleBuffer> GetSampleBuffer(uint32_t track_id) const;

        // Check if a segment is ready to be created and create it
        void CreateSegmentIfReady(bool force_create = false);
        void AddSegment(const std::shared_ptr<Segment> &segment);

		// Buffer
		void AddSegmentToBuffer(const std::shared_ptr<Segment> &segment);
		std::shared_ptr<Segment> GetOldestSegmentFromBuffer() const;
		size_t GetBufferedSegmentCount() const;
		void RemoveSegmentFromBuffer(const std::shared_ptr<Segment> &segment);

		// DVR
		void SaveSegmentToFile(const std::shared_ptr<Segment> &segment);
		void DeleteSegmentFile(const std::shared_ptr<Segment> &segment);
		void DeleteSegmentFromFileStoredList(const std::shared_ptr<Segment> &segment);
		double GetTotalFileStoredSegmentsDurationMs() const;
		std::shared_ptr<Segment> GetOldestSegmentFromFile() const;
		
		// Retention
		void SaveSegmentToRetentionBuffer(const std::shared_ptr<Segment> &segment);
		size_t GetReteinedSegmentCount() const;
		void RemoveSegmentFromRetentionBuffer(const std::shared_ptr<Segment> &segment);
		std::shared_ptr<Segment> GetOldestSegmentFromRetentionBuffer() const;

		// Broadcast
		void BroadcastSegmentCreated(const std::shared_ptr<Segment> &segment);
		void BroadcastSegmentDeleted(const std::shared_ptr<Segment> &segment);

		ov::String GetDvrStoragePath() const;
		ov::String GetSegmentFilePath(int64_t segment_id) const;
       
        ov::String _packager_id;
        Config _config;

		// make segment boundary as soon as possible
		bool _force_make_boundary = false;

        // track_id -> SampleBuffer
        std::map<uint32_t, std::shared_ptr<SampleBuffer>> _sample_buffers;

        uint32_t _main_track_id = UINT32_MAX;
        std::map<uint32_t, std::shared_ptr<const MediaTrack>> _media_tracks;
        std::vector<std::shared_ptr<mpegts::Packet>> _psi_packets;
        std::shared_ptr<ov::Data> _psi_packet_data;

        int64_t _last_segment_id = 0;

        std::map<int64_t, std::shared_ptr<Segment>> _segments;
		double _total_segments_duration_ms = 0;
		mutable std::shared_mutex _segments_guard;

		std::map<int64_t, std::shared_ptr<Segment>> _file_stored_segments;
		double _total_file_stored_segments_duration_ms = 0;
		mutable std::shared_mutex _file_stored_segments_guard;

		std::map<int64_t, std::shared_ptr<Segment>> _retained_segments;
		mutable std::shared_mutex _retained_segments_guard;

		std::vector<std::shared_ptr<PackagerSink>> _sinks;

		double _next_target_duration_ms = 0.0; // next target duration in milliseconds
    };
}