//==============================================================================
//
//  Schedule
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/common_types.h>
#include <base/info/audio_map_item.h>
#include <base/ovlibrary/ovlibrary.h>
#include <modules/ffmpeg/compat.h>

#include <pugixml-1.9/src/pugixml.hpp>

/*
<?xml version="1.0" encoding="UTF-8"?>
<Schedule>
    <Stream>
        <StreamName>tv1</StreamName> <!-- required -->
        <BypassTranscoder>false</BypassTranscoder> <!-- optional, default : false -->
        <VideoTrack>true</VideoTrack> <!-- optional, default : true -->
        <AudioTrack>true</AudioTrack> <!-- optional, default : true -->

		<ErrorToleranceDurationMs>1000</ErrorToleranceDurationMs>
    </Stream>

    <FallbackProgram>
        <Item url="file://sample.mp4" start="0" duration="60000" fallbackOnErr="true"/> <!-- Milliseconds -->
        <Item url="stream://default/app/stream1" duration="60000" fallbackOnErr="true"/>
    </FallbackProgram>

    <Program scheduled="2023-09-27T00:00:00.000Z" repeat="true"> <!-- optional -->
        <Item url="file://sample.mp4" start="0" duration="60000" fallbackOnErr="true"/> <!-- Milliseconds -->
        <Item url="stream://default/app/stream1" duration="60000"/>
    </Program>
    <Program scheduled="2023-09-27 03:11:22.000Z" repeat="true">
        <Item url="file://sample.mp4" start="0" duration="60000" fallbackOnErr="true"/>
        <Item url="stream://default/app/stream1" duration="60000"/>
        <Item url="file://sample.mp4" start="60000" duration="120000"/>
    </Program>
</Schedule>
*/

namespace pvd
{
	constexpr const char *ScheduleFileExtension = "sch";

	class Schedule
	{
	public:
		class Item
		{
		public:
			Item() = default;
			~Item() = default;

			// == operator
			bool operator==(const Item &rhs) const
			{
				if (_url != rhs._url ||
					_start_time_ms_conf != rhs._start_time_ms_conf ||
					_duration_ms_conf != rhs._duration_ms_conf)
				{
					return false;
				}

				return true;
			}

			// != operator
			bool operator!=(const Item &rhs) const
			{
				return !(*this == rhs);
			}

			ov::String _url;
			ov::String _file_path;
			bool _fallback_on_err = true;  // default true
			bool _file			 = true;
			bool _fallback		 = false;

			// setting values
			int64_t _start_time_ms_conf;
			int64_t _duration_ms_conf;

			// calculated values
			int64_t _start_time_ms = 0;
			int64_t _duration_ms	  = 0;

			// File
			std::shared_ptr<AVFormatContext> LoadContext();
		private:
			std::shared_ptr<AVFormatContext> _format_context;
			struct stat _last_loaded_stat;
		};

		class Stream
		{
		public:
			Stream()  = default;
			~Stream() = default;

			bool operator==(const Stream &rhs) const
			{
				if (_name != rhs._name ||
					_bypass_transcoder != rhs._bypass_transcoder ||
					_video_track != rhs._video_track ||
					_audio_track != rhs._audio_track)
				{
					return false;
				}

				return true;
			}

			bool operator!=(const Stream &rhs) const
			{
				return !(*this == rhs);
			}

			ov::String _name;
			bool _bypass_transcoder = false;
			bool _video_track	   = true;
			bool _audio_track	   = true;

			std::vector<info::AudioMapItem> _audio_map;

			int64_t _error_tolerance_duration_ms = 500;
		};

		class Program
		{
		public:
			Program()  = default;
			~Program() = default;

			std::shared_ptr<Item> GetFirstItemWithPosition();
			std::shared_ptr<Item> GetNextItem();
			std::shared_ptr<Item> GetItem(int index);
			bool IsOffAir() const;

