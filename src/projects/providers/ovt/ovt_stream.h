//
// Created by getroot on 19. 12. 9.
//

#pragma once

#include <base/common_types.h>
#include <base/ovlibrary/url.h>
#include <base/ovlibrary/semaphore.h>
#include <modules/ovt_packetizer/ovt_packet.h>
#include <modules/ovt_packetizer/ovt_packetizer.h>
#include <modules/ovt_packetizer/ovt_depacketizer.h>
#include <modules/ovt_packetizer/ovt_signaling.h>
#include <monitoring/monitoring.h>

#include <base/provider/pull_provider/application.h>
#include <base/provider/pull_provider/stream.h>

#define OVT_TIMEOUT_MSEC		3000

namespace pvd
{
	class OvtProvider;

	class OvtStream final : public pvd::PullStream, public OvtPacketizerInterface
	{
	public:
		struct PendingControlRequest;
		using PendingControlRequestPtr = std::shared_ptr<PendingControlRequest>;
		struct ActiveRequestScope
		{
			std::shared_ptr<ov::Url> requested_url;
			std::shared_ptr<ov::Url> final_url;
			bool has_authoritative_scope = false;
			ov::String playlist_file_name;
			std::set<int32_t> resolved_track_ids;
			// Explicit authoritative track set (e.g. from a runtime `subscribe`
			// spanning multiple playlists). When present, this is the source of
			// truth for `resolved_target_track_ids`; the URL is informational.
			// Required to represent a multi-playlist union, since a URL only
			// encodes "full" or "single playlist" and would silently downgrade
			// a union to "full".
			std::optional<std::set<int32_t>> authoritative_resolved_track_ids;
		};

		enum class InventoryUpdateMode : uint8_t
		{
			NONE,
			UPSERT_PARTIAL,
			REPLACE_ALL,
		};

		enum class InventorySnapshotState : uint8_t
		{
			UNKNOWN,
			PARTIAL,
			FULL_SNAPSHOT,
		};

		struct SharedRuntimeCapabilityState
		{
			ovt::CapabilitySupport runtime_widening = ovt::CapabilitySupport::UNKNOWN;
		};

		struct CurrentUpstreamSubscriptionState
		{
			bool is_known					 = false;
			bool is_full_stream				 = false;
			bool track_ids_are_authoritative = false;
			std::set<int32_t> resolved_track_ids;
		};

		struct SharedRequestState
		{
			uint32_t full_request_count						= 0;
			bool target_requires_full_stream				= false;
			InventorySnapshotState inventory_snapshot_state = InventorySnapshotState::UNKNOWN;
			bool target_track_ids_are_authoritative			= false;
			bool target_computation_deferred				= false;
			bool runtime_widening_required					= false;
			bool compatibility_fallback_required			= false;
			std::map<ov::String, uint32_t> playlist_request_counts;
			std::set<ov::String> unresolved_playlist_file_names;
			std::map<int32_t, uint32_t> track_ref_counts;
			std::set<int32_t> resolved_target_track_ids;
		};

		static std::shared_ptr<OvtStream> Create(const std::shared_ptr<pvd::PullApplication> &application, const uint32_t stream_id, const ov::String &stream_name,	const std::vector<ov::String> &url_list, const std::shared_ptr<pvd::PullStreamProperties> &properties);

		OvtStream(const std::shared_ptr<pvd::PullApplication> &application, const info::Stream &stream_info, const std::vector<ov::String> &url_list, const std::shared_ptr<pvd::PullStreamProperties> &properties);
		~OvtStream() final;

		bool OnOvtPacketized(std::shared_ptr<OvtPacket> &packet) override;

		ProcessMediaEventTrigger GetProcessMediaEventTriggerMode() override {
			return ProcessMediaEventTrigger::TRIGGER_EPOLL;
		}

