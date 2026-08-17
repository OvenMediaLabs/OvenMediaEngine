//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "base/common_types.h"

namespace info
{
	class Dump
	{
	public:
		void SetId(const ov::String &id)
		{
			_id = id;
		}

		const ov::String &GetId() const
		{
			return _id;
		}

		void SetStreamName(const ov::String &stream_name)
		{
			_stream_name = stream_name;
		}

		const ov::String &GetStreamName() const
		{
			return _stream_name;
		}

		void SetUserData(const ov::String &user_data)
		{
			_user_data = user_data;
		}

		const ov::String &GetUserData() const
		{
			return _user_data;
		}

		void SetEnabled(bool enabled)
		{
			_enabled = enabled;
			if (enabled == true)
			{
				_enabled_time = std::chrono::system_clock::now();
			}
		}

		bool IsEnabled() const
		{
			return _enabled;
		}

		// Get enabled time
		std::chrono::system_clock::time_point GetEnabledTime() const
		{
			return _enabled_time;
		}

		void SetOutputPath(const ov::String &output_path)
		{
			_output_path = output_path;
		}

		const ov::String &GetOutputPath() const
		{
			return _output_path;
		}

		// Info file
		void SetInfoFile(const ov::String &info_file_url)
		{
			_info_file_url = info_file_url;

			// Split file name and path
			_info_file_name = info_file_url.Split("/").back();
			_info_file_path = info_file_url.Substring(0, info_file_url.GetLength() - _info_file_name.GetLength());
		}

		const ov::String &GetInfoFileUrl() const
		{
			return _info_file_url;
		}

		const ov::String &GetInfoFileName() const
		{
			return _info_file_name;
		}

		const ov::String &GetInfoFilePath() const
		{
			return _info_file_path;
		}

		// Playlists
		void AddPlaylist(const ov::String &playlist)
		{
			_playlists.push_back(playlist);
		}

		void SetPlaylists(const std::vector<ov::String> &playlists)
		{
			_playlists = playlists;
		}

		const std::vector<ov::String> &GetPlaylists() const
		{
			return _playlists;
		}

		
	protected:
		ov::String _id;
		ov::String _stream_name;

		ov::String _user_data;

		bool _enabled = false;
		std::chrono::system_clock::time_point _enabled_time;
		ov::String _output_path;

		ov::String _info_file_url;
		ov::String _info_file_path;
		ov::String _info_file_name;

		std::vector<ov::String> _playlists;
	};
}