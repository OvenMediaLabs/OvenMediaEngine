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

	class Rendition
	{
	public:
		Rendition(const ov::String &name, const ov::String &video_variant_name, const ov::String &audio_variant_name)
			: _name(name), _video_variant_name(video_variant_name), _audio_variant_name(audio_variant_name)
		{

		}

		// Getter
		ov::String GetName() const
		{
			return _name;
		}

		ov::String GetVideoVariantName() const
		{
			return _video_variant_name;
		}

		ov::String GetAudioVariantName() const
		{
			return _audio_variant_name;
		}

		void SetVideoIndexHint(int index)
		{
			_video_index_hint = index;
		}

		int GetVideoIndexHint() const
		{
			return _video_index_hint;
		}

		void SetAudioIndexHint(int index)
		{
			_audio_index_hint = index;
		}

		int GetAudioIndexHint() const
		{
			return _audio_index_hint;
		}

		// equals operator
		bool operator==(const Rendition &rhs) const
		{
			if (_name != rhs._name)
			{
				return false;
			}

			if (_video_variant_name != rhs._video_variant_name)
			{
				return false;
			}

			if (_audio_variant_name != rhs._audio_variant_name)
			{
				return false;
			}

			return true;
		}

		// not equals operator
		bool operator!=(const Rendition &rhs) const
		{
			return !(*this == rhs);
		}

	private:
		ov::String _name;
		ov::String _video_variant_name;
		int _video_index_hint = -1;
		ov::String _audio_variant_name;
		int _audio_index_hint = -1;
	};

	class Playlist
	{
	public:
		Playlist(const ov::String &name, const ov::String &file_name, bool is_default)
		{
			_name = name;
			_file_name = file_name;
			_is_default = is_default;
		}
		~Playlist() = default;

		// copy constructor
		Playlist(const Playlist &other)
		{
			_name = other._name;
			_file_name = other._file_name;
			_is_default = other._is_default;
			_webrtc_auto_abr = other._webrtc_auto_abr;
			_hls_chunklist_path_depth = other._hls_chunklist_path_depth;
			_enable_ts_packaging = other._enable_ts_packaging;

			for (auto &rendition : other._renditions)
			{
				_renditions.push_back(std::make_shared<Rendition>(*rendition));
			}
		}

		ov::String ToString() const
		{
			ov::String out_str = ov::String::FormatString("Playlist(%s) : %s\n", _name.CStr(), _file_name.CStr());
			out_str.AppendFormat("\tDefault : %s\n", _is_default ? "true" : "false");
			out_str.AppendFormat("\tWebRTC Auto ABR : %s\n", _webrtc_auto_abr ? "true" : "false");
			out_str.AppendFormat("\tHLS Chunklist Path Depth : %d\n", _hls_chunklist_path_depth);
			out_str.AppendFormat("\tTS Packaging : %s\n", _enable_ts_packaging ? "true" : "false");

			for (auto &rendition : _renditions)
			{
				out_str.AppendFormat("\t%s\n", rendition->GetName().CStr());
				out_str.AppendFormat("\t\tVideo Variant : %s\n", rendition->GetVideoVariantName().CStr());
				out_str.AppendFormat("\t\tVideo Index Hint : %d\n", rendition->GetVideoIndexHint());
				out_str.AppendFormat("\t\tAudio Variant : %s\n", rendition->GetAudioVariantName().CStr());
				out_str.AppendFormat("\t\tAudio Index Hint : %d\n", rendition->GetAudioIndexHint());
			}

			return out_str;
		}

		void SetWebRtcAutoAbr(bool enabled)
		{
			_webrtc_auto_abr = enabled;
		}

		bool IsWebRtcAutoAbr() const
		{
			return _webrtc_auto_abr;
		}

		void EnableTsPackaging(bool enabled)
		{
			_enable_ts_packaging = enabled;
		}

		bool IsTsPackagingEnabled() const
		{
			return _enable_ts_packaging;
		}

		void SetHlsChunklistPathDepth(int depth)
		{
			_hls_chunklist_path_depth = depth;
		}

		int GetHlsChunklistPathDepth() const
		{
			return _hls_chunklist_path_depth;
		}

		void EnableSubtitles(bool enabled)
		{
			_enable_subtitles = enabled;
		}

		bool IsSubtitlesEnabled() const
		{
			return _enable_subtitles;
		}

		// Append Rendition
		void AddRendition(const std::shared_ptr<Rendition> &rendition)
		{
			_renditions.push_back(rendition);
		}

		// Getter
		ov::String GetName() const
		{
			return _name;
		}

		ov::String GetFileName() const
		{
			return _file_name;
		}

		bool IsDefault() const
		{
			return _is_default;
		}
		
		// Get Rendition by name
		std::shared_ptr<Rendition> GetRendition(const ov::String &name) const
		{
			for (auto &rendition : _renditions)
			{
				if (rendition->GetName() == name)
				{
					return rendition;
				}
			}

			return nullptr;
		}

		// Get Rendition List
		const std::vector<std::shared_ptr<Rendition>> &GetRenditionList() const
		{
			return _renditions;
		}

		// equals operator
		bool operator==(const Playlist &rhs) const
		{
			if (_name != rhs._name)
			{
				return false;
			}

			if (_file_name != rhs._file_name)
			{
				return false;
			}

			if (_is_default != rhs._is_default)
			{
				return false;
			}

			if (_webrtc_auto_abr != rhs._webrtc_auto_abr)
			{
				return false;
			}

			if (_hls_chunklist_path_depth != rhs._hls_chunklist_path_depth)
			{
				return false;
			}

			if (_enable_ts_packaging != rhs._enable_ts_packaging)
			{
				return false;
			}

			if (_renditions.size() != rhs._renditions.size())
			{
				return false;
			}

			for (size_t i = 0; i < _renditions.size(); i++)
			{
				if (*_renditions[i] != *rhs._renditions[i])
				{
					return false;
				}
			}

			return true;
		}

		// not equals operator
		bool operator!=(const Playlist &rhs) const
		{
			return !(*this == rhs);
		}

	private:
		ov::String _name;
		ov::String _file_name;

		bool _is_default = false;

		bool _webrtc_auto_abr = false;
		int _hls_chunklist_path_depth = -1;
		bool _enable_ts_packaging = false;
		bool _enable_subtitles = true;

		std::vector<std::shared_ptr<Rendition>> _renditions;
	};
} // namespace info