		int GetFileDescriptorForDetectingEvent() override;
		// If this stream belongs to the Pull provider, 
		// this function is called periodically by the StreamMotor of application. 
		// Media data has to be processed here.
		PullStream::ProcessMediaResult ProcessMediaPacket() override;
		void RegisterDownstreamSession(uint32_t session_id,
									   const std::shared_ptr<const ov::Url> &requested_url,
									   const std::shared_ptr<const ov::Url> &final_url,
									   const std::optional<std::set<int32_t>> &authoritative_resolved_track_ids) override;
		// Inherit the base 3-arg wrapper that forwards with `nullopt`.
		using pvd::Stream::RegisterDownstreamSession;
		void UnregisterDownstreamSession(uint32_t session_id) override;
		void RegisterDownstreamRequest(const ov::String &request_key,
									   const std::shared_ptr<const ov::Url> &requested_url,
									   const std::shared_ptr<const ov::Url> &final_url) override;
		void UnregisterDownstreamRequest(const ov::String &request_key) override;
		void UnregisterDownstreamRequestsByKeyPrefix(const ov::String &request_key_prefix) override;

	private:
		struct UpstreamBootstrapScope
		{
			bool is_resolved	= false;
			bool is_full_stream = false;
			ov::String playlist_file_name;
			std::set<int32_t> resolved_track_ids;
		};

		enum class ReceivePacketResult : uint8_t
		{
			COMPLETE,
			INCOMPLETE,
			DISCONNECTED,
			ERROR, 
			TIMEOUT,
			ALREADY_COMPLETED,
		};

		std::shared_ptr<pvd::OvtProvider> GetOvtProvider();

		bool StartStream(const std::shared_ptr<const ov::Url> &url) override; // Start
		bool RestartStream(const std::shared_ptr<const ov::Url> &url) override; // Failover
		bool StopStream() override; // Stop

