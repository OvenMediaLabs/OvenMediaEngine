//==============================================================================
//
//  Schedule
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================

#include "schedule.h"
#include "schedule_private.h"
#include <base/ovlibrary/files.h>

namespace pvd
{
	std::shared_ptr<AVFormatContext> Schedule::Item::LoadContext()
	{
		ov::StopWatch sw;
		sw.Start();

		if (_file == false || _file_path.IsEmpty())
		{
			logti("LoadContext: Not a file item: %s", _file_path.CStr());
			return nullptr;
		}

		if (_format_context != nullptr)
		{
			struct stat current_stat;
			if (stat(_file_path.CStr(), &current_stat) == 0)
			{
				// Check if the file has been modified
				if (current_stat.st_mtime == _last_loaded_stat.st_mtime &&
					current_stat.st_size == _last_loaded_stat.st_size)
				{
					// Not modified
					return _format_context;
				}
				else
				{
					// Modified, close previous context
					_format_context = nullptr;
					logti("LoadContext: File modified, reloading: %s", _file_path.CStr());
				}
			}
		}

		int err = 0;
		AVFormatContext *ctx = nullptr;
		err = ::avformat_open_input(&ctx, _file_path.CStr(), nullptr, nullptr);
		if (err < 0)
		{
			char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
			::av_strerror(err, errbuf, sizeof(errbuf));
			_format_context = nullptr;
			logte("LoadContext: Failed to open file %s. error (%d, %s)", _file_path.CStr(), err,  errbuf);
			return nullptr;
		}

		ov::String file_path_copy = _file_path;
		_format_context.reset(ctx, [file_path_copy](AVFormatContext *ctx) {
			if (ctx)
			{
				logti("LoadContext: Closing format context : %s", file_path_copy.CStr());
				::avformat_close_input(&ctx);
			}
		});

		err = ::avformat_find_stream_info(_format_context.get(), nullptr);
		if (err < 0)
		{
			char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
			::av_strerror(err, errbuf, sizeof(errbuf));
			_format_context = nullptr;
			logte("LoadContext: Failed to find stream info for file %s. error (%d, %s)", _file_path.CStr(), err,  errbuf);
			return nullptr;
		}

		// Record last modified time
		if (stat(_file_path.CStr(), &_last_loaded_stat) != 0)
		{
			_format_context = nullptr;
			logte("LoadContext: Failed to get file stat for file %s.", _file_path.CStr());
			return nullptr;
		}

		logti("LoadContext: File loaded successfully: %s (%" PRId64 " ms)", _file_path.CStr(), sw.Elapsed());

		return _format_context;
	}

	std::shared_ptr<Schedule::Item> Schedule::Program::GetFirstItemWithPosition()
	{
		// Search for the first item by now() - scheduled_time
		if (_items.empty() || (_unlimited_duration == false && _total_item_duration_ms <= 0))
		{
			logte("GetFirstItem: No items or total item duration is zero");
			return nullptr;
		}

		auto now = std::chrono::system_clock::now();
		auto time_elapsed_ms_from_scheduled = std::chrono::duration_cast<std::chrono::milliseconds>(now - _scheduled_time).count();
		int64_t position_ms = time_elapsed_ms_from_scheduled;

		if (_unlimited_duration == false)
		{
			// if position is less than 1 second, it is considered as processing time difference and not seeked
			if (position_ms < 1000)
			{
				position_ms = 0;
			}
			else
			{
				position_ms = time_elapsed_ms_from_scheduled % _total_item_duration_ms;
			}
		} 


		logti("GetFirstItem: Now: %" PRId64 " ms, Scheduled: %" PRId64 " ms, Elapsed: %" PRId64 " ms, Position: %" PRId64 " ms", 
			std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count(),
			std::chrono::duration_cast<std::chrono::milliseconds>(_scheduled_time.time_since_epoch()).count(),
			time_elapsed_ms_from_scheduled, position_ms);

		int64_t current_position = 0;
		for (const auto &item : _items)
		{
			_current_item_index ++;

			if (item->_duration_ms <= 0 ||  // it means unlimited duration like live stream with no duration set
				(current_position <= position_ms && position_ms < current_position + item->_duration_ms))
			{
				// Copy item and set start_time_ms
				auto first_item = std::make_shared<Item>(*item);
				first_item->_start_time_ms = first_item->_start_time_ms_conf + (position_ms - current_position);
				first_item->_duration_ms = item->_duration_ms - (position_ms - current_position);

				logti("Item : %s - GetFirstItem: Found item at position: %" PRId64 " ms, Item start time: %" PRId64 " ms, Item duration: %" PRId64 " ms", 
					item->_url.CStr(), position_ms, first_item->_start_time_ms, first_item->_duration_ms);

				return first_item;
			}

			current_position += item->_duration_ms;
		}

		return nullptr;
	}

