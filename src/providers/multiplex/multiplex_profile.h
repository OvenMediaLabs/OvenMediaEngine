//==============================================================================
//
//  Multiplex
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <base/common_types.h>
#include <pugixml-1.9/src/pugixml.hpp>

#include <base/info/playlist.h>
#include <mediarouter/mediarouter_stream_tap.h>

/*
<?xml version="1.0" encoding="UTF-8"?>
<Multiplex>
    
    <OutputStream>
        <Name>tv1</Name>
		<BypassTranscoder>true</BypassTranscoder> <!-- defualt : true -->
    </OutputStream>

    <Playlists> <!-- only available when BypassTranscoder is true -->
        <Playlist>
            <Name>LLHLS ABR</Name>
            <FileName>abr</FileName> <!-- FileName must be unique in the application -->
            
            <Rendition>
                <Name>1080p</Name>
                <Video>tv1_video</Video>
                <Audio>tv1_audio</Audio>
            </Rendition>
            <Rendition>
                <Name>720p</Name>
                <Video>tv3_video</Video>
                <Audio>tv3_audio</Audio>
            </Rendition>
        </Playlist>
    </Playlists>

    <SourceStreams>
        <SourceStream>
            <Name>tv1</Name>
            <Url>stream://default/live/tv1</Url>
            <TrackMap>
                <Track>
                    <SourceTrackName>bypass_video</SourceTrackName>
                    <NewTrackName>tv1_video</NewTrackName>
                </Track>
                <Track>
                    <SourceTrackName>bypass_audio</SourceTrackName>
                    <NewTrackName>tv1_audio</NewTrackName>
                    <!-- PublicName, Language, Characteristics are optional audio metadata -->
                    <!-- <PublicName>Korean</PublicName> -->
                    <!-- <Language>kor</Language> -->
                    <!-- <Characteristics>public.accessibility.describes-video</Characteristics> -->
                </Track>
                <Track>
                    <SourceTrackName>opus</SourceTrackName>
                    <NewTrackName>tv1_opus</NewTrackName>
                </Track>
            </TrackMap>
        </SourceStream>
        <SourceStream>
            <Name>tv2</Name>
            <Url>stream://default/live/tv2</Url>

            <TrackMap>
                <Track>
                    <SourceTrackName>bypass_video</SourceTrackName>
                    <NewTrackName>tv2_video</NewTrackName>
                </Track>
                <Track>
                    <SourceTrackName>bypass_audio</SourceTrackName>
                    <NewTrackName>tv2_audio</NewTrackName>
                </Track>
                <Track>
                    <SourceTrackName>opus</SourceTrackName>
                    <NewTrackName>tv2_opus</NewTrackName>
                </Track>
            </TrackMap>

        </SourceStream>
        <SourceStream>
            <Name>tv3</Name>
            <Url>stream://default/live/tv3</Url>

            <TrackMap>
                <Track>
                    <SourceTrackName>bypass_video</SourceTrackName>
                    <NewTrackName>tv3_video</NewTrackName>
                </Track>
                <Track>
                    <SourceTrackName>bypass_audio</SourceTrackName>
                    <NewTrackName>tv3_audio</NewTrackName>
                </Track>
                <Track>
                    <SourceTrackName>opus</SourceTrackName>
                    <NewTrackName>tv3_opus</NewTrackName>
                </Track>
            </TrackMap>

        </SourceStream>
    </SourceStreams>
    
</Multiplex>
*/

namespace pvd
{
    constexpr const char* MultiplexFileExtension = "mux";
    
    class MultiplexProfile
    {
    public:

        struct NewTrackInfo
        {
            // constructor
            NewTrackInfo() = default;
            NewTrackInfo(ov::String source_track_name, ov::String new_track_name, int32_t bitrate_conf = 0, int32_t framerate_conf = 0,
                         ov::String public_name = "", ov::String language = "", ov::String characteristics = "", int32_t audio_index = -1)
                : source_track_name(source_track_name), new_track_name(new_track_name), bitrate_conf(bitrate_conf), framerate_conf(framerate_conf),
                  public_name(public_name), language(language), characteristics(characteristics), audio_index(audio_index)
            {
            }

            // == operator
            bool operator==(const NewTrackInfo &other) const
            {
                if (source_track_name != other.source_track_name)
                    return false;
                if (new_track_name != other.new_track_name)
                    return false;
                if (bitrate_conf != other.bitrate_conf)
                    return false;
                if (framerate_conf != other.framerate_conf)
                    return false;
                if (public_name != other.public_name)
                    return false;
                if (language != other.language)
                    return false;
                if (characteristics != other.characteristics)
                    return false;
                if (audio_index != other.audio_index)
                    return false;
                return true;
            }

            // != operator
            bool operator!=(const NewTrackInfo &other) const
            {
                return !(*this == other);
            }

            ov::String source_track_name;
            ov::String new_track_name;

            int32_t bitrate_conf = 0;
            int32_t framerate_conf = 0;

            // Audio track metadata for HLS EXT-X-MEDIA
            ov::String public_name;      // NAME attribute displayed in the player
            ov::String language;          // LANGUAGE attribute (e.g. "kor", "eng", "fra")
            ov::String characteristics;   // CHARACTERISTICS attribute

            // -1 = apply to all occurrences of this SourceTrackName
            // >= 0 = apply only to the Nth occurrence (0-based)
            int32_t audio_index = -1;
        };

