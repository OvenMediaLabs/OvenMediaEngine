//==============================================================================
//
//  MultiplexApplication
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================

#include "multiplex_application.h"
#include "multiplex_provider_private.h"
#include "multiplex_stream.h"
#include "multiplex_profile.h"

#include <base/ovlibrary/files.h>

namespace pvd
{
    // Implementation of MultiplexApplication
    std::shared_ptr<MultiplexApplication> MultiplexApplication::Create(const std::shared_ptr<Provider> &provider, const info::Application &application_info)
    {
        auto application = std::make_shared<pvd::MultiplexApplication>(provider, application_info);
        if (!application->Start())
        {
            return nullptr;
        }

        return application;
    }

    MultiplexApplication::MultiplexApplication(const std::shared_ptr<Provider> &provider, const info::Application &info)
        : Application(provider, info)
    {
    }

    bool MultiplexApplication::Start()
    {
        auto config = GetConfig().GetProviders().GetMultiplexProvider();

        _multiplex_files_path = ov::GetDirPath(config.GetMuxFilesDir(), cfg::ConfigManager::GetInstance()->GetConfigPath());
        _multiplex_file_name_regex = ov::Regex::CompiledRegex(ov::Regex::WildCardRegex(ov::String::FormatString("*.%s", MultiplexFileExtension)));
        
		_packet_silence_timeout_ms = config.GetPacketSilenceTimeoutMs();

        return Application::Start();
    }

    bool MultiplexApplication::Stop()
    {
        return Application::Stop();
    }

    std::map<ov::String, std::shared_ptr<MultiplexStream>> MultiplexApplication::GetMultiplexStreams()
    {
        std::shared_lock<std::shared_mutex> lock(_multiplex_streams_mutex);
        return _multiplex_streams;
    }

    std::shared_ptr<MultiplexStream> MultiplexApplication::GetMultiplexStream(const ov::String &stream_name)
    {
        std::shared_lock<std::shared_mutex> lock(_multiplex_streams_mutex);
        auto it = _multiplex_streams.find(stream_name);
        if (it == _multiplex_streams.end())
        {
            return nullptr;
        }

        return it->second;
    }

