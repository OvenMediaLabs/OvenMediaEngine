//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <base/info/application.h>
#include <base/info/host.h>
#include <base/info/stream.h>
#include <base/ovlibrary/url.h>
#include <base/provider/stream.h>
#include <gtest/gtest.h>

#include "streams_controller_internal.h"

// Management API request-scope helpers for stream pull/delete
namespace
{
	class TestApplicationInfo final : public info::Application
	{
	public:
		TestApplicationInfo(const ov::String &vhost_name, const ov::String &app_name)
			: info::Application(
				  info::Host("test-server", "test-server-id", cfg::vhost::VirtualHost()),
				  1,
				  info::VHostAppName(vhost_name, app_name),
				  false)
		{
		}
	};

	class TestProviderStream final : public pvd::Stream
	{
	public:
		explicit TestProviderStream(const info::Stream &stream_info)
			: pvd::Stream(nullptr, stream_info)
		{
		}

		void RegisterDownstreamRequest(const ov::String &request_key,
									   const std::shared_ptr<const ov::Url> &requested_url,
									   const std::shared_ptr<const ov::Url> &final_url) override
		{
			register_count++;
			last_request_key   = request_key;
			last_requested_url = requested_url;
			last_final_url	   = final_url;
		}

		void UnregisterDownstreamRequest(const ov::String &request_key) override
		{
			unregister_count++;
			last_unregistered_request_key = request_key;
		}

		void UnregisterDownstreamRequestsByKeyPrefix(const ov::String &request_key_prefix) override
		{
			unregister_by_prefix_count++;
			last_unregistered_request_key_prefix = request_key_prefix;
		}

		int register_count			   = 0;
		int unregister_count		   = 0;
		int unregister_by_prefix_count = 0;
		ov::String last_request_key;
		ov::String last_unregistered_request_key;
		ov::String last_unregistered_request_key_prefix;
		std::shared_ptr<const ov::Url> last_requested_url;
		std::shared_ptr<const ov::Url> last_final_url;
	};

	info::Stream MakeStreamInfo(const std::shared_ptr<info::Application> &app_info, const ov::String &stream_name)
	{
		info::Stream stream_info(*app_info, StreamSourceType::Ovt);
		stream_info.SetId(9);
		stream_info.SetName(stream_name);
		return stream_info;
	}
}  // namespace

TEST(ManagementApiRequestScope, SyntheticKeyIncludesScopeUrl)
{
	// Scope-aware key prevents two concurrent management-API requests for the same stream
	// (with different playlist scopes) from colliding under a single map slot. The prefix
	// uses the US (Unit Separator, `\x1f`) byte as delimiter - that character cannot
	// appear in any URL, vhost-app name, or stream name, so prefix-based DELETE cannot
	// collide on stream names that legitimately contain `:` (e.g. `foo:bar`).
	auto vhost_app = info::VHostAppName("default", "app");

	auto prefix	   = api::v1::internal::MakeManagementApiRequestScopeKeyPrefix(vhost_app, "stream");
	EXPECT_EQ(prefix, ov::String("management-api\x1f#default#app\x1fstream\x1f"));

	auto mobile_url = ov::Url::Parse("ovt://origin/app/stream/mobile");
	auto master_url = ov::Url::Parse("ovt://origin/app/stream/master");
	auto mobile_key = api::v1::internal::MakeManagementApiRequestScopeKey(vhost_app, "stream", mobile_url);
	auto master_key = api::v1::internal::MakeManagementApiRequestScopeKey(vhost_app, "stream", master_url);

	// Distinct scopes yield distinct keys ...
	EXPECT_NE(mobile_key, master_key);
	// ... but both share the stream-level prefix used by the DELETE handler.
	EXPECT_TRUE(mobile_key.HasPrefix(prefix));
	EXPECT_TRUE(master_key.HasPrefix(prefix));

	// Critical: a stream named `stream:bar` must NOT collide with `stream`'s prefix.
	// (Pre-fix: `:` delimiter would let `stream`'s prefix `management-api:.../stream:`
	//  match `stream:bar`'s key starting with `management-api:.../stream:bar:...`.)
	auto sibling_prefix = api::v1::internal::MakeManagementApiRequestScopeKeyPrefix(vhost_app, "stream:bar");
	auto sibling_key	= api::v1::internal::MakeManagementApiRequestScopeKey(vhost_app, "stream:bar", mobile_url);
	EXPECT_FALSE(sibling_key.HasPrefix(prefix)) << "DELETE /streams/stream must not match keys for /streams/stream:bar";
	EXPECT_TRUE(sibling_key.HasPrefix(sibling_prefix));
}

