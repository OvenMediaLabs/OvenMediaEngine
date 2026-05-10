//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/playlist_file.h>
#include <base/info/vhost_app_name.h>
#include <base/ovlibrary/string.h>
#include <base/ovlibrary/url.h>
#include <base/provider/stream.h>

namespace api::v1::internal
{
	/// Tests whether a parsed URL is an OVT-scheme management API request scope URL.
	///
	/// @param request_scope_url Parsed URL to test. May be `nullptr`.
	///
	/// @return `true` if non-null and uses scheme `OVT`, otherwise `false`.
	inline bool IsOvtManagementApiRequestScopeUrl(const std::shared_ptr<const ov::Url> &request_scope_url)
	{
		return (request_scope_url != nullptr) && (request_scope_url->Scheme().UpperCaseString() == "OVT");
	}

	/// Builds the common prefix shared by every management API request scope key
	/// for a given stream, so callers can wipe all scopes for the stream via
	/// prefix scan. The trailing delimiter is `\x1f` (US) rather than `:`
	/// because OME stream names may legally contain `:`, which would let
	/// `DELETE /streams/foo` prefix-match keys for `foo:bar`. `\x1f` cannot
	/// appear in any URL or name OME parses.
	///
	/// @param vhost_app_name Vhost-qualified application name owning the stream.
	/// @param stream_name Stream name.
	///
	/// @return Prefix string ending in `\x1f`.
	inline ov::String MakeManagementApiRequestScopeKeyPrefix(const info::VHostAppName &vhost_app_name, const ov::String &stream_name)
	{
		return ov::String::FormatString("management-api\x1f%s\x1f%s\x1f", vhost_app_name.CStr(), stream_name.CStr());
	}

	/// Builds a unique key for a single management API request scope.
	///
	/// Embedding the canonical scope URL lets concurrent management API demands
	/// on the same stream (e.g. `mobile` and `master` playlists) coexist; a
	/// stream-only key would silently collapse them and lose one demand on
	/// unregister.
	///
	/// @param vhost_app_name Vhost-qualified application name owning the stream.
	/// @param stream_name Stream name.
	/// @param canonical_request_scope_url Canonical URL identifying the scope.
	///        May be `nullptr`, in which case only the prefix is returned.
	///
	/// @return Full key string.
	inline ov::String MakeManagementApiRequestScopeKey(const info::VHostAppName &vhost_app_name,
													   const ov::String &stream_name,
													   const std::shared_ptr<const ov::Url> &canonical_request_scope_url)
	{
		auto scope_suffix = (canonical_request_scope_url != nullptr)
								? canonical_request_scope_url->ToUrlString(true)
								: ov::String("");
		return ov::String::FormatString(
			"%s%s",
			MakeManagementApiRequestScopeKeyPrefix(vhost_app_name, stream_name).CStr(),
			scope_suffix.CStr());
	}

	/// Returns the first parsable URL in `request_urls`, or `nullptr` if none parse.
	///
	/// @param request_urls Candidate URL strings.
	///
	/// @return Parsed URL, or `nullptr` if no entry parses.
	inline std::shared_ptr<const ov::Url> ResolveManagementApiRequestScopeUrl(const std::vector<ov::String> &request_urls)
	{
		for (const auto &request_url : request_urls)
		{
			auto parsed_url = ov::Url::Parse(request_url);

			if (parsed_url != nullptr)
			{
				return parsed_url;
			}
		}

		return nullptr;
	}

	/// Returns the first OVT-scheme URL in `request_urls`, or `nullptr` if none qualify.
	///
	/// @param request_urls Candidate URL strings.
	///
	/// @return Parsed OVT URL, or `nullptr` if no entry is OVT.
	inline std::shared_ptr<const ov::Url> ResolveManagementApiOvtRequestScopeUrl(const std::vector<ov::String> &request_urls)
	{
		for (const auto &request_url : request_urls)
		{
			auto parsed_url = ov::Url::Parse(request_url);

			if (IsOvtManagementApiRequestScopeUrl(parsed_url))
			{
				return parsed_url;
			}
		}

		return nullptr;
	}