	std::shared_ptr<Schedule::Item> Schedule::Program::GetNextItem()
	{
		if (_items.empty())
		{
			return nullptr;
		}

		if (size_t(_current_item_index) >= _items.size())
		{
			if (_repeat)
			{
				_current_item_index = 0;
			}
			else
			{
				_off_air = true;
				return nullptr;
			}
		}

		auto item = _items[_current_item_index];
		_current_item_index++;

		return item;
	}

	std::shared_ptr<Schedule::Item> Schedule::Program::GetItem(int index)
	{
		if (index < 0 || size_t(index) >= _items.size())
		{
			return nullptr;
		}

		auto item = _items[index];

		_current_item_index = index + 1;

		return item;
	}

	bool Schedule::Program::IsOffAir() const
	{
		return _off_air;
	}


	std::tuple<std::shared_ptr<Schedule>, ov::String> Schedule::CreateFromXMLFile(const ov::String &file_path, const ov::String &media_root_dir)
	{
		auto schedule = std::make_shared<Schedule>();
		if (schedule->LoadFromXMLFile(file_path, media_root_dir) == false)
		{
			return {nullptr, schedule->GetLastError()};
		}

		return {schedule, "success"};
	}

	std::tuple<std::shared_ptr<Schedule>, ov::String> Schedule::CreateFromJsonObject(const Json::Value &object, const ov::String &media_root_dir)
	{
		auto schedule = std::make_shared<Schedule>();
		if (schedule->LoadFromJsonObject(object, media_root_dir) == false)
		{
			return {nullptr, schedule->GetLastError()};
		}

		return {schedule, "success"};
	}

	std::shared_ptr<Schedule> Schedule::Clone() const
	{
		auto schedule = std::make_shared<Schedule>();

		Json::Value root_object;
		if (ToJsonObject(root_object) != CommonErrorCode::SUCCESS)
		{
			return nullptr;
		}

		schedule->LoadFromJsonObject(root_object, _media_root_dir);

		return schedule;
	}

	bool Schedule::LoadFromJsonObject(const Json::Value &object, const ov::String &media_file_root_dir)
	{
		_media_root_dir = media_file_root_dir;

		if (ReadStreamObject(object) == false)
		{
			return false;
		}

		if (ReadFallbackProgramObject(object) == false)
		{
			return false;
		}

		if (ReadProgramObjects(object) == false)
		{
			return false;
		}

		_created_time = std::chrono::system_clock::now();

		return true;
	}

	bool Schedule::PatchFromJsonObject(const Json::Value &object)
	{
		if (object.isMember("fallbackProgram"))
		{
			_fallback_program = nullptr;
			if (ReadFallbackProgramObject(object) == false)
			{
				return false;
			}
		}

		if (object.isMember("programs"))
		{
			_programs.clear();
			if (ReadProgramObjects(object) == false)
			{
				return false;
			}
		}

		return true;
	}

