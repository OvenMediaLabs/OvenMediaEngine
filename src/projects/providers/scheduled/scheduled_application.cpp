//==============================================================================
//
//  ScheduledApplication
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================

#include "scheduled_application.h"
#include "scheduled_stream.h"
#include "schedule.h"
#include "schedule_private.h"

#include <base/ovlibrary/files.h>

namespace pvd
{
    // Implementation of ScheduledApplication
    std::shared_ptr<ScheduledApplication> ScheduledApplication::Create(const std::shared_ptr<Provider> &provider, const info::Application &application_info)
    {
        auto application = std::make_shared<pvd::ScheduledApplication>(provider, application_info);
        if (!application->Start())
        {
            return nullptr;
        }

        return application;
    }

    ScheduledApplication::ScheduledApplication(const std::shared_ptr<Provider> &provider, const info::Application &info)
        : Application(provider, info)
    {
    }

    bool ScheduledApplication::Start()
    {
        auto config = GetConfig().GetProviders().GetScheduledProvider();
        _media_root_dir = ov::GetDirPath(config.GetMediaRootDir(), cfg::ConfigManager::GetInstance()->GetConfigPath());
        _schedule_files_path = ov::GetDirPath(config.GetScheduleFilesDir(), cfg::ConfigManager::GetInstance()->GetConfigPath());

        _schedule_file_name_regex = ov::Regex::CompiledRegex(ov::Regex::WildCardRegex(ov::String::FormatString("*.%s", ScheduleFileExtension)));
        
        return Application::Start();
    }

    bool ScheduledApplication::Stop()
    {
        return Application::Stop();
    }