    // Called by MultiplexProvider thread every 500ms 
    void MultiplexApplication::OnMultiplexWatcherTick()
    {
        // List all files in multiplex file root
        auto multiplex_file_info_list_from_dir = SearchMultiplexFileInfoFromDir(_multiplex_files_path);

        // Check if file is added or updated
        for (auto &multiplex_file_info_from_dir : multiplex_file_info_list_from_dir)
        {
            MultiplexFileInfo multiplex_file_info_from_db;
            if (GetMultiplexFileInfoFromDB(multiplex_file_info_from_dir._file_path.Hash(), multiplex_file_info_from_db) == false)
            {
                // New file
                if (AddMultiplex(multiplex_file_info_from_dir) == true)
                {
                    _multiplex_file_info_db.emplace(multiplex_file_info_from_dir._file_path.Hash(), multiplex_file_info_from_dir);
                    logti("Added multiplex channel : %s/%s (%s)", GetVHostAppName().CStr(), multiplex_file_info_from_dir._multiplex_profile->GetOutputStreamName().CStr(), multiplex_file_info_from_dir._file_path.CStr());
                }
            }
            else
            {
                multiplex_file_info_from_dir._deleted_checked_count = 0;
                // Updated?
                if (multiplex_file_info_from_dir._file_stat.st_mtime != multiplex_file_info_from_db._file_stat.st_mtime)
                {
                    if (UpdateMultiplex(multiplex_file_info_from_db, multiplex_file_info_from_dir) == true)
                    {
                        _multiplex_file_info_db[multiplex_file_info_from_dir._file_path.Hash()] = multiplex_file_info_from_dir;
                        logti("Updated multiplex channel : %s/%s (%s)", GetVHostAppName().CStr(), multiplex_file_info_from_dir._multiplex_profile->GetOutputStreamName().CStr(), multiplex_file_info_from_dir._file_path.CStr());
                    }
                }
            }
        }

        // Check if file is deleted or stream is terminated
        for (auto it = _multiplex_file_info_db.begin(); it != _multiplex_file_info_db.end();)
        {
            bool found = false;
            auto &multiplex_file_info = it->second;

            // Get file stat
            struct stat file_stat = {0};
            auto result = stat(multiplex_file_info._file_path.CStr(), &file_stat);
            if (result != 0 && errno == ENOENT)
            {
                found = true;
            }

            // Check if stream is terminated
            auto stream = std::static_pointer_cast<MultiplexStream>(GetStreamByName(multiplex_file_info._multiplex_profile->GetOutputStreamName()));
            if (stream != nullptr && stream->GetState() == Stream::State::TERMINATED)
            {
                found = true;
            }
			else if (stream != nullptr)
			{
				if (_packet_silence_timeout_ms > 0)
				{
					// Check if stream is silent
					auto stream_metrics = StreamMetrics(*std::static_pointer_cast<info::Stream>(stream));
					if (stream_metrics != nullptr)
					{
						auto current = std::chrono::high_resolution_clock::now();
						auto elapsed_time_from_last_recv = std::chrono::duration_cast<std::chrono::milliseconds>(current - stream_metrics->GetLastRecvTime()).count();

						if (elapsed_time_from_last_recv > _packet_silence_timeout_ms)
						{
							found = true;
							// Delete immediately 
							multiplex_file_info._deleted_checked_count = 2;
						
							logtw("Multiplex stream is silent for %d ms, removing: %s/%s (%s)", _packet_silence_timeout_ms, GetVHostAppName().CStr(), multiplex_file_info._multiplex_profile->GetOutputStreamName().CStr(), multiplex_file_info._file_path.CStr());
						}
					}
				}
			}

            if (found == true)
            {
                // VI Editor ":wq" will delete file and create new file, 
                // so we need to check if file is really deleted by checking ENOENT 3 times
                multiplex_file_info._deleted_checked_count++;
                if (multiplex_file_info._deleted_checked_count < 3)
                {
                    ++it;
                    continue;
                }

                RemoveMultiplex(multiplex_file_info);
                logti("Removed multiplex channel : %s/%s (%s)", GetVHostAppName().CStr(), multiplex_file_info._multiplex_profile->GetOutputStreamName().CStr(), multiplex_file_info._file_path.CStr());

                it = _multiplex_file_info_db.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    std::vector<MultiplexApplication::MultiplexFileInfo> MultiplexApplication::SearchMultiplexFileInfoFromDir(const ov::String &multiplex_file_root) const
    {
        std::vector<MultiplexFileInfo> multiplex_files;

        auto [success, file_list] = ov::GetFileList(multiplex_file_root);
        if (success == false)
        {
            return multiplex_files;
        }

        for (auto &file_path : file_list)
        {
            if (_multiplex_file_name_regex.Matches(file_path).IsMatched() == false)
            {
                continue;
            }

            MultiplexFileInfo multiplex_file_info;
            multiplex_file_info._file_path = file_path;

            if (stat(file_path.CStr(), &multiplex_file_info._file_stat) != 0)
            {
                continue;
            }

            if (S_ISREG(multiplex_file_info._file_stat.st_mode) == false)
            {
                continue;
            }

            logtt("Found multiplex file : %s, mtime : %d, hash : %d", multiplex_file_info._file_path.CStr(), multiplex_file_info._file_stat.st_mtime, multiplex_file_info._file_path.Hash());

            multiplex_files.push_back(multiplex_file_info);
        }

        return multiplex_files;
    }

    bool MultiplexApplication::GetMultiplexFileInfoFromDB(const size_t &hash, MultiplexFileInfo &multiplex_file_info)
    {
        auto it = _multiplex_file_info_db.find(hash);
        if (it == _multiplex_file_info_db.end())
        {
            return false;
        }

        multiplex_file_info = it->second;
        return true;
    }

    bool MultiplexApplication::AddMultiplex(MultiplexFileInfo &multiplex_file_info)
    {
        auto [multiplex_profile, msg] = MultiplexProfile::CreateFromXMLFile(multiplex_file_info._file_path);
        if (multiplex_profile == nullptr)
        {
            logte("Failed to add multiplex (Could not create multiplex): %s - %s", multiplex_file_info._file_path.CStr(), msg.CStr());
            return false;
        }

        // Create Stream
        auto stream_info = info::Stream(*this, IssueUniqueStreamId(), StreamSourceType::Multiplex);
        stream_info.SetName(multiplex_profile->GetOutputStreamName());

		if (multiplex_profile->IsBypassTranscoder())
		{
        	stream_info.SetRepresentationType(StreamRepresentationType::Relay);
		}
		else
		{
			stream_info.SetRepresentationType(StreamRepresentationType::Source);
		}

        auto stream = MultiplexStream::Create(GetSharedPtrAs<Application>(), stream_info, multiplex_profile);
        if (stream == nullptr)
        {
            logte("Failed to add multiplex (Could not create stream): %s", multiplex_file_info._file_path.CStr());
            return false;
        }

        if (stream->Start() == false)
        {
            logte("Failed to add multiplex (Could not start stream): %s", multiplex_file_info._file_path.CStr());
            return false;
        }

        // later stream will call AddStream by itself when it is ready
        {
            std::lock_guard<std::shared_mutex> lock(_multiplex_streams_mutex);
            _multiplex_streams.emplace(stream_info.GetName(), stream);
        }

        // Add file info to DB 
        multiplex_file_info._multiplex_profile = multiplex_profile;

        return true;
    }

    bool MultiplexApplication::UpdateMultiplex(MultiplexFileInfo &multiplex_file_info, MultiplexFileInfo &new_multiplex_file_info)
    {
        auto old_profile = multiplex_file_info._multiplex_profile;
        if (old_profile == nullptr)
        {
            logte("Failed to update multiplex (Could not find multiplex): %s", multiplex_file_info._file_path.CStr());
            return false;
        }

        auto [new_profile, msg] = MultiplexProfile::CreateFromXMLFile(new_multiplex_file_info._file_path);
        if (new_profile == nullptr)
        {
            logte("Failed to update multiplex (Could not create multiplex): %s - %s", multiplex_file_info._file_path.CStr(), msg.CStr());
            return false;
        }

        if (new_profile == old_profile)
        {
            new_multiplex_file_info._multiplex_profile = new_profile;
            logti("File is modified but profile is not changed : %s", multiplex_file_info._file_path.CStr());
            return true;
        }

        // Multiplex Provider will delete old stream and create new stream

        auto stream = GetMultiplexStream(old_profile->GetOutputStreamName());
        if (stream == nullptr)
        {
            logte("Failed to update multiplex (Could not find %s stream): %s", old_profile->GetOutputStreamName().CStr(), multiplex_file_info._file_path.CStr());
            return false;
        }

        {
            // Remove from stream list
            std::lock_guard<std::shared_mutex> lock(_multiplex_streams_mutex);
            _multiplex_streams.erase(old_profile->GetOutputStreamName());
        }

        // Remove from application
        if (GetStreamByName(old_profile->GetOutputStreamName()) != nullptr)
        {
            if (DeleteStream(stream) == false)
            {
                logte("Failed to update multiplex (Could not delete %s stream): %s", old_profile->GetOutputStreamName().CStr(), multiplex_file_info._file_path.CStr());
                return false;
            }
        }
        else
        {
            // just stop since stream has not been added to application
            stream->Stop();
        }

        // Create new stream
        // Create Stream
        auto stream_info = info::Stream(*this, IssueUniqueStreamId(), StreamSourceType::Multiplex);
        stream_info.SetName(new_profile->GetOutputStreamName());
        stream_info.SetRepresentationType(StreamRepresentationType::Relay);

        auto new_stream = MultiplexStream::Create(GetSharedPtrAs<Application>(), stream_info, new_profile);
        if (new_stream == nullptr)
        {
            logte("Failed to add multiplex (Could not create stream): %s", multiplex_file_info._file_path.CStr());
            return false;
        }

        if (new_stream->Start() == false)
        {
            logte("Failed to add multiplex (Could not start stream): %s", multiplex_file_info._file_path.CStr());
            return false;
        }

        {
            // later stream will call AddStream by itself when it is ready
            std::lock_guard<std::shared_mutex> lock(_multiplex_streams_mutex);
            _multiplex_streams.emplace(stream_info.GetName(), new_stream);
        }

        new_multiplex_file_info._multiplex_profile = new_profile;

        return true;
    }

    bool MultiplexApplication::RemoveMultiplex(MultiplexFileInfo &multiplex_file_info)
    {
        auto stream_name = multiplex_file_info._multiplex_profile->GetOutputStreamName();

        {
            auto mux_stream = GetMultiplexStream(stream_name);
            if (mux_stream != nullptr)
            {
                mux_stream->Stop();
            }

            std::lock_guard<std::shared_mutex> lock(_multiplex_streams_mutex);
            _multiplex_streams.erase(stream_name);
        }

         // Remove Stream if it published
        auto stream = std::static_pointer_cast<MultiplexStream>(GetStreamByName(stream_name));
        if (stream != nullptr)
        {
            if (DeleteStream(stream) == false)
            {
                logte("Failed to remove multiplex (Could not delete %s stream): %s", stream_name.CStr(), multiplex_file_info._file_path.CStr());
                return false;
            }
        }

        return true;
    }
}