	bool Schedule::ReadStreamObject(const Json::Value &root_object)
	{
		ov::String name;
		bool bypass_transcoder = false;
		bool video_track = true;
		bool audio_track = true;

		auto stream_object = root_object["stream"];
		if (stream_object.isNull() || stream_object.isObject() == false)
		{
			_last_error = "Failed to find Stream object";
			return false;
		}

		// Required in Json
		auto name_object = stream_object["name"];
		if (name_object.isNull() || name_object.isString() == false)
		{
			_last_error = "Failed to find Stream.Name object";
			return false;
		}

		name = name_object.asString().c_str();

		auto bypass_transcoder_object = stream_object["bypassTranscoder"];
		if (bypass_transcoder_object.isBool() == true)
		{
			bypass_transcoder = bypass_transcoder_object.asBool();
		}

		auto video_track_object = stream_object["videoTrack"];
		if (video_track_object.isBool() == true)
		{
			video_track = video_track_object.asBool();
		}

		auto audio_track_object = stream_object["audioTrack"];
		if (audio_track_object.isBool() == true)
		{
			audio_track = audio_track_object.asBool();
		}

		_stream = MakeStream(name, bypass_transcoder, video_track, audio_track);

		// audioMap

		/*
			"audioMap": [
				{
					"name": "Korean",
					"language": "kor"
				},
				{
					"name": "English",
					"language": "eng"
				}
			]

			for (uint32_t i=0; i<programs_object.size(); i++)
			{
				auto program_object = programs_object[i];
		
		*/

		auto audio_map_object = stream_object["audioMap"];
		if (audio_map_object.isNull() == false)
		{
			if (audio_map_object.isArray() == false)
			{
				_last_error = "audioMap must be an array";
				return false;
			}

			for (uint32_t i=0; i<audio_map_object.size(); i++)
			{
				auto audio_map_item_object = audio_map_object[i];
				ov::String public_name;
				ov::String language;
				ov::String characteristics;
				
				auto public_name_object = audio_map_item_object["name"];
				if (public_name_object.isNull() == false || public_name_object.isString() == true)
				{
					public_name = public_name_object.asString().c_str();
				}

				auto language_object = audio_map_item_object["language"];
				if (language_object.isNull() == false || language_object.isString() == true)
				{
					language = language_object.asString().c_str();
				}

				auto characteristics_object = audio_map_item_object["characteristics"];
				if (characteristics_object.isNull() == false || characteristics_object.isString() == true)
				{
					characteristics = characteristics_object.asString().c_str();
				}

				_stream._audio_map.push_back({static_cast<int>(i), public_name, language, characteristics});
			}
		}

		// error_tolerance_duration_ms
		auto error_tolerance_duration_ms_object = stream_object["errorToleranceDurationMs"];
		if (error_tolerance_duration_ms_object.isNull() == false || error_tolerance_duration_ms_object.isInt() == true)
		{
			_stream._error_tolerance_duration_ms = error_tolerance_duration_ms_object.asInt();
		}

		return true;
	}

	bool Schedule::ReadFallbackProgramObject(const Json::Value &root_object)
	{
		auto fallback_program_object = root_object["fallbackProgram"];
		if (fallback_program_object.isNull())
		{
			// optional
			return true;
		}

		if (fallback_program_object.isObject() == false)
		{
			_last_error = "FallbackProgram must be an object";
			return false;
		}

		auto fallback_program = MakeFallbackProgram();
		if (fallback_program == nullptr)
		{
			return false;
		}

		if (ReadItemObjects(fallback_program_object, fallback_program->_items) == false)
		{
			return false;
		}

		_fallback_program = fallback_program;

		return true;
	}

