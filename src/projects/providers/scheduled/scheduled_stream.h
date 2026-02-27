//==============================================================================
//
//  ScheduledStream
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <modules/ffmpeg/compat.h>
#include <orchestrator/orchestrator.h>
#include <mediarouter/mediarouter_stream_tap.h>
#include <base/provider/stream.h>

#include "schedule.h"

namespace pvd
{
    // ScheduledStream doesn't support multiple tracks yet
    constexpr int kScheduledVideoTrackId = 0;
    constexpr int kScheduledAudioTrackId = 100;
    constexpr int kScheduledDataTrackId = 200;

    constexpr int kScheduledVideoTimebase = 90000;
	constexpr int kScheduledAudioTimebase = 90000;

    class ScheduledStream : public Stream
    {
    public:
        static std::shared_ptr<ScheduledStream> Create(const std::shared_ptr<Application> &application, const info::Stream &stream_info, const std::shared_ptr<Schedule> &schedule);

        explicit ScheduledStream(const std::shared_ptr<Application> &application, const info::Stream &info, const std::shared_ptr<Schedule> &schedule);
        ~ScheduledStream();

        bool Start() override;
        bool Stop() override;
        bool Terminate() override;

        bool UpdateSchedule(const std::shared_ptr<Schedule> &schedule);
        std::shared_ptr<const Schedule> PeekSchedule() const;

        // Get current program
        bool GetCurrentProgram(std::shared_ptr<Schedule::Program> &curr_program, std::shared_ptr<Schedule::Item> &curr_item, int64_t &curr_item_pos) const;

    private:
        void WorkerThread();

        enum class PlaybackResult
        {
            PLAY_NEXT_ITEM,
            PLAY_NEXT_PROGRAM,
            ERROR,
            FAILBACK
        };

        // If there is no current program
        //      ==> Continue playing until the current program changes
        // If there is no current item
        //      ==> Continue playing until the current program changes (in cases where there is no item, this might occur later in the case of non-repeat programs)
        // If the current item encounters an error
        //      ==> Continue playing until the item returns to a normal state, or until the duration of the item ends, or until the current program changes

        // If there is an error in the fallback
        //     ==> Keep attempting
        // If the fallback ends (program change, item change, item duration ends)
        //     ==> return true
        // If there is no fallback (not configured)
        //     ==> return false
        PlaybackResult PlayFallbackOrWait();

        PlaybackResult PlayItem(const std::shared_ptr<Schedule::Item> &item, bool fallback_item = false);

        PlaybackResult PlayFile(const std::shared_ptr<Schedule::Item> &item, bool fallback_item);
        bool PrepareFilePlayback(const std::shared_ptr<Schedule::Item> &item);
        
        PlaybackResult PlayStream(const std::shared_ptr<Schedule::Item> &item, bool fallback_item);
        std::shared_ptr<MediaRouterStreamTap> PrepareStreamPlayback(const std::shared_ptr<Schedule::Item> &item);
        
        std::shared_ptr<Schedule> GetSchedule() const;
        bool CheckCurrentProgramChanged();
        bool CheckCurrentFallbackProgramChanged();

        bool CheckCurrentItemAvailable(bool immediate=false);
        bool CheckFileItemAvailable(const std::shared_ptr<Schedule::Item> &item);
        bool CheckStreamItemAvailable(const std::shared_ptr<Schedule::Item> &item);
        
        int FindTrackIdByOriginId(int origin_id) const;

		bool SetDurationToAllItems(const std::shared_ptr<Schedule::Program> &program);
		int64_t GetFileItemDurationMS(const std::shared_ptr<Schedule::Item> &item) const;

        std::shared_ptr<Schedule> _schedule;
        mutable std::shared_mutex _schedule_mutex;

        std::thread _worker_thread;
        std::atomic<bool> _worker_thread_running{false};

        mutable ov::Event _schedule_updated;

        // Current
        const Schedule::Stream _channel_info;

        mutable std::shared_mutex _current_mutex;
        std::shared_ptr<Schedule> _current_schedule;
        std::shared_ptr<Schedule::Program> _current_program;
        std::shared_ptr<Schedule::Item> _current_item;
        double _current_item_position_ms = 0;

        // Fallback
        std::shared_ptr<Schedule::Program> _fallback_program;

        std::map<int, int> _origin_id_track_id_map;

		std::map<uint32_t, int64_t> _last_timestamp_map;
		int64_t _biggest_timestamp = -1LL;

        ov::StopWatch _realtime_clock;
		std::map<uint32_t, int64_t> _global_track_offset_us_map;
        ov::StopWatch _failback_check_clock;

        std::map<uint32_t, std::shared_ptr<MediaPacket>> _last_packet_map;
    };
}