	/// Rejects request bodies that would yield ambiguous demand after
	/// `AppendPlaylistScopeToOvtUrls(request_from, ...)`:
	///
	/// (1) `request_from` has a playlist scope -> Append overwrites every OVT
	///     URL's file segment. Reject any OVT URL with a *different* explicit
	///     scope (it would be silently lost). Bare OVT URLs are fine.
	/// (2) `request_from` has no scope -> Append is a no-op, so OVT URLs keep
	///     their original scopes. Reject if they disagree (the resulting demand
	///     would be URL-ordering-dependent).
	///
	/// MUST run before `AppendPlaylistScopeToOvtUrls` so the diagnostic can
	/// name URLs in their original form.
	///
	/// @param request_urls Request body URLs (mixed schemes allowed).
	/// @param request_from Trigger URL whose scope is propagated by Append.
	///                     May be `nullptr`.
	/// @param[out] mismatch_message Filled with a human-readable explanation
	///                              when validation fails; cleared on success.
	///
	/// @return `true` when no OVT URLs are present or all checks pass.
	inline bool ValidateManagementApiOvtRequestScopesConsistent(const std::vector<ov::String> &request_urls,
																const std::shared_ptr<const ov::Url> &request_from,
																ov::String &mismatch_message)
	{
		mismatch_message = "";

		ov::String normalizing_scope;
		if (request_from != nullptr)
		{
			normalizing_scope = info::StripPlaylistFileExtension(
				info::GetMasterPlaylistFileName(*request_from));
		}

		if (normalizing_scope.IsEmpty() == false)
		{
			// Case (1): Append will inject `normalizing_scope` into every OVT URL.
			// Reject any OVT URL whose explicit scope disagrees; bare OVT URLs are OK.
			for (const auto &request_url : request_urls)
			{
				auto parsed_url = ov::Url::Parse(request_url);
				if (IsOvtManagementApiRequestScopeUrl(parsed_url) == false)
				{
					continue;
				}

				auto scope = info::StripPlaylistFileExtension(parsed_url->File());
				if ((scope.IsEmpty() == false) && (scope != normalizing_scope))
				{
					mismatch_message = ov::String::FormatString(
						"OVT URL `%s` carries scope '%s' but the request resolves to scope '%s'; "
						"AppendPlaylistScopeToOvtUrls would silently overwrite the URL's scope. "
						"Either drop the explicit scope on the OVT URL or align it with the request scope.",
						request_url.CStr(),
						scope.CStr(),
						normalizing_scope.CStr());
					return false;
				}
			}
			return true;
		}

		// Case (2): no normalizing source. OVT URLs keep their original scopes - if
		// they disagree, the demand is ambiguous and ordering-dependent.
		std::optional<ov::String> shared_scope;
		ov::String first_url_for_diagnosis;

		for (const auto &request_url : request_urls)
		{
			auto parsed_url = ov::Url::Parse(request_url);
			if (IsOvtManagementApiRequestScopeUrl(parsed_url) == false)
			{
				continue;
			}

			auto scope = info::StripPlaylistFileExtension(parsed_url->File());
			if (shared_scope.has_value() == false)
			{
				shared_scope			= scope;
				first_url_for_diagnosis = request_url;
				continue;
			}

			if (*shared_scope != scope)
			{
				mismatch_message = ov::String::FormatString(
					"OVT URLs in `urls` must share the same playlist scope when no HTTP "
					"normalizing URL is present; `%s` resolves to scope '%s' while `%s` "
					"resolves to scope '%s'.",
					first_url_for_diagnosis.CStr(),
					shared_scope->IsEmpty() ? "<full-stream>" : shared_scope->CStr(),
					request_url.CStr(),
					scope.IsEmpty() ? "<full-stream>" : scope.CStr());

				return false;
			}
		}
		return true;
	}