	bool Schedule::ReadProgramObjects(const Json::Value &root_object)
	{
		ov::String name;
		bool repeat = false;
		ov::String scheduled;
		ov::String next_scheduled;
		bool last = false;

		auto programs_object = root_object["programs"];
		if (programs_object.isNull())
		{
			// optional
			return true;
		}

		if (programs_object.isArray() == false)
		{
			_last_error = "programs must be an array";
			return false;
		}

		for (uint32_t i=0; i<programs_object.size(); i++)
		{
			auto program_object = programs_object[i];
			Json::Value next_program_object = Json::nullValue;

			if (i + 1 < programs_object.size())
			{
				next_program_object = programs_object[i + 1];
			}
			else
			{
				last = true;
			}

			auto name_object = program_object["name"];
			if (name_object.isNull() == false && name_object.isString() == true)
			{
				name = name_object.asString().c_str();
			}

			auto repeat_object = program_object["repeat"];
			if (repeat_object.isNull() == false || repeat_object.isBool() == true)
			{
				repeat = repeat_object.asBool();
			}

			auto scheduled_object = program_object["scheduled"];
			if (scheduled_object.isNull() || scheduled_object.isString() == false)
			{
				_last_error = "Failed to find scheduled object, scheduled is required";
				return false;
			}

			scheduled = scheduled_object.asString().c_str();

			if (last == false)
			{
				auto next_scheduled_object = next_program_object["scheduled"];
				if (next_scheduled_object.isNull() || next_scheduled_object.isString() == false)
				{
					_last_error = "Failed to find scheduled object, scheduled is required";
					return false;
				}

				next_scheduled = next_scheduled_object.asString().c_str();
			}

			auto program = MakeProgram(name, scheduled, next_scheduled, repeat, last);
			if (program == nullptr)
			{
				return false;
			}

			// If the program has already passed, it is ignored.
			if (program->_end_time < std::chrono::system_clock::now())
			{
				logti("The program has already passed, it is ignored. name: %s, scheduled: %s", program->_name.CStr(), program->_scheduled.CStr());
				continue;
			}

			if (ReadItemObjects(program_object, program->_items) == false)
			{
				return false;
			}

			_programs.push_back(program);
		}

		return true;
	}

	bool Schedule::ReadItemObjects(const Json::Value &item_parent_object, std::vector<std::shared_ptr<Schedule::Item>> &items)
	{
		auto items_object = item_parent_object["items"];
		if (items_object.isNull())
		{
			// it is optional
			return true;
		}

		if (items_object.isArray() == false)
		{
			_last_error = "items must be an array";
			return false;
		}

		for (uint32_t i=0; i<items_object.size(); i++)
		{
			ov::String url;
			int64_t start_time_ms_conf = 0;
			int64_t duration_ms_conf = -1;
			bool fallback_on_err = true;

			auto item_object = items_object[i];

			// url
			auto url_object = item_object["url"];
			if (url_object.isNull() || url_object.isString() == false)
			{
				_last_error = "Failed to find url object, url is required";
				return false;
			}

			url = url_object.asString().c_str();

			// start
			auto start_object = item_object["start"];
			if (start_object.isNull() == false || start_object.isInt() == true)
			{
				start_time_ms_conf = start_object.asInt();
			}

			// duration
			auto duration_object = item_object["duration"];
			if (duration_object.isNull() == false || duration_object.isInt() == true)
			{
				duration_ms_conf = duration_object.asInt();
			}

			// fallbackOnErr
			auto fallback_on_err_object = item_object["fallbackOnErr"];
			if (fallback_on_err_object.isNull() == false || fallback_on_err_object.isBool() == true)
			{
				fallback_on_err = fallback_on_err_object.asBool();
			}

			auto item = MakeItem(url, start_time_ms_conf, duration_ms_conf, fallback_on_err);
			if (item == nullptr)
			{
				return false;
			}

			items.push_back(item);
		}

		return true;
	}

	bool Schedule::LoadFromXMLFile(const ov::String &file_path, const ov::String &media_root_dir)
	{
		pugi::xml_document xml_doc;
		auto load_result = xml_doc.load_file(file_path.CStr());
		if (!load_result)
		{
			_last_error = ov::String::FormatString("Failed to load schedule file: %s", load_result.description());
			return false;
		}

		_file_path = file_path;
		_file_name_without_ext = ov::GetFileNameWithoutExt(file_path);
		_media_root_dir = media_root_dir;

		auto schedule_node = xml_doc.child("Schedule");
		if (!schedule_node)
		{
			_last_error = "Failed to find Schedule node";
			return false;
		}

		if (ReadStreamNode(schedule_node) == false)
		{
			_last_error = "Failed to read Stream node";
			return false;
		}

		if (_stream._name != _file_name_without_ext)
		{
			logtw("Use the file name (%s) as the stream name. It is recommended that <Stream><Name>%s be set the same as the file name.", _file_name_without_ext.CStr(), _stream._name.CStr());
			_stream._name = _file_name_without_ext;
		}

		if (ReadFallbackProgramNode(schedule_node) == false)
		{
			return false;
		}

		if (ReadProgramNodes(schedule_node) == false)
		{
			return false;
		}

		_created_time = std::chrono::system_clock::now();

		return true;
	}

