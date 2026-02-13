//==============================================================================
//
//  MultiplexStream
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================

#include "multiplex_stream.h"
#include "multiplex_private.h"

#include <base/provider/application.h>

namespace pvd
{
    // Implementation of MultiplexStream
    std::shared_ptr<MultiplexStream> MultiplexStream::Create(const std::shared_ptr<Application> &application, const info::Stream &stream_info, const std::shared_ptr<MultiplexProfile> &multiplex_profile)
    {
        auto stream = std::make_shared<pvd::MultiplexStream>(application, stream_info, multiplex_profile);
        return stream;
    }

    MultiplexStream::MultiplexStream(const std::shared_ptr<Application> &application, const info::Stream &info, const std::shared_ptr<MultiplexProfile> &multiplex_profile)
        : Stream(application, info), _multiplex_profile(multiplex_profile)
    {
    }

    MultiplexStream::~MultiplexStream()
    {
        Stop();
    }

    bool MultiplexStream::Start()
    {
        // Create Worker
        _worker_thread_running = true;
        _worker_thread = std::thread(&MultiplexStream::WorkerThread, this);
        pthread_setname_np(_worker_thread.native_handle(), "Multiplex");

        return Stream::Start();
    }

    bool MultiplexStream::Stop()
    {
        ReleaseSourceStreams();

        if (_worker_thread_running == false)
        {
            return true;
        }

        _worker_thread_running = false;

        if (_worker_thread.joinable())
        {
            _worker_thread.join();
        }

        return Stream::Stop();
    }

    bool MultiplexStream::Terminate()
    {
        logti("Multiplex Channel : %s/%s: Terminated, it will be deleted by application", GetApplicationName(), GetName().CStr());
        ReleaseSourceStreams();

        return Stream::Terminate();
    }

    MultiplexStream::MuxState MultiplexStream::GetMuxState() const
    {
        return _mux_state;
    }

    ov::String MultiplexStream::GetMuxStateStr() const
    {
        switch (_mux_state)
        {
        case MuxState::None:
            return "None";
        case MuxState::Pulling:
            return "Pulling";
        case MuxState::Playing:
            return "Playing";
        case MuxState::Stopped:
            return "Stopped";
        }

        return "Unknown";
    }

    ov::String MultiplexStream::GetPullingStateMsg() const
    {
        return _pulling_state_msg;
    }

    std::shared_ptr<MultiplexProfile> MultiplexStream::GetProfile() const
    {
        return _multiplex_profile;
    }

    void MultiplexStream::WorkerThread()
    {
		ov::logger::ThreadHelper thread_helper;

        while (_worker_thread_running)
        {
            _mux_state = MuxState::Pulling;
            if (PullSourceStreams() == false)
            {
                // sleep and retry
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }

            break;
        }

        while (_worker_thread_running)
        {
            _mux_state = MuxState::Playing;
            bool break_loop = false;
            // Get Streams and Push
            auto source_streams = _multiplex_profile->GetSourceStreams();
            for (auto &source_stream : source_streams)
            {
                auto stream_tap = source_stream->GetStreamTap();
                if (stream_tap == nullptr || stream_tap->GetState() != MediaRouterStreamTap::State::Tapped)
                {
                    logte("Multiplex Channel : %s/%s: Stream [%s] is untapped", GetApplicationName(), GetName().CStr(), source_stream->GetUrlStr().CStr());
                    Terminate();
                    break_loop = true;
                    break;
                }

                auto media_packet = stream_tap->Pop(100);
                if (media_packet == nullptr)
                {
                    continue;
                }

				if (IsPublished() == false)
				{
					if (Publish() == false)
					{
						break_loop = true;
						break;
					}
				}

                auto source_track_id = MakeSourceTrackIdUnique(stream_tap->GetId(), media_packet->GetTrackId());
                auto new_track_id = GetNewTrackId(source_track_id);
                if (new_track_id == 0)
                {
                    continue;
                }

                media_packet->SetTrackId(new_track_id);

                SendFrame(media_packet);
            }

            if (break_loop)
            {
                break;
            }
        }

        logti("Multiplex Channel : %s/%s: Worker thread stopped", GetApplicationName(), GetName().CStr());
    }

    uint64_t MultiplexStream::MakeSourceTrackIdUnique(uint32_t tap_id, uint32_t track_id) const
    {
        return (static_cast<uint64_t>(tap_id) << 32) | static_cast<uint64_t>(track_id);
    }

    uint32_t MultiplexStream::GetNewTrackId(uint64_t source_track_id) const
    {
        auto it = _source_track_id_to_new_id_map.find(source_track_id);
        if (it == _source_track_id_to_new_id_map.end())
        {
            return 0;
        }

        return it->second;
    }

