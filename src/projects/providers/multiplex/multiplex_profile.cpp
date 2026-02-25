//==============================================================================
//
//  Multiplex
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================

#include "multiplex_profile.h"
#include "multiplex_private.h"
#include <base/ovlibrary/files.h>

namespace pvd
{
	std::tuple<std::shared_ptr<MultiplexProfile>, ov::String> MultiplexProfile::CreateFromXMLFile(const ov::String &file_path)
	{
		auto multiplex = std::make_shared<MultiplexProfile>();
		if (multiplex->LoadFromXMLFile(file_path) == false)
		{
			return {nullptr, multiplex->GetLastError()};
		}

		return {multiplex, "success"};
	}

	std::tuple<std::shared_ptr<MultiplexProfile>, ov::String> MultiplexProfile::CreateFromJsonObject(const Json::Value &object)
	{
		auto multiplex = std::make_shared<MultiplexProfile>();
		if (multiplex->LoadFromJsonObject(object) == false)
		{
			return {nullptr, multiplex->GetLastError()};
		}

		return {multiplex, "success"};
	}

	ov::String MultiplexProfile::GetFilePath() const
	{
		return _file_path;
	}

	std::shared_ptr<MultiplexProfile> MultiplexProfile::Clone() const
	{
		auto multiplex = std::make_shared<MultiplexProfile>();

		Json::Value root_object;
		if (ToJsonObject(root_object) != CommonErrorCode::SUCCESS)
		{
			return nullptr;
		}

		multiplex->LoadFromJsonObject(root_object);

		return multiplex;
	}

	bool MultiplexProfile::operator==(const MultiplexProfile &other) const
	{
		if (_output_stream_name != other._output_stream_name)
		{
			return false;
		}

		if (_bypass_transcoder != other._bypass_transcoder)
		{
			return false;
		}

		if (_source_streams.size() != other._source_streams.size())
		{
			return false;
		}

		if (_playlists.size() != other._playlists.size())
		{
			return false;
		}

		for (size_t i = 0; i < _playlists.size(); i++)
		{
			if (*_playlists[i] != *other._playlists[i])
			{
				return false;
			}
		}

		for (size_t i = 0; i < _source_streams.size(); i++)
		{
			if (*_source_streams[i] != *other._source_streams[i])
			{
				return false;
			}
		}

		return true;
	}

	bool MultiplexProfile::LoadFromJsonObject(const Json::Value &object)
	{
		if (ReadOutputStreamObject(object) == false)
		{
			return false;
		}

		if (ReadSourceStreamsObject(object) == false)
		{
			return false;
		}

		if (ReadPlaylistsObject(object) == false)
		{
			return false;
		}

		return true;
	}

	bool MultiplexProfile::ReadOutputStreamObject(const Json::Value &object)
	{
		auto output_stream_object = object["outputStream"];
		if (output_stream_object.isNull())
		{
			_last_error = "Failed to find outputStream object";
			return false;
		}

		auto name_object = output_stream_object["name"];
		if (name_object.isNull())
		{
			_last_error = "Failed to find outputStream/name object";
			return false;
		}

		_output_stream_name = name_object.asString().c_str();

		auto bypass_transcoder_object = output_stream_object["bypassTranscoder"];
		if (bypass_transcoder_object.isNull() == false)
		{
			_bypass_transcoder = bypass_transcoder_object.asBool();		
		}

		return true;
	}