	bool Schedule::ReadStreamNode(const pugi::xml_node &schedule_node)
	{
		ov::String name;
		bool bypass_transcoder = false;
		bool video_track = true;
		bool audio_track = true;

		auto stream_node = schedule_node.child("Stream");
		if (!stream_node)
		{
			_last_error = "Failed to find Stream node";
			return false;
		}

		auto stream_name_node = stream_node.child("Name");
		if (stream_name_node)
		{
			name = stream_name_node.text().as_string();
		}

		auto bypass_transcoder_node = stream_node.child("BypassTranscoder");
		if (bypass_transcoder_node)
		{
			bypass_transcoder = bypass_transcoder_node.text().as_bool();
		}

		auto video_track_node = stream_node.child("VideoTrack");
		if (video_track_node)
		{
			video_track = video_track_node.text().as_bool();
		}

		auto audio_track_node = stream_node.child("AudioTrack");
		if (audio_track_node)
		{
			audio_track = audio_track_node.text().as_bool();
		}

		_stream = MakeStream(name, bypass_transcoder, video_track, audio_track);

		// Optional
		/*
			<AudioMap>
				<Item>
					<Name>Korean</Name>
					<Language>kor</Language>
				</Item>
				<Item>
					<Name>English</Name>
					<Language>eng</Language>
				</Item>
			</AudioMap>
		*/
		auto audio_map_node = stream_node.child("AudioMap");
		uint32_t index = 0;
		for (auto audio_map_item_node = audio_map_node.child("Item"); audio_map_item_node; audio_map_item_node = audio_map_item_node.next_sibling("Item"))
		{
			ov::String public_name;
			ov::String language;
			ov::String characteristics;

			auto public_name_node = audio_map_item_node.child("Name");
			if (public_name_node)
			{
				public_name = public_name_node.text().as_string();
			}

			auto language_node = audio_map_item_node.child("Language");
			if (language_node)
			{
				language = language_node.text().as_string();
			}

			auto characteristics_node = audio_map_item_node.child("Characteristics");
			if (characteristics_node)
			{
				characteristics = characteristics_node.text().as_string();
			}

			_stream._audio_map.push_back({static_cast<int>(index), public_name, language, characteristics});
			index ++;
		}

		// error_tolerance_duration_ms
		auto error_tolerance_duration_ms_node = stream_node.child("ErrorToleranceDurationMs");
		if (error_tolerance_duration_ms_node)
		{
			_stream._error_tolerance_duration_ms = error_tolerance_duration_ms_node.text().as_llong();
		}

		return true;
	}

	bool Schedule::ReadFallbackProgramNode(const pugi::xml_node &schedule_node)
	{
		auto fallback_program_node = schedule_node.child("FallbackProgram");
		if (!fallback_program_node)
		{
			// optional
			return true;
		}

		auto fallback_program = MakeFallbackProgram();
		if (fallback_program == nullptr)
		{
			return false;
		}
		
		if (ReadItemNodes(fallback_program_node, fallback_program->_items) == false)
		{
			return false;
		}

		_fallback_program = fallback_program;

		return true;
	}

	bool Schedule::ReadProgramNodes(const pugi::xml_node &schedule_node)
	{		
		for (auto program_node = schedule_node.child("Program"); program_node; program_node = program_node.next_sibling("Program"))
		{
			auto next_program = program_node.next_sibling("Program");

			ov::String name;
			ov::String scheduled;
			ov::String next_scheduled;
			bool repeat = false;
			bool last = false;

			auto name_attribute = program_node.attribute("name");
			if (name_attribute)
			{
				name = name_attribute.as_string();
			}

			auto scheduled_attribute = program_node.attribute("scheduled");
			if (!scheduled_attribute)
			{
				_last_error = "Failed to find scheduled attribute, scheduled is required";
				return false;
			}

			scheduled = scheduled_attribute.as_string();

			if (next_program)
			{
				auto next_scheduled_attribute = next_program.attribute("scheduled");
				if (!next_scheduled_attribute)
				{
					_last_error = ov::String::FormatString("Failed to find scheduled attribute, scheduled is required");
					return false;
				}

				next_scheduled = next_scheduled_attribute.as_string();
			}
			else // last program
			{
				last = true;
			}

			auto repeat_attribute = program_node.attribute("repeat");
			if (repeat_attribute)
			{
				repeat = repeat_attribute.as_bool();
			}

			auto program = MakeProgram(name, scheduled, next_scheduled, repeat, last);
			if (program == nullptr)
			{
				return false;
			}

			if (ReadItemNodes(program_node, program->_items) == false)
			{
				return false;
			}

			_programs.push_back(program);
		}

		return true;
	}