        class SourceStream
        {
        public:
            ov::String GetName() const
            {
                return _name;
            }

            ov::String GetUrlStr() const
            {
                return _url_str;
            }

            std::shared_ptr<ov::Url> GetUrl() const
            {
                return _url;
            }

            const std::map<ov::String, std::vector<NewTrackInfo>> &GetTrackMap() const
            {
                return _new_track_infos_map;
            }

            // Returns the NewTrackInfo for the Nth occurrence of source_track_name.
            // audio_index == -1 is the only catch-all value; any other negative value is invalid.
            // Lookup order:
            //   1. Explicit match: entry whose audio_index == occurrence.
            //   2. Catch-all fallback: first entry with audio_index == -1 (if no explicit match).
            bool GetNewTrackInfo(const ov::String &source_track_name, int occurrence, NewTrackInfo &new_track_info) const
            {
                auto it = _new_track_infos_map.find(source_track_name);
                if (it == _new_track_infos_map.end())
                {
                    return false;
                }

                const auto &infos = it->second;

                // First pass: look for an explicit index match
                for (const auto &info : infos)
                {
                    if (info.audio_index == occurrence)
                    {
                        new_track_info = info;
                        return true;
                    }
                }

                // Second pass: fallback to catch-all (audio_index == -1)
                for (const auto &info : infos)
                {
                    if (info.audio_index == -1)
                    {
                        new_track_info = info;
                        return true;
                    }
                }

                return false;
            }

            std::shared_ptr<MediaRouterStreamTap> GetStreamTap()
            {
                if (_stream_tap == nullptr)
                {
                    _stream_tap = MediaRouterStreamTap::Create();
                }

                return _stream_tap;
            }

            // Setter
            void SetName(const ov::String &name)
            {
                _name = name;
            }

            bool SetUrl(const ov::String &url)
            {
                _url_str = url;
                _url = ov::Url::Parse(url);
                if (_url == nullptr)
                {
                    return false;
                }

                return true;
            }

            void AddTrackMap(const ov::String &source_track_name, const NewTrackInfo &new_track_info)
            {
                _new_track_infos_map[source_track_name].push_back(new_track_info);
            }

            // equal operator
            bool operator==(const SourceStream &other) const
            {
                if (_name != other._name)
                    return false;
                if (_url_str != other._url_str)
                    return false;
                if (_new_track_infos_map.size() != other._new_track_infos_map.size())
                    return false;

                for (const auto &[key, infos] : _new_track_infos_map)
                {
                    auto it = other._new_track_infos_map.find(key);
                    if (it == other._new_track_infos_map.end())
                        return false;
                    if (it->second != infos)
                        return false;
                }

                return true;
            }

            // not equals operator
            bool operator!=(const SourceStream &other) const
            {
                return !(*this == other);
            }

        private:
            ov::String _name;
            ov::String _url_str;
            std::shared_ptr<ov::Url> _url;

            std::shared_ptr<MediaRouterStreamTap> _stream_tap = nullptr;

            // source track name → list of NewTrackInfo (one per audio_index, or a single catch-all entry)
            std::map<ov::String, std::vector<NewTrackInfo>> _new_track_infos_map;
        };

        static std::tuple<std::shared_ptr<MultiplexProfile>, ov::String> CreateFromXMLFile(const ov::String &file_path);
        static std::tuple<std::shared_ptr<MultiplexProfile>, ov::String> CreateFromJsonObject(const Json::Value &object);

        MultiplexProfile() = default;
        ~MultiplexProfile() = default;

        bool LoadFromXMLFile(const ov::String &file_path);
        bool LoadFromJsonObject(const Json::Value &object);

        std::shared_ptr<MultiplexProfile> Clone() const;

        ov::String GetLastError() const;
        
        ov::String GetFilePath() const;
        std::chrono::system_clock::time_point GetCreatedTime() const;
        ov::String GetOutputStreamName() const;
		bool IsBypassTranscoder() const;
        const std::vector<std::shared_ptr<info::Playlist>> &GetPlaylists() const;
        const std::vector<std::shared_ptr<SourceStream>> &GetSourceStreams() const;

        CommonErrorCode SaveToXMLFile(const ov::String &file_path) const;
        CommonErrorCode ToJsonObject(Json::Value &root_object) const;

        // equal operator
        bool operator==(const MultiplexProfile &other) const;

        ov::String InfoStr() const;

    private:
        bool ReadOutputStreamNode(const pugi::xml_node &root_node);
        bool ReadPlaylistsNode(const pugi::xml_node &root_node);
        bool ReadSourceStreamsNode(const pugi::xml_node &root_node);

        bool ReadOutputStreamObject(const Json::Value &object);
        bool ReadPlaylistsObject(const Json::Value &object);
        bool ReadSourceStreamsObject(const Json::Value &object);
        
        std::chrono::system_clock::time_point _created_time;
        
        ov::String _file_path; // when loaded from file

        ov::String _output_stream_name;
		bool _bypass_transcoder = true;
        std::vector<std::shared_ptr<info::Playlist>> _playlists;
        std::vector<std::shared_ptr<SourceStream>> _source_streams;

        // For validation of new track names and playlist names
        std::map<ov::String, bool> _new_track_names;

        mutable ov::String _last_error;
    };
}