	bool MultiplexProfile::ReadSourceStreamsObject(const Json::Value &object)
	{
		auto source_streams_object = object["sourceStreams"];
		if (source_streams_object.isNull())
		{
			_last_error = "Failed to find sourceStreams object";
			return false;
		}

		for (const auto &source_stream_object : source_streams_object)
		{
			auto name_object = source_stream_object["name"];
			if (name_object.isNull())
			{
				_last_error = "Failed to find sourceStreams/name object";
				return false;
			}

			auto url_object = source_stream_object["url"];
			if (url_object.isNull())
			{
				_last_error = "Failed to find sourceStreams/url object";
				return false;
			}

			auto source_stream = std::make_shared<SourceStream>();
			source_stream->SetName(name_object.asString().c_str());
			ov::String url_str = url_object.asString().c_str();
			if (source_stream->SetUrl(url_str) == false)
			{
				_last_error = ov::String::FormatString("Failed to parse url: %s", url_str.CStr());
				return false;
			}

			// TrackMap
			auto track_map_object = source_stream_object["trackMap"];
			if (track_map_object.isNull())
			{
				_last_error = "Failed to find sourceStreams/trackMap object";
				return false;
			}

			for (const auto &track_object : track_map_object)
			{
				auto source_track_name_object = track_object["sourceTrackName"];
				if (source_track_name_object.isNull())
				{
					_last_error = "Failed to find sourceStreams/trackMap/sourceTrackName object";
					return false;
				}

				auto new_track_name_object = track_object["newTrackName"];
				if (new_track_name_object.isNull())
				{
					_last_error = "Failed to find sourceStreams/trackMap/newTrackName object";
					return false;
				}

				auto bitrate_conf_object = track_object["bitrateConf"];
				int32_t bitrate_conf = 0;

				if (!bitrate_conf_object.isNull())
				{
					bitrate_conf = bitrate_conf_object.asInt();
				}

				auto framerate_conf_object = track_object["framerateConf"];
				int32_t framerate_conf = 0;

				if (!framerate_conf_object.isNull())
				{
					framerate_conf = framerate_conf_object.asInt();
				}

				ov::String source_track_name = source_track_name_object.asString().c_str();
				ov::String new_track_name = new_track_name_object.asString().c_str();
				source_stream->AddTrackMap(source_track_name, NewTrackInfo(source_track_name, new_track_name, bitrate_conf, framerate_conf));
				_new_track_names.emplace(new_track_name, true);
			}

			_source_streams.push_back(source_stream);
		}

		return true;
	}

	bool MultiplexProfile::ReadPlaylistsObject(const Json::Value &object)
	{
		auto playlists_object = object["playlists"];
		if (playlists_object.isNull())
		{
			// playlists is optional
			return true;
		}

		for (const auto &playlist_object : playlists_object)
		{
			auto name_object = playlist_object["name"];
			if (name_object.isNull())
			{
				_last_error = "Failed to find playlists/name object";
				return false;
			}

			auto file_name_object = playlist_object["fileName"];
			if (file_name_object.isNull())
			{
				_last_error = "Failed to find playlists/fileName object";
				return false;
			}

			auto playlist = std::make_shared<info::Playlist>(name_object.asString().c_str(), file_name_object.asString().c_str(), false);

			// Options
			auto options_object = playlist_object["options"];
			if (!options_object.isNull())
			{
				auto webrtc_auto_abr_object = options_object["webrtcAutoAbr"];
				if (!webrtc_auto_abr_object.isNull())
				{
					playlist->SetWebRtcAutoAbr(webrtc_auto_abr_object.asBool());
				}

				auto hls_chunklist_path_depth_object = options_object["hlsChunklistPathDepth"];
				if (!hls_chunklist_path_depth_object.isNull())
				{
					playlist->SetHlsChunklistPathDepth(hls_chunklist_path_depth_object.asInt());
				}

				auto enable_ts_packaging_object = options_object["enableTsPackaging"];
				if (!enable_ts_packaging_object.isNull())
				{
					playlist->EnableTsPackaging(enable_ts_packaging_object.asBool());
				}
			}

			// Rendition
			auto renditions_object = playlist_object["renditions"];
			if (renditions_object.isNull())
			{
				// renditions is optional
				continue;
			}

			for (const auto &rendition_object : renditions_object)
			{
				auto rendition_name_object = rendition_object["name"];
				if (rendition_name_object.isNull())
				{
					_last_error = "Failed to find playlists/renditions/name object";
					return false;
				}

				auto video_variant_name_object = rendition_object["video"];
				ov::String video_variant_name;
				if (video_variant_name_object.isNull() == false)
				{
					video_variant_name = video_variant_name_object.asString().c_str();

					// Check if video variant name is exist in SourceStream/TrackMap/NewTrackName
					if (_new_track_names.find(video_variant_name) == _new_track_names.end())
					{
						_last_error = ov::String::FormatString("Failed to find video variant name %s in SourceStream/TrackMap object", video_variant_name.CStr());
						return false;
					}
				}

				auto audio_variant_name_object = rendition_object["audio"];
				ov::String audio_variant_name;
				if (audio_variant_name_object.isNull() == false)
				{
					audio_variant_name = audio_variant_name_object.asString().c_str();

					// Check if audio variant name is exist in SourceStream/TrackMap/NewTrackName
					if (_new_track_names.find(audio_variant_name) == _new_track_names.end())
					{
						_last_error = ov::String::FormatString("Failed to find audio variant name %s in SourceStream/TrackMap object", audio_variant_name.CStr());
						return false;
					}
				}

				auto rendition = std::make_shared<info::Rendition>(rendition_name_object.asString().c_str(), video_variant_name, audio_variant_name);

				// VideoIndexHint
				auto video_index_hint_object = rendition_object["videoIndexHint"];
				if (!video_index_hint_object.isNull())
				{
					rendition->SetVideoIndexHint(video_index_hint_object.asInt());
				}

				// AudioIndexHint
				auto audio_index_hint_object = rendition_object["audioIndexHint"];
				if (!audio_index_hint_object.isNull())
				{
					rendition->SetAudioIndexHint(audio_index_hint_object.asInt());
				}

				playlist->AddRendition(rendition);
			}

			_playlists.push_back(playlist);
		}

		return true;
	}