	bool Schedule::ReadItemNodes(const pugi::xml_node &item_parent_node, std::vector<std::shared_ptr<Schedule::Item>> &items)
	{
		for (auto item_node = item_parent_node.child("Item"); item_node; item_node = item_node.next_sibling("Item"))
		{
			ov::String url;
			int64_t start_time_ms_conf = 0;
			int64_t duration_ms_conf = -1;
			bool fallback_on_err = true;

			auto url_attribute = item_node.attribute("url");
			if (!url_attribute)
			{
				_last_error = "Failed to find url attribute, url is required";
				return false;
			}

			url = url_attribute.as_string();

			auto start_attribute = item_node.attribute("start");
			if (start_attribute)
			{
				start_time_ms_conf = start_attribute.as_llong();
			}

			auto duration_attribute = item_node.attribute("duration");
			if (duration_attribute)
			{
				duration_ms_conf = duration_attribute.as_llong();
			}

			auto fallback_on_err_attribute = item_node.attribute("fallbackOnErr");
			if (fallback_on_err_attribute)
			{
				fallback_on_err = fallback_on_err_attribute.as_bool();
			}

			auto item = MakeItem(url, start_time_ms_conf, duration_ms_conf, fallback_on_err);
			if (item == nullptr)
			{
				return false;
			}

			items.push_back(item);
		}

		return true;
	}

	Schedule::Stream Schedule::MakeStream(const ov::String &name, bool bypass_transcoder, bool video_track, bool audio_track) const
	{
		Schedule::Stream stream;
		
		stream._name = name;
		stream._bypass_transcoder = bypass_transcoder;
		stream._video_track = video_track;
		stream._audio_track = audio_track;

		return stream;
	}

	std::shared_ptr<Schedule::Program> Schedule::MakeFallbackProgram() const
	{
		auto program = std::make_shared<Schedule::Program>();
		program->_name = "fallback";
		program->_scheduled = "1970-01-01T00:00:00Z";
		program->_scheduled_time = std::chrono::system_clock::time_point::min();
		program->_duration_ms = -1;
		program->_end_time = std::chrono::system_clock::time_point::max();
		program->_repeat = true;

		return program;
	}

	std::shared_ptr<Schedule::Program> Schedule::MakeProgram(const ov::String &name, const ov::String &scheduled_time, const ov::String &next_scheduled_time, bool repeat, bool last) const
	{
		auto program = std::make_shared<Schedule::Program>();

		program->_name = name;
		program->_scheduled = scheduled_time;
		program->_repeat = repeat;

		try 
		{
			program->_scheduled_time = ov::Converter::FromISO8601(program->_scheduled);
		}
		catch (std::exception &e)
		{
			_last_error = ov::String::FormatString("Failed to parse scheduled attribute: %s", e.what());
			return nullptr;
		}

		if (last == false)
		{
			try
			{
				program->_end_time = ov::Converter::FromISO8601(next_scheduled_time);
			}
			catch (std::exception &e)
			{
				_last_error = ov::String::FormatString("Failed to parse scheduled attribute: %s", e.what());
				return nullptr;
			}

			program->_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(program->_end_time - program->_scheduled_time).count();
		}
		else
		{
			program->_duration_ms = -1; // -1 means unknown duration
			program->_end_time = std::chrono::system_clock::time_point::max();
		}

		return program;
	}

