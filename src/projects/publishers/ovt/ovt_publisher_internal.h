//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/url.h>


class OvtStream;

namespace Json
{
	class Value;
}

namespace ovt_pub::internal
{
	/// Parses the `contents` object of an OVT `subscribe` message.
	///
	/// A null or absent `contents` is treated as a full-stream request.
	/// Empty `trackIds` arrays collapse to full-stream when `fullStream` is
	/// also true; otherwise they leave `track_ids` unset.
	///
	/// @param contents The `contents` JSON object from a `subscribe` message.
	/// @param[out] full_stream Set to `true` when the request is for the full
	///                         stream, `false` for a track subset.
	/// @param[out] track_ids Set to the parsed track id set, or left empty
	///                       when the request is full-stream.
	///
	/// @return `true` on a well-formed `contents`, `false` on parse error.
	bool ParseSubscribeSelection(const Json::Value &contents,
								 bool &full_stream,
								 std::optional<std::set<int32_t>> &track_ids);

	/// Resolves the canonical scope URL representing a `subscribe` selection.
	///
	/// Returns the stream-level (full) URL when `full_stream` is true, when
	/// `track_ids` is empty, or when the selection equals the union of all
	/// tracks. Otherwise tries an exact single-playlist match first, then
	/// falls back to the union-of-playlists shape (still represented by the
	/// stream-level URL because no single playlist file fits the scope).
	///
	/// @param base_url URL whose host / port / scheme components seed the
	///                 returned URL.
	/// @param stream Publisher stream the scope belongs to.
	/// @param track_ids Requested track ids; `std::nullopt` means full-stream.
	/// @param full_stream `true` to force full-stream regardless of `track_ids`.
	///
	/// @return Canonical scope URL, or `nullptr` if the selection cannot be
	///         expressed as any subset of registered playlists.
	std::shared_ptr<ov::Url> ResolveCanonicalSubscribeScopeUrl(const std::shared_ptr<const ov::Url> &base_url,
															   const std::shared_ptr<OvtStream> &stream,
															   const std::optional<std::set<int32_t>> &track_ids,
															   bool full_stream);
}  // namespace ovt_pub::internal