	bool MultiplexProfile::LoadFromXMLFile(const ov::String &file_path)
	{
		_file_path = file_path;

		pugi::xml_document xml_doc;
		auto load_result = xml_doc.load_file(file_path.CStr());
		if (!load_result)
		{
			_last_error = ov::String::FormatString("Failed to load schedule file: %s", load_result.description());
			return false;
		}

		auto root_node = xml_doc.child("Multiplex");
		if (!root_node)
		{
			_last_error = "Failed to find root node";
			return false;
		}

		if (ReadOutputStreamNode(root_node) == false)
		{
			return false;
		}

		if (ReadSourceStreamsNode(root_node) == false)
		{
			return false;
		}

		if (ReadPlaylistsNode(root_node) == false)
		{
			return false;
		}

		return true;
	}

	bool MultiplexProfile::ReadOutputStreamNode(const pugi::xml_node &root_node)
	{
		auto output_stream_node = root_node.child("OutputStream");
		if (!output_stream_node)
		{
			_last_error = "Failed to find OutputStream node";
			return false;
		}

		auto name = output_stream_node.child("Name");
		if (!name)
		{
			_last_error = "Failed to find Name node";
			return false;
		}

		_output_stream_name = name.text().as_string();

		auto bypass_transcoder = output_stream_node.child("BypassTranscoder");
		if (bypass_transcoder)
		{
			_bypass_transcoder = bypass_transcoder.text().as_bool();
		}

		return true;
	}