	std::shared_ptr<Schedule::Item> Schedule::MakeItem(const ov::String &url, int64_t start_time_ms_conf, int64_t duration_ms_conf, bool fallback_on_err) const
	{
		auto item = std::make_shared<Schedule::Item>();

		item->_url = url;
		item->_start_time_ms_conf = start_time_ms_conf;
		item->_start_time_ms = start_time_ms_conf; // start_time_ms can be changed later
		item->_duration_ms_conf = duration_ms_conf;
		item->_duration_ms = duration_ms_conf; // duration_ms can be changed later
		item->_fallback_on_err = fallback_on_err;

		// file_path
		if (url.LowerCaseString().HasPrefix("file://"))
		{
			item->_file_path = url.Substring(7);
			item->_file_path = _media_root_dir + item->_file_path;
			item->_file = true;
		}
		else if (url.LowerCaseString().HasPrefix("stream://"))
		{
			item->_file_path = url.Substring(9);
			item->_file = false;
		}
		else
		{
			_last_error = "Failed to parse url attribute, url must be file:// or stream://";
			return nullptr;
		}

		// minimum duration is 1000ms
		if (item->_duration_ms_conf >= 0 && item->_duration_ms_conf <= 1000)
		{
			logtw("Item duration is too short, duration must be greater than 1000ms. url: %s, duration: %" PRId64 ", it will be changed to 1000ms", item->_url.CStr(), item->_duration_ms_conf);
			item->_duration_ms_conf = 1000;
			item->_duration_ms = 1000;
		}

		// Preload file information if it is a file
		if (item->_file == true)
		{
			// Pre load file information
			if (item->LoadContext() == nullptr)
			{
				logte("Failed to preload file information. url: %s", item->_file_path.CStr());
			}
		}

		return item;
	}

	const Schedule::Stream &Schedule::GetStream() const
	{
		return _stream;
	}

	const std::shared_ptr<Schedule::Program> Schedule::GetFallbackProgram() const
	{
		return _fallback_program;
	}

	const std::vector<std::shared_ptr<Schedule::Program>> &Schedule::GetPrograms() const
	{
		return _programs;
	}

	const std::shared_ptr<Schedule::Program> Schedule::GetCurrentProgram() const
	{
		// Get current time
		auto now = std::chrono::system_clock::now();

		// Find program which between scheduled time and finished time
		for (auto program : _programs)
		{
			if (program->_scheduled_time <= now && now < program->_end_time)
			{
				if (program->IsOffAir() == true)
				{
					return nullptr;
				}

				return program;
			}

			// scheduled time is future, there is no current program yet
			if (program->_scheduled_time > now)
			{
				break;
			}
		}

		return nullptr;
	}

	const std::shared_ptr<Schedule::Program> Schedule::GetNextProgram() const
	{
		// Get current time
		auto now = std::chrono::system_clock::now();

		// Find program which between scheduled time and finished time
		for (auto program : _programs)
		{
			if (program->_scheduled_time > now)
			{
				return program;
			}
		}

		return nullptr;
	}

	ov::String Schedule::GetLastError() const
	{
		return _last_error;
	}

	std::chrono::system_clock::time_point Schedule::GetCreatedTime() const
	{
		return _created_time;
	}

	CommonErrorCode Schedule::SaveToXMLFile(const ov::String &file_path) const
	{
		pugi::xml_document xml_doc;
		auto schedule_node = xml_doc.append_child("Schedule");

		// Stream
		auto stream_node = schedule_node.append_child("Stream");
		stream_node.append_child("Name").text().set(_stream._name.CStr());
		stream_node.append_child("BypassTranscoder").text().set(_stream._bypass_transcoder);
		stream_node.append_child("VideoTrack").text().set(_stream._video_track);
		stream_node.append_child("AudioTrack").text().set(_stream._audio_track);
		
		// AudioMap
		stream_node.append_child("AudioMap");
		for (const auto &audio_map_item : _stream._audio_map)
		{
			auto audio_map_item_node = stream_node.child("AudioMap").append_child("Item");
			audio_map_item_node.append_child("Name").text().set(audio_map_item.GetName().CStr());
			audio_map_item_node.append_child("Language").text().set(audio_map_item.GetLanguage().CStr());
			audio_map_item_node.append_child("Characteristics").text().set(audio_map_item.GetCharacteristics().CStr());
		}

		// ErrorToleranceDurationMs
		stream_node.append_child("ErrorToleranceDurationMs").text().set(_stream._error_tolerance_duration_ms);

		// FallbackProgram
		if (_fallback_program != nullptr)
		{
			auto fallback_program_node = schedule_node.append_child("FallbackProgram");

			if (WriteItemNodes(_fallback_program->_items, fallback_program_node) == false)
			{
				return CommonErrorCode::ERROR;
			}
		}

		// Programs
		for (auto program : _programs)
		{
			auto program_node = schedule_node.append_child("Program");
			program_node.append_attribute("name").set_value(program->_name.CStr());
			program_node.append_attribute("scheduled").set_value(program->_scheduled.CStr());
			program_node.append_attribute("repeat").set_value(program->_repeat);

			if (WriteItemNodes(program->_items, program_node) == false)
			{
				return CommonErrorCode::ERROR;
			}
		}

		// Save
		auto save_result = xml_doc.save_file(file_path.CStr());
		if (!save_result)
		{
			return CommonErrorCode::ERROR;
		}

		return CommonErrorCode::SUCCESS;
	}