    // Called by ScheduledProvider thread every 500ms 
    void ScheduledApplication::OnScheduleWatcherTick()
    {
        // List all files in schedule file root
        auto schedule_file_info_list_from_dir = SearchScheduleFileInfoFromDir(_schedule_files_path);

        // Check if file is added or updated
        for (auto &schedule_file_info_from_dir : schedule_file_info_list_from_dir)
        {
            ScheduleFileInfo schedule_file_info_from_db;
            if (GetScheduleFileInfoFromDB(schedule_file_info_from_dir._file_path.Hash(), schedule_file_info_from_db) == false)
            {
                // New file
                if (AddSchedule(schedule_file_info_from_dir) == true)
                {
                    _schedule_file_info_db.emplace(schedule_file_info_from_dir._file_path.Hash(), schedule_file_info_from_dir);
                    logti("Added schedule channel : %s/%s (%s)", GetVHostAppName().CStr(), schedule_file_info_from_dir._schedule->GetStream()._name.CStr(), schedule_file_info_from_dir._file_path.CStr());
                }
            }
            else
            {
                schedule_file_info_from_dir._deleted_checked_count = 0;
                // Updated?
                if (schedule_file_info_from_dir._file_stat.st_mtime != schedule_file_info_from_db._file_stat.st_mtime)
                {
                    if (UpdateSchedule(schedule_file_info_from_db, schedule_file_info_from_dir) == true)
                    {
                        _schedule_file_info_db[schedule_file_info_from_dir._file_path.Hash()] = schedule_file_info_from_dir;
                        logti("Updated schedule channel : %s/%s (%s)", GetVHostAppName().CStr(), schedule_file_info_from_dir._schedule->GetStream()._name.CStr(), schedule_file_info_from_dir._file_path.CStr());
                    }
                }
            }
        }

        // Check if file is deleted
        for (auto it = _schedule_file_info_db.begin(); it != _schedule_file_info_db.end();)
        {
            auto &schedule_file_info = it->second;
            struct stat file_stat = {0};
            auto result = stat(schedule_file_info._file_path.CStr(), &file_stat);
            if (result != 0 && errno == ENOENT)
            {
                // VI Editor ":wq" will delete file and create new file, 
                // so we need to check if file is really deleted by checking ENOENT 3 times
                schedule_file_info._deleted_checked_count++;
                if (schedule_file_info._deleted_checked_count < 3)
                {
                    ++it;
                    continue;
                }

                RemoveSchedule(schedule_file_info);
                logti("Removed schedule channel : %s/%s (%s)", GetVHostAppName().CStr(), schedule_file_info._schedule->GetStream()._name.CStr(), schedule_file_info._file_path.CStr());

                it = _schedule_file_info_db.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    std::vector<ScheduledApplication::ScheduleFileInfo> ScheduledApplication::SearchScheduleFileInfoFromDir(const ov::String &schedule_file_root) const
    {
        std::vector<ScheduleFileInfo> schedule_files;

        auto [success, file_list] = ov::GetFileList(schedule_file_root);
        if (success == false)
        {
            return schedule_files;
        }

        for (auto &file_path : file_list)
        {
            if (_schedule_file_name_regex.Matches(file_path).IsMatched() == false)
            {
                continue;
            }

            ScheduleFileInfo schedule_file_info;
            schedule_file_info._file_path = file_path;

            if (stat(file_path.CStr(), &schedule_file_info._file_stat) != 0)
            {
                continue;
            }

            if (S_ISREG(schedule_file_info._file_stat.st_mode) == false)
            {
                continue;
            }

            logtt("Found schedule file : %s, mtime : %d, hash : %d", schedule_file_info._file_path.CStr(), schedule_file_info._file_stat.st_mtime, schedule_file_info._file_path.Hash());

            schedule_files.push_back(schedule_file_info);
        }

        return schedule_files;
    }

    bool ScheduledApplication::GetScheduleFileInfoFromDB(const size_t &hash, ScheduleFileInfo &schedule_file_info)
    {
        auto it = _schedule_file_info_db.find(hash);
        if (it == _schedule_file_info_db.end())
        {
            return false;
        }

        schedule_file_info = it->second;
        return true;
    }

    bool ScheduledApplication::AddSchedule(ScheduleFileInfo &schedule_file_info)
    {
        auto [schedule, msg] = Schedule::CreateFromXMLFile(schedule_file_info._file_path, _media_root_dir);
        if (schedule == nullptr)
        {
            logte("Failed to add schedule (Could not create schedule): %s - %s", schedule_file_info._file_path.CStr(), msg.CStr());
            return false;
        }

        // Create Stream
        auto stream_info = info::Stream(*this, IssueUniqueStreamId(), StreamSourceType::Scheduled);
        stream_info.SetName(schedule->GetStream()._name);

        if (schedule->GetStream()._bypass_transcoder == true)
        {
            stream_info.SetRepresentationType(StreamRepresentationType::Relay);
        }
        
        auto stream = ScheduledStream::Create(GetSharedPtrAs<Application>(), stream_info, schedule);
        if (stream == nullptr)
        {
            logte("Failed to add schedule (Could not create stream): %s", schedule_file_info._file_path.CStr());
            return false;
        }

        auto channel_info = schedule->GetStream();

        // Make temp tracks
        if (channel_info._video_track == true)
        {
            auto track = std::make_shared<MediaTrack>();
            track->SetId(kScheduledVideoTrackId);
            auto public_name = ov::String::FormatString("Video_%d", track->GetId());
            track->SetPublicName(public_name);
            track->SetMediaType(cmn::MediaType::Video);

            // Set Timebase to 1/1000 fixed
            track->SetTimeBase(1, kScheduledVideoTimebase);

            stream->AddTrack(track);
        }

        if (channel_info._audio_track == true)
        {
			if (channel_info._audio_map.empty())
			{
				auto track = std::make_shared<MediaTrack>();
				track->SetId(kScheduledAudioTrackId);
				auto public_name = ov::String::FormatString("Audio_%d", track->GetId());
				track->SetPublicName(public_name);
				track->SetMediaType(cmn::MediaType::Audio);
				track->SetTimeBase(1, kScheduledAudioTimebase);

				stream->AddTrack(track);
			}
			else
			{
				for (const auto &audio_map_item : channel_info._audio_map)
				{
					auto track = std::make_shared<MediaTrack>();
					track->SetId(kScheduledAudioTrackId + audio_map_item.GetIndex());

					ov::String public_name = audio_map_item.GetName();
					if (public_name.IsEmpty())
					{
						public_name = ov::String::FormatString("Audio_%d", track->GetId());
					}
					track->SetPublicName(public_name);
					track->SetLanguage(audio_map_item.GetLanguage());
					track->SetCharacteristics(audio_map_item.GetCharacteristics());
					track->SetMediaType(cmn::MediaType::Audio);
					track->SetTimeBase(1, kScheduledAudioTimebase);

					stream->AddTrack(track);
				}
			}
        }

		auto data_track = std::make_shared<MediaTrack>();
		data_track->SetId(kScheduledDataTrackId);
		data_track->SetMediaType(cmn::MediaType::Data);
		data_track->SetTimeBase(1, 1000); // Data track time base is always 1/1000 in
		data_track->SetOriginBitstream(cmn::BitstreamFormat::Unknown);
		stream->AddTrack(data_track);

        if (AddStream(stream) == false)
        {
            logte("Failed to add schedule (Could not add stream): %s", schedule_file_info._file_path.CStr());
            return false;
        }

        stream->Start();

        // Add file info to DB 
        schedule_file_info._schedule = schedule;

        return true;
    }

    bool ScheduledApplication::UpdateSchedule(ScheduleFileInfo &schedule_file_info, ScheduleFileInfo &new_schedule_file_info)
    {
        std::shared_ptr<Schedule> old_schedule = schedule_file_info._schedule;
        if (old_schedule == nullptr)
        {
            logte("Failed to update schedule (Could not find schedule): %s", schedule_file_info._file_path.CStr());
            return false;
        }

        auto [new_schedule, err] = Schedule::CreateFromXMLFile(new_schedule_file_info._file_path, _media_root_dir);
        if (new_schedule == nullptr)
        {
            logtw("Failed to update schedule (Could not create schedule): %s - %s", new_schedule_file_info._file_path.CStr(), err.CStr());
            return false;
        }

        // Check Stream Changed
        if (old_schedule->GetStream() != new_schedule->GetStream())
        {
            logtw("%s <Stream> cannot be changed while running. (%s -> %s)", new_schedule_file_info._file_path.CStr(), old_schedule->GetStream()._name.CStr(), new_schedule->GetStream()._name.CStr());

            return false;
        }

        auto stream = std::static_pointer_cast<ScheduledStream>(GetStreamByName(new_schedule->GetStream()._name));
        if (stream == nullptr)
        {
            logtw("Failed to update schedule (%s stream not found ) : %s", new_schedule_file_info._file_path.CStr(), new_schedule->GetStream()._name.CStr());
        }

        if (stream->UpdateSchedule(new_schedule) == false)
        {
            logtw("Failed to update schedule (%s stream failed to update schedule) : %s", new_schedule_file_info._file_path.CStr(), new_schedule->GetStream()._name.CStr());
            return false;
        }

        // Update file info to DB
        new_schedule_file_info._schedule = new_schedule;

        return true;
    }

    bool ScheduledApplication::RemoveSchedule(ScheduleFileInfo &schedule_file_info)
    {
        auto stream_name = schedule_file_info._schedule->GetStream()._name;

        // Remove Stream
        auto stream = std::static_pointer_cast<ScheduledStream>(GetStreamByName(stream_name));
        if (stream == nullptr)
        {
            logte("Failed to remove schedule (Could not find %s stream): %s", stream_name.CStr(), schedule_file_info._file_path.CStr());
            return false;
        }

        if (DeleteStream(stream) == false)
        {
            logte("Failed to remove schedule (Could not delete %s stream): %s", stream_name.CStr(), schedule_file_info._file_path.CStr());
            return false;
        }

        return true;
    }
}