	bool MultiplexProfile::ReadPlaylistsNode(const pugi::xml_node &root_node)
	{
		auto playlists_node = root_node.child("Playlists");
		if (!playlists_node)
		{
			// playlists is optional
			return true;
		}

		for (auto playlist_node = playlists_node.child("Playlist"); playlist_node; playlist_node = playlist_node.next_sibling("Playlist"))
		{
			auto playlist_name_node = playlist_node.child("Name");
			if (!playlist_name_node)
			{
				_last_error = "Failed to find Playlists/Playlist/Name node";
				return false;
			}

			auto file_name_node = playlist_node.child("FileName");
			if (!file_name_node)
			{
				_last_error = "Failed to find Playlists/Playlist/FileName node";
				return false;
			}

			auto playlist = std::make_shared<info::Playlist>(playlist_name_node.text().as_string(), file_name_node.text().as_string(), false);

			// Options
			auto options_node = playlist_node.child("Options");
			if (options_node)
			{
				auto webrtc_auto_abr = options_node.child("WebRtcAutoAbr");
				if (webrtc_auto_abr)
				{
					playlist->SetWebRtcAutoAbr(webrtc_auto_abr.text().as_bool());
				}

				auto hls_chunklist_path_depth = options_node.child("HLSChunklistPathDepth");
				if (!hls_chunklist_path_depth)
				{
					hls_chunklist_path_depth = options_node.child("HlsChunklistPathDepth");
				}

				if (hls_chunklist_path_depth)
    			{
        			playlist->SetHlsChunklistPathDepth(hls_chunklist_path_depth.text().as_int());
    			}

				auto enable_ts_packaging = options_node.child("EnableTsPackaging");
				if (enable_ts_packaging)
				{
					playlist->EnableTsPackaging(enable_ts_packaging.text().as_bool());
				}
			}

			// Rendition
			for (auto rendition_node = playlist_node.child("Rendition"); rendition_node; rendition_node = rendition_node.next_sibling("Rendition"))
			{
				ov::String rendition_name;
				ov::String video_variant_name;
				ov::String audio_variant_name;

				auto rendition_name_node = rendition_node.child("Name");
				if (!rendition_name_node)
				{
					_last_error = "Failed to find Playlists/Playlist/Rendition/Name node";
					return false;
				}

				rendition_name = rendition_name_node.text().as_string();

				auto video_variant_name_node = rendition_node.child("Video");
				if (video_variant_name_node)
				{
					video_variant_name = video_variant_name_node.text().as_string();

					// Check if video variant name is exist in SourceStream/TrackMap/NewTrackName
					if (_new_track_names.find(video_variant_name) == _new_track_names.end())
					{
						_last_error = ov::String::FormatString("Failed to find video variant name %s in SourceStream/TrackMap/NewTrackName", video_variant_name.CStr());
						return false;
					}
				}

				auto audio_variant_name_node = rendition_node.child("Audio");
				if (audio_variant_name_node)
				{
					audio_variant_name = audio_variant_name_node.text().as_string();

					// Check if audio variant name is exist in SourceStream/TrackMap/NewTrackName
					if (_new_track_names.find(audio_variant_name) == _new_track_names.end())
					{
						_last_error = ov::String::FormatString("Failed to find audio variant name %s in SourceStream/TrackMap/NewTrackName", audio_variant_name.CStr());
						return false;
					}
				}

				auto rendition = std::make_shared<info::Rendition>(rendition_name, video_variant_name, audio_variant_name);
				
				// VideoIndexHint
				auto video_index_hint_node = rendition_node.child("VideoIndexHint");
				if (video_index_hint_node)
				{
					rendition->SetVideoIndexHint(video_index_hint_node.text().as_int());
				}

				// AudioIndexHint
				auto audio_index_hint_node = rendition_node.child("AudioIndexHint");
				if (audio_index_hint_node)
				{
					rendition->SetAudioIndexHint(audio_index_hint_node.text().as_int());
				}

				playlist->AddRendition(rendition);
			}

			_playlists.push_back(playlist);
		}

		return true;
	}

	bool MultiplexProfile::ReadSourceStreamsNode(const pugi::xml_node &root_node)
	{
		auto source_streams_node = root_node.child("SourceStreams");

		for (auto source_stream_node = source_streams_node.child("SourceStream"); source_stream_node; source_stream_node = source_stream_node.next_sibling("SourceStream"))
		{
			auto name_node = source_stream_node.child("Name");
			if (!name_node)
			{
				_last_error = "Failed to find SourceStreams/SourceStream/Name node";
				return false;
			}

			auto url_node = source_stream_node.child("Url");
			if (!url_node)
			{
				_last_error = "Failed to find SourceStreams/SourceStream/Url node";
				return false;
			}

			auto source_stream = std::make_shared<SourceStream>();
			source_stream->SetName(name_node.text().as_string());
			ov::String url_str = url_node.text().as_string();
			if (source_stream->SetUrl(url_str) == false)
			{
				_last_error = ov::String::FormatString("Failed to parse url: %s", url_str.CStr());
				return false;
			}

			// TrackMap
			auto track_map_node = source_stream_node.child("TrackMap");

			// Track
			for (auto track_node = track_map_node.child("Track"); track_node; track_node = track_node.next_sibling("Track"))
			{
				auto source_track_name_node = track_node.child("SourceTrackName");
				if (!source_track_name_node)
				{
					_last_error = "Failed to find SourceStreams/SourceStream/TrackMap/SourceTrackName node";
					return false;
				}

				auto new_track_name_node = track_node.child("NewTrackName");
				if (!new_track_name_node)
				{
					_last_error = "Failed to find SourceStreams/SourceStream/TrackMap/NewTrackName node";
					return false;
				}

				auto bitrate_conf_node = track_node.child("BitrateConf");
				int32_t bitrate_conf = 0;

				if (bitrate_conf_node)
				{
					bitrate_conf = bitrate_conf_node.text().as_int();
				}

				auto framerate_conf_node = track_node.child("FramerateConf");
				int32_t framerate_conf = 0;

				if (framerate_conf_node)
				{
					framerate_conf = framerate_conf_node.text().as_int();
				}

				ov::String source_track_name = source_track_name_node.text().as_string();
				ov::String new_track_name = new_track_name_node.text().as_string();
				source_stream->AddTrackMap(source_track_name, NewTrackInfo(source_track_name, new_track_name, bitrate_conf, framerate_conf));
				_new_track_names.emplace(new_track_name, true);
			}

			_source_streams.push_back(source_stream);
		}

		return true;
	}