TEST(ManagementApiRequestScope, RegisterUsesFirstOvtUrlFromRequestBody)
{
	auto app_info		 = std::make_shared<TestApplicationInfo>("default", "app");
	auto provider_stream = std::make_shared<TestProviderStream>(MakeStreamInfo(app_info, "stream"));

	api::v1::internal::RegisterManagementApiRequestScope(
		provider_stream,
		app_info->GetVHostAppName(),
		"stream",
		{
			"https://origin.example.com/app/stream/master.m3u8",
			"ovt://origin.example.com:9000/app/stream/mobile?token=abc",
			"ovt://backup.example.com:9000/app/stream/master?token=def",
		});

	EXPECT_EQ(provider_stream->register_count, 1);
	// Key now includes the canonical scope URL so multiple concurrent management-API
	// requests for the same stream can coexist without overwriting each other.
	EXPECT_EQ(provider_stream->last_request_key,
			  ov::String("management-api\x1f#default#app\x1fstream\x1fovt://origin.example.com:9000/app/stream/mobile?token=abc"));
	ASSERT_NE(provider_stream->last_requested_url, nullptr);
	ASSERT_NE(provider_stream->last_final_url, nullptr);
	EXPECT_EQ(provider_stream->last_requested_url->ToUrlString(true), "ovt://origin.example.com:9000/app/stream/mobile?token=abc");
	EXPECT_EQ(provider_stream->last_final_url->ToUrlString(true), "ovt://origin.example.com:9000/app/stream/mobile?token=abc");
	EXPECT_EQ(provider_stream->last_requested_url->Path(), "/app/stream/mobile");
	EXPECT_EQ(provider_stream->last_final_url->Path(), "/app/stream/mobile");
}

TEST(ManagementApiRequestScope, RegisterSkipsNonOvtUrlsAndCleanupUsesSameKey)
{
	auto app_info		 = std::make_shared<TestApplicationInfo>("default", "app");
	auto provider_stream = std::make_shared<TestProviderStream>(MakeStreamInfo(app_info, "stream"));

	api::v1::internal::RegisterManagementApiRequestScope(
		provider_stream,
		app_info->GetVHostAppName(),
		"stream",
		{
			"https://origin.example.com/app/stream/master.m3u8",
			"rtsp://origin.example.com/app/stream",
		});

	EXPECT_EQ(provider_stream->register_count, 0);
	EXPECT_EQ(provider_stream->unregister_count, 0);

	api::v1::internal::UnregisterManagementApiRequestScope(
		provider_stream,
		app_info->GetVHostAppName(),
		"stream");

	// DELETE wipes out every scope-keyed entry for the stream via the prefix-based API,
	// not the single-key UnregisterDownstreamRequest path.
	EXPECT_EQ(provider_stream->unregister_count, 0);
	EXPECT_EQ(provider_stream->unregister_by_prefix_count, 1);
	EXPECT_EQ(provider_stream->last_unregistered_request_key_prefix,
			  ov::String("management-api\x1f#default#app\x1fstream\x1f"));
}

TEST(ManagementApiRequestScope, ConcurrentScopesOnSameStreamDoNotCollide)
{
	// Regression for B4: two POSTs for the same stream with different playlist scopes
	// must register independent demand entries (key includes the canonical scope URL).
	auto app_info		 = std::make_shared<TestApplicationInfo>("default", "app");
	auto provider_stream = std::make_shared<TestProviderStream>(MakeStreamInfo(app_info, "stream"));

	api::v1::internal::RegisterManagementApiRequestScope(
		provider_stream,
		app_info->GetVHostAppName(),
		"stream",
		{"ovt://origin/app/stream/mobile"});
	auto first_key = provider_stream->last_request_key;

	api::v1::internal::RegisterManagementApiRequestScope(
		provider_stream,
		app_info->GetVHostAppName(),
		"stream",
		{"ovt://origin/app/stream/master"});
	auto second_key = provider_stream->last_request_key;

	EXPECT_EQ(provider_stream->register_count, 2);
	EXPECT_NE(first_key, second_key);
	EXPECT_TRUE(first_key.HasPrefix(ov::String("management-api\x1f#default#app\x1fstream\x1f")));
	EXPECT_TRUE(second_key.HasPrefix(ov::String("management-api\x1f#default#app\x1fstream\x1f")));
}

