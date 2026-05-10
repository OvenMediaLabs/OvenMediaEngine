//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

namespace info
{
	/// Tests whether a file name qualifies as a master playlist.
	///
	/// Recognises HLS `.m3u8` master and DASH `.mpd`; HLS sub-playlists
	/// (`chunklist*.m3u8` / `medialist*.m3u8`), segments, and thumbnails are
	/// excluded. Allow-list - extend only when adding a new master manifest format.
	///
	/// @param file_name File name to test (case-insensitive).
	///                  May include or omit an extension.
	///
	/// @return `true` if `file_name` qualifies as a master playlist, otherwise `false`.
	inline bool IsMasterPlaylistFileName(const ov::String &file_name)
	{
		auto lower_file_name = file_name.LowerCaseString();
		if (lower_file_name.IsEmpty())
		{
			return false;
		}

		if (lower_file_name.HasSuffix(".m3u8"))
		{
			// HLS sub-playlists (chunklist*.m3u8 / medialist*.m3u8) are not master playlists.
			return (lower_file_name.HasPrefix("medialist") == false) &&
				   (lower_file_name.HasPrefix("chunklist") == false);
		}

		if (lower_file_name.HasSuffix(".mpd"))
		{
			return true;
		}

		return false;
	}

	/// Extracts the master playlist file name from a URL.
	///
	/// For non-OVT schemes, the file segment is returned only if it qualifies
	/// as a master playlist (see `IsMasterPlaylistFileName`). OVT URLs are
	/// returned verbatim because the orchestrator already pre-scopes them with
	/// a master playlist file name; the file segment can be trusted as-is.
	///
	/// @param url URL to extract from.
	///
	/// @return Master playlist file name, or empty string if `url` does not
	///         point to one.
	inline ov::String GetMasterPlaylistFileName(const ov::Url &url)
	{
		auto file_name = url.File();
		if (file_name.IsEmpty())
		{
			return "";
		}

		if (url.Scheme().UpperCaseString() == "OVT")
		{
			return file_name;
		}

		return IsMasterPlaylistFileName(file_name) ? file_name : "";
	}

	/// `nullptr`-safe overload of `GetMasterPlaylistFileName`.
	///
	/// @param url URL to extract from. May be `nullptr`.
	///
	/// @return Master playlist file name, or empty string if `url` is `nullptr`
	///         or does not point to a master playlist.
	inline ov::String GetMasterPlaylistFileName(const std::shared_ptr<const ov::Url> &url)
	{
		return (url != nullptr)
				   ? GetMasterPlaylistFileName(*url)
				   : ov::String("");
	}

	/// Strips the trailing extension from a playlist file name.
	///
	/// `info::Playlist::_file_name` is stored without an extension (e.g. `master`),
	/// but URLs on the wire may carry either form (`master` or `master.m3u8`).
	/// Use this when turning a URL's `File()` into a playlist lookup key.
	/// Only the last dot is stripped, so `master.live.m3u8` -> `master.live`.
	///
	/// @param playlist_file_name File name to strip. Empty or dot-less names
	///                           pass through unchanged.
	///
	/// @return File name without its trailing extension.
	inline ov::String StripPlaylistFileExtension(const ov::String &playlist_file_name)
	{
		if (playlist_file_name.IsEmpty())
		{
			return playlist_file_name;
		}

		auto dot_pos = playlist_file_name.IndexOfRev('.');
		if (dot_pos <= 0)
		{
			return playlist_file_name;
		}

		return playlist_file_name.Substring(0, dot_pos);
	}

	/// Rewrites every OVT URL in `url_list` so its file segment matches the
	/// master playlist scope derived from `request_from`. Non-OVT URLs and
	/// URLs already carrying the same scope are left untouched. No-op if
	/// `request_from` does not resolve to a master playlist.
	///
	/// `SetFile()` *replaces* the file segment - blind append on an already-scoped
	/// URL (e.g. an origin map's `ovt://host/app/stream/a.m3u8`) would produce
	/// `/app/stream/a.m3u8/b.m3u8` and force compat fallback. Bare URLs (no file
	/// segment) are also handled cleanly.
	///
	/// @param request_from Trigger URL whose scope is propagated.
	/// @param url_list URL list to rewrite in place.
	inline void AppendPlaylistScopeToOvtUrls(const std::shared_ptr<const ov::Url> &request_from,
											 std::vector<ov::String> &url_list)
	{
		auto playlist_file_name = GetMasterPlaylistFileName(request_from);
		if (playlist_file_name.IsEmpty())
		{
			return;
		}

		// OVT URLs carry the playlist name without extension (OME stores
		// `info::Playlist::_file_name` as `master`, not `master.m3u8`).
		playlist_file_name = StripPlaylistFileExtension(playlist_file_name);

		for (auto &url : url_list)
		{
			auto parsed_url = ov::Url::Parse(url);
			if ((parsed_url == nullptr) || (parsed_url->Scheme().UpperCaseString() != "OVT"))
			{
				continue;
			}

			if (parsed_url->File() == playlist_file_name)
			{
				// Already correctly scoped.
				continue;
			}

			if (parsed_url->SetFile(playlist_file_name) == false)
			{
				continue;
			}

			url = parsed_url->ToUrlString(true);
		}
	}
}  // namespace info