	ov::String MultiplexProfile::GetLastError() const
	{
		return _last_error;
	}

	CommonErrorCode MultiplexProfile::SaveToXMLFile(const ov::String &file_path) const
	{
		pugi::xml_document xml_doc;
		auto root_node = xml_doc.append_child("Multiplex");

		// OutputStream
		auto output_stream_node = root_node.append_child("OutputStream");
		output_stream_node.append_child("Name").text().set(_output_stream_name.CStr());
		output_stream_node.append_child("BypassTranscoder").text().set(_bypass_transcoder);

		// SourceStreams
		auto source_streams_node = root_node.append_child("SourceStreams");
		for (const auto &source_stream : _source_streams)
		{
			auto source_stream_node = source_streams_node.append_child("SourceStream");
			source_stream_node.append_child("Name").text().set(source_stream->GetName().CStr());
			source_stream_node.append_child("Url").text().set(source_stream->GetUrlStr().CStr());

			auto track_map_node = source_stream_node.append_child("TrackMap");
			for (const auto &[source_track_name, new_track_info] : source_stream->GetTrackMap())
			{
				auto track_node = track_map_node.append_child("Track");
				track_node.append_child("SourceTrackName").text().set(source_track_name.CStr());
				track_node.append_child("NewTrackName").text().set(new_track_info.new_track_name.CStr());

				if (new_track_info.bitrate_conf != 0)
				{
					track_node.append_child("BitrateConf").text().set(new_track_info.bitrate_conf);
				}

				if (new_track_info.framerate_conf != 0)
				{
					track_node.append_child("FramerateConf").text().set(new_track_info.framerate_conf);
				}
			}
		}

		// Playlists
		if (_playlists.size() > 0)
		{
			auto playlists_node = root_node.append_child("Playlists");
			for (const auto &playlist : _playlists)
			{
				auto playlist_node = playlists_node.append_child("Playlist");
				playlist_node.append_child("Name").text().set(playlist->GetName().CStr());
				playlist_node.append_child("FileName").text().set(playlist->GetFileName().CStr());

				// Options
				auto options_node = playlist_node.append_child("Options");
				if (playlist->IsWebRtcAutoAbr())
				{
					options_node.append_child("WebRtcAutoAbr").text().set(playlist->IsWebRtcAutoAbr());
				}

				if (playlist->GetHlsChunklistPathDepth() != -1)
				{
					options_node.append_child("HLSChunklistPathDepth").text().set(playlist->GetHlsChunklistPathDepth());
				}

				if (playlist->IsTsPackagingEnabled())
				{
					options_node.append_child("EnableTsPackaging").text().set(playlist->IsTsPackagingEnabled());
				}

				// Renditions
				for (const auto &rendition : playlist->GetRenditionList())
				{
					auto rendition_node = playlist_node.append_child("Rendition");
					rendition_node.append_child("Name").text().set(rendition->GetName().CStr());

					if (rendition->GetVideoVariantName().IsEmpty() == false)
					{
						rendition_node.append_child("Video").text().set(rendition->GetVideoVariantName().CStr());
					}

					if (rendition->GetAudioVariantName().IsEmpty() == false)
					{
						rendition_node.append_child("Audio").text().set(rendition->GetAudioVariantName().CStr());
					}
				}
			}
		}

		if (xml_doc.save_file(file_path.CStr()) == false)
		{
			return CommonErrorCode::ERROR;
		}

		return CommonErrorCode::SUCCESS;
	}

