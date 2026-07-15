//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#pragma once

#include "base/common_types.h"

namespace info
{
	class TrackSetEntry
	{
	public:
		TrackSetEntry(const ov::String &variant_name, int index_hint)
			: _variant_name(variant_name), _index_hint(index_hint)
		{
		}

		ov::String GetVariantName() const
		{
			return _variant_name;
		}

		int GetIndexHint() const
		{
			return _index_hint;
		}

		void SetIndexHint(int index)
		{
			_index_hint = index;
		}

		bool operator==(const TrackSetEntry &rhs) const
		{
			return _variant_name == rhs._variant_name && _index_hint == rhs._index_hint;
		}

		bool operator!=(const TrackSetEntry &rhs) const
		{
			return !(*this == rhs);
		}

	private:
		ov::String _variant_name;
		int _index_hint = -1;
	};

	class TrackSet
	{
	public:
		TrackSet(const ov::String &name, bool strict = false)
			: _name(name), _strict(strict)
		{
		}
		~TrackSet() = default;

		TrackSet(const TrackSet &other)
		{
			_name	= other._name;
			_strict = other._strict;

			for (auto &entry : other._video_entries)
			{
				_video_entries.push_back(std::make_shared<TrackSetEntry>(*entry));
			}

			for (auto &entry : other._audio_entries)
			{
				_audio_entries.push_back(std::make_shared<TrackSetEntry>(*entry));
			}
		}

		ov::String GetName() const
		{
			return _name;
		}

		bool IsStrict() const
		{
			return _strict;
		}

		void AddVideoEntry(const std::shared_ptr<TrackSetEntry> &entry)
		{
			_video_entries.push_back(entry);
		}

		void AddAudioEntry(const std::shared_ptr<TrackSetEntry> &entry)
		{
			_audio_entries.push_back(entry);
		}

		const std::vector<std::shared_ptr<TrackSetEntry>> &GetVideoEntries() const
		{
			return _video_entries;
		}

		const std::vector<std::shared_ptr<TrackSetEntry>> &GetAudioEntries() const
		{
			return _audio_entries;
		}

		ov::String ToString() const
		{
			ov::String out_str = ov::String::FormatString("TrackSet(%s)\n", _name.CStr());

			for (auto &entry : _video_entries)
			{
				out_str.AppendFormat("\tVideo Variant : %s (IndexHint : %d)\n", entry->GetVariantName().CStr(), entry->GetIndexHint());
			}

			for (auto &entry : _audio_entries)
			{
				out_str.AppendFormat("\tAudio Variant : %s (IndexHint : %d)\n", entry->GetVariantName().CStr(), entry->GetIndexHint());
			}

			return out_str;
		}

		bool operator==(const TrackSet &rhs) const
		{
			if (_name != rhs._name)
			{
				return false;
			}

			if (_video_entries.size() != rhs._video_entries.size())
			{
				return false;
			}

			for (size_t i = 0; i < _video_entries.size(); i++)
			{
				if (*_video_entries[i] != *rhs._video_entries[i])
				{
					return false;
				}
			}

			if (_audio_entries.size() != rhs._audio_entries.size())
			{
				return false;
			}

			for (size_t i = 0; i < _audio_entries.size(); i++)
			{
				if (*_audio_entries[i] != *rhs._audio_entries[i])
				{
					return false;
				}
			}

			return true;
		}

		bool operator!=(const TrackSet &rhs) const
		{
			return !(*this == rhs);
		}

	private:
		ov::String _name;
		bool _strict = false;
		std::vector<std::shared_ptr<TrackSetEntry>> _video_entries;
		std::vector<std::shared_ptr<TrackSetEntry>> _audio_entries;
	};
}  // namespace info