			// == operator
			bool operator==(const Program &rhs) const
			{
				if (_scheduled_time != rhs._scheduled_time ||
					//duration_ms != rhs.duration_ms || // duration_ms can be changed by next program
					_repeat != rhs._repeat ||
					_items.size() != rhs._items.size())
				{
					return false;
				}

				for (size_t i = 0; i < _items.size(); i++)
				{
					if (*_items[i] != *rhs._items[i])
					{
						return false;
					}
				}

				return true;
			}

			// != operator
			bool operator!=(const Program &rhs) const
			{
				return !(*this == rhs);
			}

			ov::String _name;
			ov::String _scheduled;
			std::chrono::system_clock::time_point _scheduled_time;
			int64_t _duration_ms;
			std::chrono::system_clock::time_point _end_time;
			bool _repeat;
			std::vector<std::shared_ptr<Item>> _items;
			int64_t _total_item_duration_ms = 0;
			bool _unlimited_duration		   = false;	 // if live with no duration, it will be played until the end of the program

		private:
			int _current_item_index = 0;
			bool _off_air		   = false;
		};

		static std::tuple<std::shared_ptr<Schedule>, ov::String> CreateFromXMLFile(const ov::String &file_path, const ov::String &media_root_dir);
		static std::tuple<std::shared_ptr<Schedule>, ov::String> CreateFromJsonObject(const Json::Value &object, const ov::String &media_root_dir);

		Schedule()	= default;
		~Schedule() = default;

		bool LoadFromXMLFile(const ov::String &file_path, const ov::String &media_file_root_dir);
		bool LoadFromJsonObject(const Json::Value &object, const ov::String &media_file_root_dir);
		bool PatchFromJsonObject(const Json::Value &object);

		std::shared_ptr<Schedule> Clone() const;

		ov::String GetLastError() const;

		std::chrono::system_clock::time_point GetCreatedTime() const;

		const Stream &GetStream() const;
		const std::shared_ptr<Program> GetFallbackProgram() const;
		const std::vector<std::shared_ptr<Program>> &GetPrograms() const;

		const std::shared_ptr<Program> GetCurrentProgram() const;
		const std::shared_ptr<Program> GetNextProgram() const;

		CommonErrorCode SaveToXMLFile(const ov::String &file_path) const;
		CommonErrorCode ToJsonObject(Json::Value &root_object) const;

	private:
		bool ReadStreamNode(const pugi::xml_node &schedule_node);
		bool ReadFallbackProgramNode(const pugi::xml_node &schedule_node);
		bool ReadProgramNodes(const pugi::xml_node &schedule_node);
		bool ReadItemNodes(const pugi::xml_node &item_parent_node, std::vector<std::shared_ptr<Item>> &items);

		bool WriteItemNodes(const std::vector<std::shared_ptr<Item>> &items, pugi::xml_node &item_parent_node) const;
		bool WriteItemObjects(const std::vector<std::shared_ptr<Item>> &items, Json::Value &item_parent_object) const;

		bool ReadStreamObject(const Json::Value &root_object);
		bool ReadFallbackProgramObject(const Json::Value &root_object);
		bool ReadProgramObjects(const Json::Value &root_object);
		bool ReadItemObjects(const Json::Value &item_parent_object, std::vector<std::shared_ptr<Item>> &items);

		Stream MakeStream(const ov::String &name, bool bypass_transcoder, bool video_track, bool audio_track) const;
		std::shared_ptr<Program> MakeFallbackProgram() const;
		std::shared_ptr<Program> MakeProgram(const ov::String &name, const ov::String &scheduled_time, const ov::String &next_scheduled_time, bool repeat, bool last) const;
		std::shared_ptr<Item> MakeItem(const ov::String &url, int64_t start_time_ms_conf, int64_t duration_ms_conf, bool fallback_on_err) const;

		Stream _stream;
		std::shared_ptr<Program> _fallback_program;
		std::vector<std::shared_ptr<Program>> _programs;

		ov::String _file_path;
		ov::String _file_name_without_ext;
		ov::String _media_root_dir;

		// Created time
		std::chrono::system_clock::time_point _created_time;

		mutable ov::String _last_error;
	};
}  // namespace pvd