	CommonErrorCode MultiplexProfile::ToJsonObject(Json::Value &root_object) const
	{
		// OutputStream
		Json::Value output_stream_object;
		output_stream_object["name"] = _output_stream_name.CStr();
		output_stream_object["bypassTranscoder"] = _bypass_transcoder;
		root_object["outputStream"] = output_stream_object;

		// SourceStreams
		Json::Value source_streams_object;
		for (const auto &source_stream : _source_streams)
		{
			Json::Value source_stream_object;
			source_stream_object["name"] = source_stream->GetName().CStr();
			source_stream_object["url"] = source_stream->GetUrlStr().CStr();

			Json::Value track_map_object;
			for (const auto &[source_track_name, new_track_info] : source_stream->GetTrackMap())
			{
				Json::Value track_object;
				track_object["sourceTrackName"] = source_track_name.CStr();
				track_object["newTrackName"] = new_track_info.new_track_name.CStr();

				if (new_track_info.bitrate_conf != 0)
				{
					track_object["bitrateConf"] = new_track_info.bitrate_conf;
				}

				if (new_track_info.framerate_conf != 0)
				{
					track_object["framerateConf"] = new_track_info.framerate_conf;
				}

				track_map_object.append(track_object);
			}

			source_stream_object["trackMap"] = track_map_object;
			source_streams_object.append(source_stream_object);
		}

		root_object["sourceStreams"] = source_streams_object;

		// Playlists
		if (_playlists.size() > 0)
		{
			Json::Value playlists_object;
			for (const auto &playlist : _playlists)
			{
				Json::Value playlist_object;
				playlist_object["name"] = playlist->GetName().CStr();
				playlist_object["fileName"] = playlist->GetFileName().CStr();

				// Options
				Json::Value options_object;
				if (playlist->IsWebRtcAutoAbr())
				{
					options_object["webrtcAutoAbr"] = playlist->IsWebRtcAutoAbr();
				}

				if (playlist->GetHlsChunklistPathDepth() != -1)
				{
					options_object["hlsChunklistPathDepth"] = playlist->GetHlsChunklistPathDepth();
				}

				if (playlist->IsTsPackagingEnabled())
				{
					options_object["enableTsPackaging"] = playlist->IsTsPackagingEnabled();
				}

				playlist_object["options"] = options_object;

				// Rendition
				Json::Value renditions_object;
				for (const auto &rendition : playlist->GetRenditionList())
				{
					Json::Value rendition_object;
					rendition_object["name"] = rendition->GetName().CStr();

					if (rendition->GetVideoVariantName().IsEmpty() == false)
					{
						rendition_object["video"] = rendition->GetVideoVariantName().CStr();
					}

					if (rendition->GetAudioVariantName().IsEmpty() == false)
					{
						rendition_object["audio"] = rendition->GetAudioVariantName().CStr();
					}

					renditions_object.append(rendition_object);
				}

				playlist_object["renditions"] = renditions_object;
				playlists_object.append(playlist_object);
			}

			root_object["playlists"] = playlists_object;
		}

		return CommonErrorCode::SUCCESS;
	}

	std::chrono::system_clock::time_point MultiplexProfile::GetCreatedTime() const
	{
		return _created_time;
	}

	ov::String MultiplexProfile::GetOutputStreamName() const
	{
		return _output_stream_name;
	}

	bool MultiplexProfile::IsBypassTranscoder() const
	{
		return _bypass_transcoder;
	}

	const std::vector<std::shared_ptr<info::Playlist>> &MultiplexProfile::GetPlaylists() const
	{
		return _playlists;
	}

	const std::vector<std::shared_ptr<MultiplexProfile::SourceStream>> &MultiplexProfile::GetSourceStreams() const
	{
		return _source_streams;
	}

	ov::String MultiplexProfile::InfoStr() const
	{
		ov::String info_str;

		info_str += ov::String::FormatString("OutputStreamName : %s\n", _output_stream_name.CStr());
		info_str += ov::String::FormatString("SourceStreams : %zu\n", _source_streams.size());

		for (const auto &source : GetSourceStreams())
		{
			info_str += ov::String::FormatString("\tSourceStream : %s\n", source->GetName().CStr());
			info_str += ov::String::FormatString("\tUrl : %s\n", source->GetUrlStr().CStr());

			for (const auto &[source_track_name, new_track_info] : source->GetTrackMap())
			{
				info_str += ov::String::FormatString("\t\tTrackMap : %s -> %s\n", source_track_name.CStr(), new_track_info.new_track_name.CStr());
			}
		}

		return info_str;
	}
}