		bool ConnectOrigin();
		bool RequestDescribe();
		bool ReceiveDescribe(uint32_t request_id);
		bool HandleDescribeResponse(const PendingControlRequest &pending_request);
		void MaybeRequestRuntimeFullDescribeRefresh();
		bool RefreshFullInventoryAfterPlaylistBootstrap();
		bool RetryBootstrapInCompatibilityMode(const std::shared_ptr<const ov::Url> &playlist_scoped_url);
		bool TryPrepareCompatibilityBootstrapRetry(const std::shared_ptr<const ov::Url> &playlist_scoped_url,
												   std::shared_ptr<const ov::Url> &full_stream_target_url);
		void FinalizeCompatibilityBootstrapRetryPreparation();
		bool RequestPlay();
		bool ReceivePlay(uint32_t request_id);
		bool HandlePlayResponse(const PendingControlRequest &pending_request);
		bool RequestStop();
		bool ReceiveStop(uint32_t request_id, const std::shared_ptr<OvtPacket> &packet);
		bool SendControlRequest(const char *application, uint32_t request_id, const std::shared_ptr<const ov::Url> &target_url);
		bool SendControlRequest(const char *application, uint32_t request_id, const std::shared_ptr<const ov::Url> &target_url, const Json::Value &contents);
		PendingControlRequestPtr CreatePendingControlRequest(const ov::String &application, uint32_t request_id);
		PendingControlRequestPtr TakePendingControlRequest(uint32_t request_id);
		void RemovePendingControlRequest(uint32_t request_id);
		void DiscardPendingControlRequestAsTimedOut(uint32_t request_id);
		void ClearPendingControlRequests();
		bool IsPendingControlRequestCompleted(uint32_t request_id);
		void RememberCompletedControlRequestIdLocked(uint32_t request_id);
		bool WasRecentlyCompletedControlRequestId(uint32_t request_id);
		bool CompletePendingControlResponse(uint32_t request_id, const ov::String &application, uint32_t code, const ov::String &message, const ov::String &payload);
		bool WaitForPendingControlResponse(uint32_t request_id, uint32_t timeout_msec);
		bool DispatchBufferedControlMessage(bool *handled, bool *stop_requested = nullptr, bool *has_buffered_packets = nullptr);
		bool DispatchControlMessage(const std::shared_ptr<ov::Data> &message, bool *stop_requested = nullptr);
		bool DispatchControlMessageLocked(const std::shared_ptr<ov::Data> &message, bool *stop_requested = nullptr);
		void ProcessCompletedRuntimeControlRequests();
		void MaybeRequestRuntimeSubscribe();
		bool IsCurrentConnectionGeneration(uint64_t connection_generation) const;
		std::shared_ptr<const ov::Url> BuildFullStreamTargetUrlFromBaseUrl(const std::shared_ptr<const ov::Url> &base_url) const;
		std::shared_ptr<const ov::Url> BuildPlaylistScopedTargetUrlFromBaseUrl(const std::shared_ptr<const ov::Url> &base_url,
																			   const ov::String &playlist_file_name) const;
		std::shared_ptr<const ov::Url> BuildRuntimeFullDescribeTargetUrl() const;
		bool BuildRuntimeSubscribeTarget(std::shared_ptr<const ov::Url> &target_url, bool &full_stream, std::set<int32_t> &track_ids) const;
		bool ResolveTrackIdsForPlaylist(const ov::String &playlist_file_name, std::set<int32_t> &track_ids) const;
		bool ResolveCurrentUpstreamBootstrapScopeLocked(UpstreamBootstrapScope &scope) const;
		bool ResolveCurrentUpstreamTargetLocked(CurrentUpstreamSubscriptionState &state) const;
		void ResetCurrentUpstreamSubscriptionStateLocked();
		void ApplyCurrentUpstreamSubscriptionStateFromPlayLocked(const std::shared_ptr<const ov::Url> &target_url);
		void ApplyCurrentUpstreamSubscriptionStateFromSubscribeLocked(bool full_stream, const std::optional<std::set<int32_t>> &track_ids);
		bool HandleSubscribeResponse(const PendingControlRequest &pending_request);
		bool HandleDescribeResponseForTest(const ov::String &expected_application,
										   const ov::String &response_application,
										   uint32_t response_code,
										   const ov::String &response_payload,
										   InventoryUpdateMode inventory_update_mode);
		bool QueueCompletedRuntimeDescribeRefreshForTest(uint32_t request_id,
														 const std::shared_ptr<const ov::Url> &target_url,
														 uint32_t response_code,
														 const ov::String &response_message,
														 const ov::String &response_payload = "");
		bool HandleSubscribeResponseForTest(const ov::String &expected_application,
											const ov::String &response_application,
											uint32_t response_code,
											bool requested_full_stream,
											const std::optional<std::set<int32_t>> &requested_track_ids);
		bool ShouldTriggerCompatibilityFallbackRestart(std::shared_ptr<const ov::Url> *restart_target_url = nullptr);
		bool ShouldFinishProcessMediaForCompatibilityFallback(std::shared_ptr<const ov::Url> *restart_target_url = nullptr);
		std::shared_ptr<const ov::Url> CloneCurrentUrl() const;
		std::shared_ptr<const ov::Url> ResolveRestartTargetUrl(const std::shared_ptr<const ov::Url> &url) const;
		using StartStreamInvoker = std::function<bool(const std::shared_ptr<const ov::Url> &url)>;
		void SetStartStreamInvokerForTest(StartStreamInvoker invoker);
		bool InvokeStartStream(const std::shared_ptr<const ov::Url> &url);
		uint32_t AllocateRequestId();
		void ResetBufferedReceiveStateLocked();
		void BeginConnectionGeneration();
		bool IsStaleControlResponseFromPreviousGenerationLocked(uint32_t request_id) const;
		bool IsStaleControlResponseFromPreviousGeneration(uint32_t request_id) const;
		void InvalidateInventoryAuthority();
		void InvalidateInventoryAuthorityLocked();
		bool ShouldRequestRuntimeFullDescribeRefreshLocked(uint64_t *state_revision = nullptr) const;
		void ResetRuntimeDescribeRefreshStateLocked();
		void ResetSharedRuntimeCapabilityStateLocked();
		void ResetRuntimeDescribeRefreshRetryStateLocked(uint32_t request_id = 0);
		void ClearRuntimeDescribeRefreshInFlightLocked(uint32_t request_id = 0);
		void FinalizeRuntimeDescribeRefreshAttemptLocked(uint32_t request_id, bool refresh_succeeded);
		void RecalculateActiveRequestState();
		void RecalculateActiveRequestStateLocked();
		bool TryBuildPlaylistReprobeTargetForReconnectLocked(const std::shared_ptr<const ov::Url> &base_url,
															 std::shared_ptr<const ov::Url> &target_url) const;
		bool TrySelectRepresentativePlaylistForReconnectLocked(ov::String &playlist_file_name) const;
		bool TryAccumulateActiveRequestScopeLocked(ActiveRequestScope &active_request_scope,
												   SharedRequestState &shared_request_state,
												   const ov::String &normalized_app_name,
												   const ov::String &normalized_stream_name,
												   const ov::String &normalized_stream_path) const;
		bool HasAuthoritativeInventorySnapshotLocked() const;
		void UpdateInventorySnapshotStateLocked(bool inventory_replace_applied);