	/// Builds the canonical (server-side) form of a management API request
	/// scope URL. Replaces the path with `/<vhost_app_name>/<stream_name>`,
	/// re-appending the playlist file segment if the remote URL had one.
	/// Used so request scopes from different remote hosts collapse to the
	/// same key for the same logical demand on this server.
	///
	/// @param remote_request_scope_url Original URL from the request body.
	///        May be `nullptr` (returns `nullptr`).
	/// @param vhost_app_name Vhost-qualified application name owning the stream.
	/// @param stream_name Stream name.
	///
	/// @return Canonicalised URL, or `nullptr` if `remote_request_scope_url`
	///         is `nullptr` or canonicalisation fails.
	inline std::shared_ptr<const ov::Url> BuildManagementApiCanonicalRequestScopeUrl(
		const std::shared_ptr<const ov::Url> &remote_request_scope_url,
		const info::VHostAppName &vhost_app_name,
		const ov::String &stream_name)
	{
		if (remote_request_scope_url == nullptr)
		{
			return nullptr;
		}

		auto canonical_request_scope_url = remote_request_scope_url->Clone();
		if (canonical_request_scope_url == nullptr)
		{
			return nullptr;
		}

		auto canonical_path = ov::String::FormatString(
			"/%s/%s",
			vhost_app_name.GetAppName().CStr(),
			stream_name.CStr());

		auto playlist_file_name = remote_request_scope_url->File();
		if (playlist_file_name.IsEmpty() == false)
		{
			canonical_path.AppendFormat("/%s", playlist_file_name.CStr());
		}

		if (canonical_request_scope_url->SetPath(canonical_path) == false)
		{
			return nullptr;
		}

		return canonical_request_scope_url;
	}

	/// Registers a management API pull demand on the linked OVT provider stream.
	/// No-op when `provider_stream` is `nullptr`, when its source type is not
	/// OVT, or when no OVT URL is present in `request_urls`.
	///
	/// @param provider_stream Provider stream to register the demand on.
	/// @param vhost_app_name Vhost-qualified application name owning the stream.
	/// @param stream_name Stream name.
	/// @param request_urls Request body URLs.
	inline void RegisterManagementApiRequestScope(const std::shared_ptr<pvd::Stream> &provider_stream,
												  const info::VHostAppName &vhost_app_name,
												  const ov::String &stream_name,
												  const std::vector<ov::String> &request_urls)
	{
		if ((provider_stream == nullptr) || (provider_stream->GetSourceType() != StreamSourceType::Ovt))
		{
			return;
		}

		auto request_scope_url = ResolveManagementApiOvtRequestScopeUrl(request_urls);
		if (request_scope_url == nullptr)
		{
			return;
		}

		auto canonical_request_scope_url = BuildManagementApiCanonicalRequestScopeUrl(
			request_scope_url,
			vhost_app_name,
			stream_name);

		if (canonical_request_scope_url == nullptr)
		{
			return;
		}

		provider_stream->RegisterDownstreamRequest(
			MakeManagementApiRequestScopeKey(vhost_app_name, stream_name, canonical_request_scope_url),
			canonical_request_scope_url,
			canonical_request_scope_url);
	}

	/// Tests whether an existing provider stream can absorb a new management
	/// API request without being torn down and re-pulled.
	///
	/// @param provider_stream Existing provider stream, or `nullptr`.
	/// @param request_urls Request body URLs.
	///
	/// @return `true` if the stream is OVT-sourced and the request body
	///         contains at least one OVT URL.
	inline bool CanReuseManagementApiExistingStream(const std::shared_ptr<pvd::Stream> &provider_stream,
													const std::vector<ov::String> &request_urls)
	{
		return (provider_stream != nullptr) &&
			   (provider_stream->GetSourceType() == StreamSourceType::Ovt) &&
			   (ResolveManagementApiOvtRequestScopeUrl(request_urls) != nullptr);
	}

	/// Removes every management API scope for the stream. `DELETE /streams/<name>`
	/// is stream-level, so all scope-keyed demand entries under the prefix go.
	/// No-op when `provider_stream` is `nullptr` or its source type is not OVT.
	///
	/// @param provider_stream Provider stream to wipe scopes from.
	/// @param vhost_app_name Vhost-qualified application name owning the stream.
	/// @param stream_name Stream name.
	inline void UnregisterManagementApiRequestScope(const std::shared_ptr<pvd::Stream> &provider_stream,
													const info::VHostAppName &vhost_app_name,
													const ov::String &stream_name)
	{
		if ((provider_stream == nullptr) || (provider_stream->GetSourceType() != StreamSourceType::Ovt))
		{
			return;
		}

		provider_stream->UnregisterDownstreamRequestsByKeyPrefix(
			MakeManagementApiRequestScopeKeyPrefix(vhost_app_name, stream_name));
	}
}  // namespace api::v1::internal