// Regression for B2 + B3-2 (issues.md re-review): the validator must reject genuine
// scope ambiguity (multiple OVT URLs with different scopes when there's no
// normalizing HTTP source) but accept legitimate mixed shapes (an HTTP
// request_from + a bare OVT URL + an already-scoped OVT URL whose scope matches
// the request_from). The validator is request_from-aware to make this distinction.
TEST(ManagementApiRequestScope, ValidateOvtUrlsRejectsInconsistentScopes)
{
	ov::String mismatch_message;
	auto resolve = [](const std::vector<ov::String> &urls) {
		return api::v1::internal::ResolveManagementApiRequestScopeUrl(urls);
	};

	// All OVT URLs share the same scope -> accepted (case (2): no normalizing source,
	// since first OVT URL has no playlist file).
	std::vector<ov::String> consistent_full_stream = {
		"ovt://origin1.example.com:9000/app/stream",
		"ovt://origin2.example.com:9000/app/stream",
	};
	EXPECT_TRUE(api::v1::internal::ValidateManagementApiOvtRequestScopesConsistent(
		consistent_full_stream, resolve(consistent_full_stream), mismatch_message));
	EXPECT_TRUE(mismatch_message.IsEmpty());

	// Both OVT URLs with the same playlist scope -> accepted (case (1) since
	// request_from = first OVT URL has scope "mobile"; the second URL also has "mobile").
	std::vector<ov::String> consistent_playlist_scope = {
		"ovt://origin1.example.com:9000/app/stream/mobile",
		"ovt://origin2.example.com:9000/app/stream/mobile",
	};
	EXPECT_TRUE(api::v1::internal::ValidateManagementApiOvtRequestScopesConsistent(
		consistent_playlist_scope, resolve(consistent_playlist_scope), mismatch_message));

	// Mixed OVT scopes -> rejected via case (1): first OVT URL gives normalizing scope
	// "mobile"; second OVT URL has different scope "master" which would be silently
	// overwritten by Append.
	std::vector<ov::String> mixed_scopes = {
		"ovt://origin1/app/stream/mobile",
		"ovt://origin2/app/stream/master",
	};
	EXPECT_FALSE(api::v1::internal::ValidateManagementApiOvtRequestScopesConsistent(
		mixed_scopes, resolve(mixed_scopes), mismatch_message));
	EXPECT_FALSE(mismatch_message.IsEmpty());
	EXPECT_TRUE(mismatch_message.IndexOf("master") >= 0);

	// Bare OVT URL + scoped OVT URL with first being the bare one -> case (2):
	// request_from = first OVT URL has empty scope, no normalizing source, so OVT
	// scopes must agree. Disagreement -> rejected.
	std::vector<ov::String> mixed_full_and_playlist_bare_first = {
		"ovt://origin1/app/stream",
		"ovt://origin2/app/stream/mobile",
	};
	EXPECT_FALSE(api::v1::internal::ValidateManagementApiOvtRequestScopesConsistent(
		mixed_full_and_playlist_bare_first,
		resolve(mixed_full_and_playlist_bare_first),
		mismatch_message));

	// Extension differences are normalized away (`master` and `master.m3u8` resolve
	// to the same canonical scope) - case (1) where the first OVT URL gives scope
	// "master" and the second URL's "master.m3u8" strips to the same.
	std::vector<ov::String> consistent_after_strip = {
		"ovt://origin1/app/stream/master",
		"ovt://origin2/app/stream/master.m3u8",
	};
	EXPECT_TRUE(api::v1::internal::ValidateManagementApiOvtRequestScopesConsistent(
		consistent_after_strip, resolve(consistent_after_strip), mismatch_message));

	// HTTP normalizing source + bare OVT URLs -> case (1): all OVT URLs are bare so
	// Append will inject the HTTP scope into them. Accepted.
	std::vector<ov::String> http_scope_with_bare_ovt = {
		"http://hls-origin/app/stream/master.m3u8",
		"ovt://origin1/app/stream",
		"ovt://origin2/app/stream",
	};
	EXPECT_TRUE(api::v1::internal::ValidateManagementApiOvtRequestScopesConsistent(
		http_scope_with_bare_ovt, resolve(http_scope_with_bare_ovt), mismatch_message));

	// B3-2: HTTP normalizing source + bare OVT + matching-scope OVT -> case (1):
	// the explicitly-scoped OVT URL agrees with request_from, the bare URL gets
	// filled in by Append. Legitimate mixed shape, must be ACCEPTED.
	std::vector<ov::String> http_scope_with_mixed_consistent_ovt = {
		"http://hls-origin/app/stream/master.m3u8",
		"ovt://origin1/app/stream",
		"ovt://origin2/app/stream/master",
	};
	EXPECT_TRUE(api::v1::internal::ValidateManagementApiOvtRequestScopesConsistent(
		http_scope_with_mixed_consistent_ovt,
		resolve(http_scope_with_mixed_consistent_ovt),
		mismatch_message));

	// B3-2 negative: HTTP normalizing source provides scope "master", but an OVT URL
	// has scope "mobile" which would be silently overwritten -> rejected.
	std::vector<ov::String> http_scope_with_disagreeing_ovt = {
		"http://hls-origin/app/stream/master.m3u8",
		"ovt://origin1/app/stream/mobile",
	};
	EXPECT_FALSE(api::v1::internal::ValidateManagementApiOvtRequestScopesConsistent(
		http_scope_with_disagreeing_ovt,
		resolve(http_scope_with_disagreeing_ovt),
		mismatch_message));
	EXPECT_TRUE(mismatch_message.IndexOf("mobile") >= 0);
	EXPECT_TRUE(mismatch_message.IndexOf("master") >= 0);

	// Empty list is a no-op.
	std::vector<ov::String> empty;
	EXPECT_TRUE(api::v1::internal::ValidateManagementApiOvtRequestScopesConsistent(
		empty, nullptr, mismatch_message));
}