		ReceivePacketResult ReceivePacket(bool non_block = false);

		void Release();

		mutable std::mutex _connection_handoff_lock;
		// Lock ordering contract: callers may acquire `_active_request_state_lock` and then
		// touch connection state helpers, but the reverse order must not be introduced.
		mutable std::mutex _connection_state_lock;
		std::shared_ptr<ov::Socket> _client_socket = nullptr;
		std::shared_ptr<const ov::Url> _curr_url = nullptr;

		uint32_t _last_request_id;

		int64_t _origin_request_time_msec = 0;
		int64_t _origin_response_time_msec = 0;

		std::shared_mutex	_packetizer_lock;
		std::shared_ptr<OvtPacketizer>	_packetizer;
		OvtDepacketizer _depacketizer;
		std::mutex _pending_control_requests_lock;
		std::map<uint32_t, PendingControlRequestPtr> _pending_control_requests;
		std::deque<uint32_t> _recently_completed_control_request_ids;
		mutable std::mutex _active_request_state_lock;
		std::map<uint32_t, ActiveRequestScope> _active_request_sessions;
		std::map<ov::String, ActiveRequestScope> _active_request_scopes;
		uint64_t _active_request_state_revision						 = 0;
		uint64_t _runtime_full_describe_refresh_attempted_revision	 = 0;
		uint32_t _runtime_full_describe_refresh_in_flight_request_id = 0;
		int64_t _runtime_full_describe_refresh_request_time_msec	 = 0;
		uint64_t _runtime_subscribe_attempted_revision				 = 0;
		uint32_t _runtime_subscribe_in_flight_request_id			 = 0;
		int64_t _runtime_subscribe_request_time_msec				 = 0;
		bool _compatibility_full_stream_mode_requested				 = false;
		bool _compatibility_fallback_restart_pending				 = false;
		bool _compatibility_reprobe_on_next_reconnect				 = false;
		ov::String _preferred_playlist_reprobe_file_name;
		uint32_t _current_generation_first_request_id	 = 1;
		uint64_t _connection_generation					 = 0;
		InventorySnapshotState _inventory_snapshot_state = InventorySnapshotState::UNKNOWN;
		SharedRuntimeCapabilityState _shared_runtime_capability_state;
		CurrentUpstreamSubscriptionState _current_upstream_subscription_state;
		SharedRequestState _shared_request_state;
		StartStreamInvoker _start_stream_invoker_for_test;
		std::shared_ptr<mon::StreamMetrics> _stream_metrics;
	};
}
