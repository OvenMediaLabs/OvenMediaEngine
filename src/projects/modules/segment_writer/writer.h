//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#define __STDC_FORMAT_MACROS 1

extern "C"
{
#include <libavformat/avformat.h>
}

#include <base/info/media_track.h>
#include <base/mediarouter/media_buffer.h>
#include <base/mediarouter/media_type.h>
#include <base/ovlibrary/ovlibrary.h>

// Default buffer size to use if bps cannot be obtained from the track list.
#define WRITER_DEFAULT_BUFFER_SIZE (1024 * 1024)
// The amount to increase when there's not enough buffer.
#define WRITER_BUFFER_SIZE_INCREMENT (1024 * 1024)
// When calculating the buffer size from the track list initially, leaving some room.
#define WRITER_BUFFER_ROOM_MULTIPLIER (1.2)

class Writer
{
public:
	enum class Type
	{
		MpegTs,
		M4s
	};

	enum class MediaType
	{
		None,
		Both,
		Video,
		Audio
	};

	Writer(Type type, MediaType media_type);
	virtual ~Writer();

	Type GetType() const
	{
		return _type;
	}

	MediaType GetMediaType() const
	{
		return _media_type;
	}

	bool AddTrack(const std::shared_ptr<const MediaTrack> &media_track);
	bool Prepare(ov::String service_name = "");
	bool PrepareIfNeeded(ov::String service_name = "");

	bool ResetData(int track_id = -1);
	std::shared_ptr<const ov::Data> GetData() const;
	off_t GetCurrentOffset() const;

	// Get the packet pts of the track
	// Unit: the timebase of the MediaTrack
	int64_t GetFirstPts(uint32_t track_id) const;
	// Get the packet pts by MediaType
	// Unit: the timebase of the MediaTrack
	int64_t GetFirstPts(cmn::MediaType type) const;
	// Get the packet pts of the first track
	// Unit: the timebase of the MediaTrack
	int64_t GetFirstPts() const;

	// Get the duration of the track
	// Unit: the timebase of the MediaTrack
	int64_t GetDuration(uint32_t track_id) const;
	// Get the duration of the first track
	// Unit: the timebase of the MediaTrack
	int64_t GetDuration() const;

	bool WritePacket(const std::shared_ptr<const MediaPacket> &packet, const std::shared_ptr<const ov::Data> &data, const std::vector<size_t> &length_list, size_t skip_count, size_t split_count);
	bool WritePacket(const std::shared_ptr<const MediaPacket> &packet);
	bool Flush();

	std::shared_ptr<const ov::Data> Finalize();

protected:
	struct Track
	{
		Track(int stream_index, AVRational rational, std::shared_ptr<const MediaTrack> track)
			: stream_index(stream_index),
			  rational(rational),
			  track(track)
		{
		}

		void Reset()
		{
			duration = 0L;
			first_pts = -1L;
			first_packet_received = false;
		}

		// 0-based stream index (It's the same as the AVStream.index, and an index of _track_list)
		int stream_index;
		AVRational rational;
		std::shared_ptr<const MediaTrack> track;

		// Unit: Timebase
		int64_t duration = 0L;
		int64_t first_pts = -1L;
		bool first_packet_received = false;
	};

	// Needed mutex lock for _track_list before calling this method
	int DecideBufferSize() const OV_REQUIRES(_track_mutex);

	int OnWrite(const uint8_t *buf, int buf_size);
	static int OnWrite(void *opaque, uint8_t *buf, int buf_size)
	{
		return (static_cast<Writer *>(opaque))->OnWrite(buf, buf_size);
	}

	int64_t OnSeek(int64_t offset, int whence);
	static int64_t OnSeek(void *opaque, int64_t offset, int whence)
	{
		return (static_cast<Writer *>(opaque))->OnSeek(offset, whence);
	}

	std::shared_ptr<AVFormatContext> CreateFormatContext(Type type);
	std::shared_ptr<AVIOContext> CreateAvioContext();
	void LogPacket(const AVPacket *packet) const;
	bool FillCodecParameters(const std::shared_ptr<const Track> &track, AVCodecParameters *codec_parameters);

	Type _type;
	MediaType _media_type = MediaType::None;

	std::shared_ptr<AVFormatContext> _format_context;
	std::shared_ptr<AVIOContext> _avio_context;
	const AVOutputFormat *_output_format = nullptr;

	AVDictionary *_options = nullptr;

	std::shared_ptr<ov::ByteStream> _data_stream;

	mutable ov::Mutex _track_mutex;
	// Key: MediaPacket.GetTrackId()
	// Value: Track.stream_index
	std::map<int, std::shared_ptr<Track>> _track_map OV_GUARDED_BY(_track_mutex);
	std::vector<std::shared_ptr<Track>> _track_list OV_GUARDED_BY(_track_mutex);

	std::atomic<int> _buffer_size{-1};
};