// Regression for B3-2 (issues.md re-review B3-2): when an HTTP `request_from`
// supplies the normalizing scope, OVT URLs are allowed to be a mix of bare
// (Append fills) and already-scoped (must match the normalizing scope). This
// shape was incorrectly rejected by the earlier request_from-unaware validator.
TEST(ManagementApiRequestScope, ValidateOvtUrlsAcceptsHttpScopedMixedBody)
{
	std::vector<ov::String> request_urls = {
		"http://hls-origin/app/stream/master.m3u8",	 // request_from supplies scope "master"
		"ovt://origin1/app/stream",					 // bare; Append will fill with "master"
		"ovt://origin2/app/stream/master",			 // already scoped; matches "master"
	};

	auto request_from = api::v1::internal::ResolveManagementApiRequestScopeUrl(request_urls);
	ASSERT_NE(request_from, nullptr);

	ov::String mismatch_message;
	EXPECT_TRUE(api::v1::internal::ValidateManagementApiOvtRequestScopesConsistent(
		request_urls, request_from, mismatch_message))
		<< "Validator must accept post-Append-consistent mixed body: " << mismatch_message.CStr();

	// Sanity: after Append both OVT URLs do end up with the same scope.
	info::AppendPlaylistScopeToOvtUrls(request_from, request_urls);
	EXPECT_EQ(request_urls[1], "ovt://origin1/app/stream/master");
	EXPECT_EQ(request_urls[2], "ovt://origin2/app/stream/master");
}

TEST(ManagementApiRequestScope, ResolvedUrlListYieldsScopedDemandForMixedRequestBody)
{
	// NEW-4 regression: when a management API request body mixes a playlist-scoped non-OVT
	// URL (e.g. http://.../mobile.m3u8) with a scope-less OVT URL (e.g. ovt://origin/app/stream),
	// the actual pull is playlist-scoped (ovt://.../mobile.m3u8) but the demand registration
	// used to read the *original* request_urls and find the scope-less OVT URL, registering
	// a full-stream demand. The fix applies AppendPlaylistScopeToOvtUrls before scope
	// registration so both flows agree on the resolved URL.
	auto app_info						 = std::make_shared<TestApplicationInfo>("default", "app");
	auto provider_stream				 = std::make_shared<TestProviderStream>(MakeStreamInfo(app_info, "stream"));

	std::vector<ov::String> request_urls = {
		"http://hls-origin/app/stream/mobile.m3u8",
		"ovt://origin/app/stream",
	};

	auto request_from = api::v1::internal::ResolveManagementApiRequestScopeUrl(request_urls);
	ASSERT_NE(request_from, nullptr);
	EXPECT_EQ(request_from->Scheme().UpperCaseString(), "HTTP");

	// Same call streams_controller now performs before pull + scope registration.
	info::AppendPlaylistScopeToOvtUrls(request_from, request_urls);

	// OVT URL is now playlist-scoped to match what the actual pull will use. Note that the
	// OVT URL carries the playlist file *name* without extension because OME stores playlist
	// `_file_name` without extension (see info::AppendPlaylistScopeToOvtUrls).
	// The HTTP URL is left untouched.
	EXPECT_EQ(request_urls[0], "http://hls-origin/app/stream/mobile.m3u8");
	EXPECT_EQ(request_urls[1], "ovt://origin/app/stream/mobile");

	api::v1::internal::RegisterManagementApiRequestScope(
		provider_stream,
		app_info->GetVHostAppName(),
		"stream",
		request_urls);

	EXPECT_EQ(provider_stream->register_count, 1);
	ASSERT_NE(provider_stream->last_requested_url, nullptr);
	ASSERT_NE(provider_stream->last_final_url, nullptr);
	// Demand path now reflects the playlist scope -- not the bogus "/app/stream" full-stream
	// path that the unfixed flow registered.
	EXPECT_EQ(provider_stream->last_requested_url->Path(), "/app/stream/mobile");
	EXPECT_EQ(provider_stream->last_final_url->Path(), "/app/stream/mobile");
}