    bool MultiplexStream::PullSourceStreams()
    {
        auto source_streams = _multiplex_profile->GetSourceStreams();
        for (auto &source_stream : source_streams)
        {
            auto stream_tap = source_stream->GetStreamTap();
            if (stream_tap == nullptr)
            {
                continue;
            }

			stream_tap->SetNeedPastData(true);
			stream_tap->Start();

            if (stream_tap->GetState() != MediaRouterStreamTap::State::Tapped)
            {
                auto stream_url = source_stream->GetUrl();
                auto vhost_app_name = info::VHostAppName(stream_url->Host(), stream_url->App());

                if (ocst::Orchestrator::GetInstance()->CheckIfStreamExist(vhost_app_name, stream_url->Stream()) == false)
                {
                    _pulling_state_msg = ov::String::FormatString("Multiplex Channel : %s/%s: Wait for stream %s", GetApplicationName(), GetName().CStr(), stream_url->Stream().CStr());
                    logti("%s", _pulling_state_msg.CStr());

					ReleaseSourceStreams();
                    return false;
                }

                auto result = ocst::Orchestrator::GetInstance()->MirrorStream(stream_tap, vhost_app_name, stream_url->Stream(), MediaRouterInterface::MirrorPosition::Outbound);

                if (result != CommonErrorCode::SUCCESS)
                {
                    _pulling_state_msg = ov::String::FormatString("Multiplex Channel : %s/%s: Failed to mirror stream %s (err : %d)", GetApplicationName(), GetName().CStr(), source_stream->GetUrlStr().CStr(), static_cast<int>(result));
                    logte("%s", _pulling_state_msg.CStr());

					ReleaseSourceStreams();
                    return false;
                }
            }
        }

        // Make tracks
        for (auto &source_stream : source_streams)
        {
            auto stream_tap = source_stream->GetStreamTap();
            if (stream_tap == nullptr)
            {
                continue;
            }

            auto stream_info = stream_tap->GetStreamInfo();
            if (stream_info == nullptr)
            {
                continue;
            }

            auto tracks = stream_info->GetTracks();
            for (auto &[source_track_id, source_track] : tracks)
            {
                auto source_track_name = source_track->GetVariantName();
                MultiplexProfile::NewTrackInfo new_track_info;

                if (source_stream->GetNewTrackInfo(source_track_name, new_track_info) == false)
                {
                    continue;
                }

                auto new_track = source_track->Clone();
                new_track->SetId(IssueUniqueTrackId());
                new_track->SetVariantName(new_track_info.new_track_name);
                if (new_track_info.bitrate_conf > 0)
                {
                    new_track->SetBitrateByConfig(new_track_info.bitrate_conf);
                }
                if (new_track_info.framerate_conf > 0)
                {
                    new_track->SetFrameRateByConfig(new_track_info.framerate_conf);
                }

                AddTrack(new_track);
                _source_track_id_to_new_id_map.emplace(MakeSourceTrackIdUnique(stream_tap->GetId(), source_track_id), new_track->GetId());

                logti("Multiplex Stream : %s/%s: Added track %s from %s/%s (%d)", GetApplicationName(), GetName().CStr(), new_track->GetVariantName().CStr(), source_stream->GetUrlStr().CStr(), source_track_name.CStr(), source_track_id);
            }
        }
		
        // Make Playlist
        auto playlists = _multiplex_profile->GetPlaylists();
        for (auto &playlist : playlists)
        {
            AddPlaylist(playlist);
        }

        logti("Multiplex Channel : %s/%s: Started\n%s", GetApplicationName(), GetName().CStr(), _multiplex_profile->InfoStr().CStr());

        return true;
    }

    bool MultiplexStream::ReleaseSourceStreams()
    {
        auto source_streams = _multiplex_profile->GetSourceStreams();
        for (auto &source_stream : source_streams)
        {
            auto stream_tap = source_stream->GetStreamTap();
            if (stream_tap == nullptr)
            {
                continue;
            }

            if (stream_tap->GetState() != MediaRouterStreamTap::State::Tapped)
            {
                continue;
            }

            stream_tap->Stop();

            auto result = ocst::Orchestrator::GetInstance()->UnmirrorStream(stream_tap);
            if (result != CommonErrorCode::SUCCESS)
            {
                logte("Multiplex Channel : %s/%s: Failed to unmirror stream %s (err : %d)", GetApplicationName(), GetName().CStr(), source_stream->GetUrlStr().CStr(), static_cast<int>(result));
                return false;
            }
        }

        return true;
    }

	bool MultiplexStream::Publish()
	{
		// Now
		SetPublishedTime(std::chrono::system_clock::now());

        // Publish stream
        if (GetApplication()->AddStream(GetSharedPtr()) == false)
        {
            logte("Multiplex Channel : %s/%s: Failed to publish stream", GetApplicationName(), GetName().CStr());
            Terminate();
            return false;
        }

		_is_published = true;

		return true;
	}
	
	bool MultiplexStream::IsPublished() const
	{
		return _is_published;
	}
}