	bool Schedule::WriteItemNodes(const std::vector<std::shared_ptr<Schedule::Item>> &items, pugi::xml_node &item_parent_node) const
	{
		for (const auto &item : items)
		{
			auto item_node = item_parent_node.append_child("Item");
			item_node.append_attribute("url").set_value(item->_url.CStr());
			item_node.append_attribute("start").set_value(item->_start_time_ms_conf);
			item_node.append_attribute("duration").set_value(item->_duration_ms_conf);
		}

		return true;
	}

	CommonErrorCode Schedule::ToJsonObject(Json::Value &root_object) const
	{
		Json::Value stream_object;
		stream_object["name"] = _stream._name.CStr();
		stream_object["bypassTranscoder"] = _stream._bypass_transcoder;
		stream_object["videoTrack"] = _stream._video_track;
		stream_object["audioTrack"] = _stream._audio_track;
		// audio map
		Json::Value audio_map_object;
		for (const auto &audio_map_item : _stream._audio_map)
		{
			Json::Value audio_map_item_object;
			audio_map_item_object["name"] = audio_map_item.GetName().CStr();
			audio_map_item_object["language"] = audio_map_item.GetLanguage().CStr();
			audio_map_item_object["characteristics"] = audio_map_item.GetCharacteristics().CStr();
			audio_map_object.append(audio_map_item_object);
		}
		stream_object["audioMap"] = audio_map_object;

		// error_tolerance_duration_ms
		stream_object["errorToleranceDurationMs"] = _stream._error_tolerance_duration_ms;

		root_object["stream"] = stream_object;

		if (_fallback_program != nullptr)
		{
			Json::Value fallback_program_object;
			fallback_program_object["name"] = _fallback_program->_name.CStr();
			fallback_program_object["scheduled"] = _fallback_program->_scheduled.CStr();
			fallback_program_object["repeat"] = _fallback_program->_repeat;

			// items
			Json::Value items_object;
			if (WriteItemObjects(_fallback_program->_items, items_object) == false)
			{
				return CommonErrorCode::ERROR;
			}

			fallback_program_object["items"] = items_object;
			
			root_object["fallbackProgram"] = fallback_program_object;
		}

		for (auto program : _programs)
		{
			Json::Value program_object;
			program_object["name"] = program->_name.CStr();
			program_object["scheduled"] = program->_scheduled.CStr();
			program_object["repeat"] = program->_repeat;

			Json::Value items_object;
			if (WriteItemObjects(program->_items, items_object) == false)
			{
				return CommonErrorCode::ERROR;
			}

			program_object["items"] = items_object;

			root_object["programs"].append(program_object);
		}

		return CommonErrorCode::SUCCESS;
	}

	bool Schedule::WriteItemObjects(const std::vector<std::shared_ptr<Schedule::Item>> &items, Json::Value &item_parent_object) const
	{
		for (const auto &item : items)
		{
			Json::Value item_object;

			item_object["url"] = item->_url.CStr();
			item_object["start"] = item->_start_time_ms_conf;
			item_object["duration"] = item->_duration_ms_conf;

			item_parent_object.append(item_object);
		}

		return true;
	}
}