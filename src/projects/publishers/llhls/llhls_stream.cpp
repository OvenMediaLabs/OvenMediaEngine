//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#include "llhls_stream.h"

#include <base/ovlibrary/hex.h>
#include <base/publisher/application.h>
#include <base/publisher/stream.h>
#include <config/config_manager.h>

#include <pugixml-1.9/src/pugixml.hpp>

#include <base/modules/data_format/cue_event/cue_event.h>

#include <pugixml-1.9/src/pugixml.hpp>

#include "llhls_application.h"
#include "llhls_private.h"
#include "llhls_session.h"

std::shared_ptr<LLHlsStream> LLHlsStream::Create(const std::shared_ptr<pub::Application> application, const info::Stream &info, bool origin_mode, uint32_t worker_count)
{
	auto stream = std::make_shared<LLHlsStream>(application, info, origin_mode, worker_count);
	return stream;
}

LLHlsStream::LLHlsStream(const std::shared_ptr<pub::Application> application, const info::Stream &info, bool origin_mode, uint32_t worker_count)
	: Stream(application, info), _origin_mode(origin_mode), _worker_count(worker_count)
{
}

LLHlsStream::~LLHlsStream()
{
	logtt("LLHlsStream(%s/%s) has been terminated finally", GetApplicationName(), GetName().CStr());
}

ov::String LLHlsStream::GetStreamId() const
{
	return ov::String::FormatString("llhls/%s", GetUri().CStr());
}

bool LLHlsStream::CreateOriginSessionPool()
{
	if (_origin_mode == false)
	{
		return false;
	}

	size_t max_pool_size = _worker_count == 0 ? 1 : _worker_count;

	// Create sessions up to _worker_count
	for (size_t i = 0; i < max_pool_size; i++)
	{
		auto session = LLHlsSession::Create(static_cast<session_id_t>(i),
											_origin_mode,
											"",
											GetApplication(),
											pub::Stream::GetSharedPtr(),
											0);
		logtd("LLHlsStream(%s/%s) - Pre-created origin mode session in pool, session id: %zu", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), i);
		AddSession(session);
	}

	return true;
}

std::shared_ptr<LLHlsSession> LLHlsStream::GetSessionFromPool()
{
	// Max session pool size if _worker_count
	size_t max_pool_size = _worker_count == 0 ? 1 : _worker_count;
	
	// Get random index
	size_t index = ov::Random::GenerateUInt32() % max_pool_size;

	auto session = GetSession(static_cast<session_id_t>(index));
	if (session == nullptr)
	{
		return nullptr;
	}

	return std::static_pointer_cast<LLHlsSession>(session);
}


std::shared_ptr<const pub::Stream::DefaultPlaylistInfo> LLHlsStream::GetDefaultPlaylistInfo() const
{
	static auto info = []() -> std::shared_ptr<const pub::Stream::DefaultPlaylistInfo> {
		ov::String file_name = "llhls.m3u8";
		auto file_name_without_ext = file_name.Substring(0, file_name.IndexOfRev('.'));

		return std::make_shared<const pub::Stream::DefaultPlaylistInfo>(
			"llhls_default",
			file_name_without_ext,
			file_name);
	}();

	return info;
}

bool LLHlsStream::Start()
{
	if (GetState() != State::CREATED)
	{
		return false;
	}

	if (CreateStreamWorker(_worker_count) == false)
	{
		return false;
	}

	if (_origin_mode == true)
	{
		if (CreateOriginSessionPool() == false)
		{
			logte("LLHlsStream(%s/%s) - Failed to create origin session pool", GetApplication()->GetVHostAppName().CStr(), GetName().CStr());
			return false;
		}
	}

	auto config = GetApplication()->GetConfig();

	auto llhls_config = config.GetPublishers().GetLLHlsPublisher();
	auto dump_config = llhls_config.GetDumps();
	auto dvr_config = llhls_config.GetDvr();

	if (llhls_config.IsOriginMode())
	{
		_stream_key = ov::String::FormatString("%zu", GetName().Hash());
	}
	else
	{
		_stream_key = ov::Random::GenerateString(8);
	}

	auto drm_config = llhls_config.GetDrm();
	if (drm_config.IsEnabled())
	{
		GetDrmInfo(drm_config.GetDrmInfoPath(), _cenc_property);
	}

	_packager_config.chunk_duration_ms = llhls_config.GetChunkDuration() * 1000.0;
	_packager_config.segment_duration_ms = llhls_config.GetSegmentDuration() * 1000.0;
	// cenc property will be set in AddPackager

	_storage_config.max_segments = llhls_config.GetSegmentCount();
	if (_storage_config.max_segments < 3)
	{
		logtw("LLHlsStream(%s/%s) - Segment count should be at least 3, Set to 3", GetApplication()->GetVHostAppName().CStr(), GetName().CStr());
		_storage_config.max_segments = 3;
	}
	_storage_config.segment_duration_ms = llhls_config.GetSegmentDuration() * 1000;
	_storage_config.dvr_enabled = dvr_config.IsEnabled();
	_storage_config.dvr_storage_path = dvr_config.GetTempStoragePath();
	_storage_config.dvr_duration_sec = dvr_config.GetMaxDuration();
	_storage_config.server_time_based_segment_numbering = llhls_config.IsServerTimeBasedSegmentNumbering();

	_configured_part_hold_back = llhls_config.GetPartHoldBack();
	_preload_hint_enabled = llhls_config.IsPreloadHintEnabled();

	// Find data track
	auto data_track = GetFirstTrackByType(cmn::MediaType::Data);

	std::shared_ptr<MediaTrack> first_video_track = nullptr, first_audio_track = nullptr;
	first_video_track = GetFirstTrackByType(cmn::MediaType::Video);
    first_audio_track = GetFirstTrackByType(cmn::MediaType::Audio);

	_vtt_reference_track_id = first_video_track ? first_video_track->GetId() : first_audio_track ? first_audio_track->GetId() : -1;
	for (const auto &[id, track] : _tracks)
	{
		if (IsSupportedMediaCodec(track->GetCodecId()) == true)
		{
			if (AddPackager(track, data_track) == false)
			{
				logte("LLHlsStream(%s/%s) - Failed to add packager for track(%u)", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), track->GetId());
				return false;
			}
		}
		else
		{
			if (track == data_track)
			{
				continue;
			}

			logti("LLHlsStream(%s/%s) - Ignore unsupported codec(%s)", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), cmn::GetCodecIdString(track->GetCodecId()));
			continue;
		}
	}

	// Set renditions to each chunklist writer
	{
		std::lock_guard<std::shared_mutex> lock(_chunklist_map_lock);
		for (auto &it : _chunklist_map)
		{
			auto chunklist_writer = it.second;
			chunklist_writer->SetRenditions(_chunklist_map);
		}
	}

	if (first_video_track == nullptr && first_audio_track == nullptr)
	{
		logtw("LLHLS stream [%s/%s] could not be created because there is no supported codec.", GetApplication()->GetVHostAppName().CStr(), GetName().CStr());
		return false;
	}

	if (llhls_config.ShouldCreateDefaultPlaylist() == true)
	{
		// If there is no default playlist, make default playlist
		// Default playlist is consist of first compatible video and audio track among all tracks
		auto default_playlist_info = GetDefaultPlaylistInfo();
		OV_ASSERT2(default_playlist_info != nullptr);

		auto default_playlist = Stream::GetPlaylist(default_playlist_info->file_name);
		if (default_playlist == nullptr)
		{
			auto playlist = std::make_shared<info::Playlist>(default_playlist_info->name, default_playlist_info->file_name, true);
			auto rendition = std::make_shared<info::Rendition>("default", first_video_track ? first_video_track->GetVariantName() : "", first_audio_track ? first_audio_track->GetVariantName() : "");

			playlist->AddRendition(rendition);

			AddPlaylist(playlist);

			auto master_playlist = CreateMasterPlaylist(playlist);

			std::lock_guard<std::mutex> guard(_master_playlists_lock);
			_master_playlists[default_playlist_info->internal_file_name] = master_playlist;
		}
	}
	else
	{
		logti("LLHLS stream [%s/%s] - Default playlist creation is disabled.", GetApplication()->GetVHostAppName().CStr(), GetName().CStr());
		if (GetPlaylists().size() == 0)
		{
			logtw("LLHLS stream [%s/%s] - There is no playlist, LLHLS will not work for this stream", GetApplication()->GetVHostAppName().CStr(), GetName().CStr());
			Stop();	 // Release all resources
			return false;
		}
	}

	// Select the dump setting for this stream.
	std::lock_guard<std::shared_mutex> lock(_dumps_lock);
	for (auto dump : dump_config.GetDumps())
	{
		if (dump.IsEnabled() == false)
		{
			continue;
		}

		// check if dump.TargetStreamName is same as this stream name
		auto match_result = dump.GetTargetStreamNameRegex().Matches(GetName().CStr());
		if (match_result.IsMatched())
		{
			auto output_path = dump.GetOutputPath();
			dump.ConfigureOutputPath(output_path, GetApplication()->GetVHostAppName().GetVHostName(), GetApplication()->GetVHostAppName().GetAppName(), GetName());

			auto dump_item = std::make_shared<mdl::Dump>();
			dump_item->SetId(dump.GetId());
			dump_item->SetOutputPath(output_path);
			dump_item->SetPlaylists(dump.GetPlaylists());
			dump_item->SetEnabled(true);

			_dumps.emplace(dump_item->GetId(), dump_item);
		}
	}

	logti("LLHlsStream has been created : %s/%u\nOriginMode(%s) Chunk Duration(%.2f) Segment Duration(%.2f) Segment Count(%u) DRM(%s)", GetName().CStr(), GetId(),
		  ov::Converter::ToString(llhls_config.IsOriginMode()).CStr(), llhls_config.GetChunkDuration(), llhls_config.GetSegmentDuration(), llhls_config.GetSegmentCount(), bmff::CencProtectSchemeToString(_cenc_property.scheme));

	return Stream::Start();
}

bool LLHlsStream::Stop()
{
	logtt("LLHlsStream(%s) has been stopped", GetName().CStr());

	{
		std::scoped_lock lock{_packager_map_lock, _storage_map_lock, _chunklist_map_lock, _master_playlists_lock, _dumps_lock};

		// clear all packagers
		_packager_map.clear();

		// clear all storages
		_storage_map.clear();

		// clear all playlist
		for (auto &it : _chunklist_map)
		{
			auto chunklist_writer = it.second;
			chunklist_writer->Release();
		}

		_chunklist_map.clear();

		// complete all dumps
		for (auto &it : _dumps)
		{
			auto dump = it.second;
			dump->SetEnabled(false);
			dump->CompleteDump();
		}
	}

	return Stream::Stop();
}

std::tuple<bool, ov::String> LLHlsStream::ConcludeLive()
{
	std::unique_lock<std::shared_mutex> lock(_concluded_lock);
	if (_concluded)
	{
		return {false, "Already concluded"};
	}

	_concluded = true;

	// Flush all packagers
	for (auto &it : _packager_map)
	{
		auto packager = it.second;
		packager->Flush();
	}

	// Append #EXT-X-ENDLIST all chunklists
	for (auto &it : _chunklist_map)
	{
		auto chunklist_writer = it.second;
		chunklist_writer->SetEndList();
	}

	return {true, ""};
}

bool LLHlsStream::IsConcluded() const
{
	std::shared_lock<std::shared_mutex> lock(_concluded_lock);
	return _concluded;
}

bool LLHlsStream::GetDrmInfo(const ov::String &file_path, bmff::CencProperty &cenc_property)
{
	ov::String final_path = ov::GetFilePath(file_path, cfg::ConfigManager::GetInstance()->GetConfigPath());

	pugi::xml_document xml_doc;
	auto load_result = xml_doc.load_file(final_path.CStr());
	if (load_result == false)
	{
		logte("LLHlsStream(%s/%s) - Failed to load DRM info file(%s) status(%d) description(%s)", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), final_path.CStr(), load_result.status, load_result.description());
		return false;
	}

	auto root_node = xml_doc.child("DRMInfo");
	if (root_node.empty())
	{
		logte("LLHlsStream(%s/%s) - Failed to load DRM info file(%s) because root node is not found", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), final_path.CStr());
		return false;
	}

	bool has_fairplay_pssh_box = false;

	for (pugi::xml_node drm_node = root_node.child("DRM"); drm_node; drm_node = drm_node.next_sibling("DRM"))
	{
		ov::String name = drm_node.child_value("Name");
		ov::String virtual_host_name = drm_node.child_value("VirtualHostName");
		ov::String app_name = drm_node.child_value("ApplicationName");
		ov::String stream_name = drm_node.child_value("StreamName");

		// stream_name can be regex
		ov::Regex _target_stream_name_regex = ov::Regex::CompiledRegex(ov::Regex::WildCardRegex(stream_name));
		auto match_result = _target_stream_name_regex.Matches(GetName().CStr());

		if (virtual_host_name == GetApplication()->GetVHostAppName().GetVHostName() &&
			app_name == GetApplication()->GetVHostAppName().GetAppName() &&
			match_result.IsMatched())
		{
			ov::String drm_provider = drm_node.child_value("DRMProvider");

			if (drm_provider.IsEmpty() || drm_provider.LowerCaseString() == "manual")
			{
				ov::String cenc_protect_scheme = drm_node.child_value("CencProtectScheme");
				ov::String key_id = drm_node.child_value("KeyId");
				ov::String key = drm_node.child_value("Key");
				ov::String iv = drm_node.child_value("Iv");
				ov::String fairplay_key_url = drm_node.child_value("FairPlayKeyUrl");
				ov::String keyformat = drm_node.child_value("Keyformat");

				// required
				if (cenc_protect_scheme.IsEmpty() || key_id.IsEmpty() || key.IsEmpty() || iv.IsEmpty())
				{
					logte("LLHlsStream(%s/%s) - Failed to load DRM info file(%s) because required field is empty", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), final_path.CStr());
					return false;
				}

				if (cenc_protect_scheme == "cbcs")
				{
					cenc_property.scheme = bmff::CencProtectScheme::Cbcs;
				}
				else if (cenc_protect_scheme == "cenc")
				{
					cenc_property.scheme = bmff::CencProtectScheme::Cenc;
				}
				else
				{
					logte("LLHlsStream(%s/%s) - Failed to load DRM info file(%s) because CencProtectScheme(%s) is not supported", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), final_path.CStr(), cenc_protect_scheme.CStr());
				}

				cenc_property.key_id = ov::Hex::Decode(key_id);
				if (cenc_property.key_id == nullptr || cenc_property.key_id->GetLength() != 16)
				{
					logte("LLHlsStream(%s/%s) - Failed to load DRM info file(%s) because KeyId(%s) is invalid (must be 16 bytes HEX foramt)", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), final_path.CStr(), key_id.CStr());
					cenc_property.scheme = bmff::CencProtectScheme::None;
					return false;
				}

				cenc_property.key = ov::Hex::Decode(key);
				if (cenc_property.key == nullptr || cenc_property.key->GetLength() != 16)
				{
					logte("LLHlsStream(%s/%s) - Failed to load DRM info file(%s) because Key(%s) is invalid (must be 16 bytes HEX format)", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), final_path.CStr(), key.CStr());
					cenc_property.scheme = bmff::CencProtectScheme::None;
					return false;
				}

				cenc_property.iv = ov::Hex::Decode(iv);
				if (cenc_property.iv == nullptr || cenc_property.iv->GetLength() != 16)
				{
					logte("LLHlsStream(%s/%s) - Failed to load DRM info file(%s) because Iv(%s) is invalid (must be 16 bytes HEX format)", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), final_path.CStr(), iv.CStr());
					cenc_property.scheme = bmff::CencProtectScheme::None;
					return false;
				}

				pugi::xml_node pssh_node = drm_node.child("Pssh");
				while (pssh_node)
				{
					auto pssh_box_data = ov::Hex::Decode(pssh_node.child_value());
					if (pssh_box_data == nullptr)
					{
						logte("LLHlsStream(%s/%s) - Failed to load DRM info file(%s) because Pssh(%s) is invalid (must be HEX format)", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), final_path.CStr(), pssh_node.child_value());
						cenc_property.scheme = bmff::CencProtectScheme::None;
						return false;
					}

					auto pssh_box = bmff::PsshBox(pssh_box_data);
					cenc_property.pssh_box_list.push_back(pssh_box);

					if (pssh_box.drm_system == bmff::DRMSystem::FairPlay)
					{
						has_fairplay_pssh_box = true;
					}

					pssh_node = pssh_node.next_sibling("Pssh");
				}

				cenc_property.fairplay_key_uri = fairplay_key_url;
				cenc_property.keyformat = keyformat;
			}
			else
			{
				logte("LLHlsStream(%s/%s) - Failed to load DRM info file(%s) because DRMProvider(%s) is not supported", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), final_path.CStr(), drm_provider.CStr());
				return false;
			}

			// Just first DRM info matched is enough for one stream
			break;
		}
	}

	// If cenc_property has fairplay_key_uri, but there is no pssh box for fairplay, add it.
	// default pssh box
	if (cenc_property.fairplay_key_uri.IsEmpty() == false && has_fairplay_pssh_box == false)
	{
		cenc_property.pssh_box_list.push_back(bmff::PsshBox("94ce86fb-07ff-4f43-adb8-93d2fa968ca2", {cenc_property.key_id}, nullptr));
	}

	// Set profiles
	if (cenc_property.scheme == bmff::CencProtectScheme::Cenc)
	{
		cenc_property.crypt_bytes_block = 0;
		cenc_property.skip_bytes_block = 0;
		cenc_property.per_sample_iv_size = 16;
	}
	else if (cenc_property.scheme == bmff::CencProtectScheme::Cbcs)
	{
		cenc_property.crypt_bytes_block = 1;
		cenc_property.skip_bytes_block = 9;
		cenc_property.per_sample_iv_size = 0;
	}

	return true;
}

bool LLHlsStream::IsSupportedMediaCodec(cmn::MediaCodecId codec_id) const
{
	switch (codec_id)
	{
		case cmn::MediaCodecId::H264:
		case cmn::MediaCodecId::H265:
		case cmn::MediaCodecId::Aac:
		case cmn::MediaCodecId::WebVTT:
			return true;
		default:
			return false;
	}

	return false;
}

const ov::String &LLHlsStream::GetStreamKey() const
{
	return _stream_key;
}

std::shared_ptr<LLHlsMasterPlaylist> LLHlsStream::CreateMasterPlaylist(const std::shared_ptr<const info::Playlist> &playlist) const
{
	auto master_playlist = std::make_shared<LLHlsMasterPlaylist>();

	ov::String chunk_path;
	ov::String app_name = GetApplicationInfo().GetVHostAppName().GetAppName();
	ov::String stream_name = GetName();
	switch (playlist->GetHlsChunklistPathDepth())
	{
		case 0:
			chunk_path = "";
			break;
		case 1:
			chunk_path = ov::String::FormatString("../%s/", stream_name.CStr());
			break;
		case 2:
			chunk_path = ov::String::FormatString("../../%s/%s/", app_name.CStr(), stream_name.CStr());
			break;
		case -1:
		default:
			chunk_path = ov::String::FormatString("/%s/%s/", app_name.CStr(), stream_name.CStr());
			break;
	}

	// default options
	auto llhls_conf = GetApplication()->GetConfig().GetPublishers().GetLLHlsPublisher();
	auto default_query_value_hls_legacy = llhls_conf.GetDefaultQueryString().GetBoolValue("_HLS_legacy", kDefaultHlsLegacy);
	auto default_query_value_hls_rewind = llhls_conf.GetDefaultQueryString().GetBoolValue("_HLS_rewind", kDefaultHlsRewind);

	master_playlist->SetDefaultOptions(default_query_value_hls_legacy, default_query_value_hls_rewind);
	master_playlist->SetChunkPath(chunk_path);
	master_playlist->SetCencProperty(_cenc_property);

	// Add all media candidates to master playlist
	for (const auto &[track_id, track_group] : GetMediaTrackGroups())
	{
		auto track = track_group->GetFirstTrack();
		if (track == nullptr)
		{
			continue;
		}

		if (IsSupportedMediaCodec(track->GetCodecId()) == false)
		{
			continue;
		}

		master_playlist->AddMediaCandidateGroup(track_group, [=](const std::shared_ptr<const MediaTrack> &track) -> ov::String {
			return GetChunklistName(track->GetId());
		});
	}

	// Add stream
	for (const auto &rendition : playlist->GetRenditionList())
	{
		auto video_index_hint = rendition->GetVideoIndexHint();
		if (video_index_hint < 0)
		{
			video_index_hint = 0;
		}
		auto video_track = GetFirstTrackByVariant(rendition->GetVideoVariantName());

		// LLHLS Audio does not use audio_index_hint because it has multilingual support
		auto audio_track = GetFirstTrackByVariant(rendition->GetAudioVariantName());

		if ((video_track != nullptr && IsSupportedMediaCodec(video_track->GetCodecId()) == false) ||
			(audio_track != nullptr && IsSupportedMediaCodec(audio_track->GetCodecId()) == false))
		{
			logtw("LLHlsStream(%s/%s) - Exclude the rendition(%s) from the %s.m3u8 due to unsupported codec", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(),
				  rendition->GetName().CStr(), playlist->GetFileName().CStr());
			continue;
		}

		// If there is no media track, it is not added to the master playlist
		ov::String video_variant_name = video_track != nullptr ? video_track->GetVariantName() : "";
		ov::String audio_variant_name = audio_track != nullptr ? audio_track->GetVariantName() : "";
		ov::String vtt_variant_name;
		
		if (IsVttEnabled() && playlist->IsSubtitlesEnabled())
		{
			vtt_variant_name = kSubtitleTrackVariantName;
		}

		if (rendition->GetVideoVariantName().IsEmpty() == false && video_track == nullptr)
		{
			logtw("LLHlsStream(%s/%s) - %s video is excluded from the %s rendition in %s playlist because there is no video track.", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), rendition->GetVideoVariantName().CStr(), rendition->GetName().CStr(), playlist->GetFileName().CStr());
		}

		if (rendition->GetAudioVariantName().IsEmpty() == false && audio_track == nullptr)
		{
			logtw("LLHlsStream(%s/%s) - %s audio is excluded from the %s rendition in %s playlist because there is no audio track.", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), rendition->GetAudioVariantName().CStr(), rendition->GetName().CStr(), playlist->GetFileName().CStr());
		}

		master_playlist->AddStreamInfo(video_variant_name, video_index_hint, audio_variant_name, vtt_variant_name);
	}

	master_playlist->UpdateCacheForDefaultPlaylist();

	return master_playlist;
}

void LLHlsStream::DumpMasterPlaylistsOfAllItems()
{
	// lock
	std::shared_lock<std::shared_mutex> lock(_dumps_lock);
	for (auto &it : _dumps)
	{
		auto dump = it.second;
		if (dump->IsEnabled() == false)
		{
			continue;
		}

		if (DumpMasterPlaylist(dump) == false)
		{
			// Event if the dump fails, it will not be deleted
			//dump->SetEnabled(false);
		}
	}
}

bool LLHlsStream::DumpMasterPlaylist(const std::shared_ptr<mdl::Dump> &item)
{
	if (item->IsEnabled() == false)
	{
		return false;
	}

	for (auto &playlist : item->GetPlaylists())
	{
		auto [result, data] = GetMasterPlaylist(playlist, "", false, false, true, false);
		if (result != RequestResult::Success)
		{
			logtw("Could not get master playlist(%s) for dump", playlist.CStr());
			return false;
		}

		if (DumpData(item, playlist, data) == false)
		{
			logtw("Could not dump master playlist(%s)", playlist.CStr());
			return false;
		}
	}

	return true;
}

void LLHlsStream::DumpInitSegmentOfAllItems(const int32_t &track_id)
{
	std::shared_lock<std::shared_mutex> lock(_dumps_lock);
	for (auto &it : _dumps)
	{
		auto dump = it.second;
		if (dump->IsEnabled() == false)
		{
			continue;
		}

		if (DumpInitSegment(dump, track_id) == false)
		{
			dump->SetEnabled(false);
		}
	}
}

bool LLHlsStream::DumpInitSegment(const std::shared_ptr<mdl::Dump> &item, const int32_t &track_id)
{
	if (item->IsEnabled() == false)
	{
		logtw("Dump(%s) is disabled", item->GetId().CStr());
		return false;
	}

	// Get segment
	auto [result, data] = GetInitializationSegment(track_id);
	if (result != RequestResult::Success)
	{
		logtw("Could not get init segment for dump");
		return false;
	}

	auto init_segment_name = GetInitializationSegmentName(track_id);

	return DumpData(item, init_segment_name, data);
}

void LLHlsStream::DumpSegmentOfAllItems(const int32_t &track_id, const uint32_t &segment_number)
{
	std::shared_lock<std::shared_mutex> lock(_dumps_lock);
	for (auto &it : _dumps)
	{
		auto dump = it.second;
		if (dump->IsEnabled() == false)
		{
			continue;
		}

		if (DumpSegment(dump, track_id, segment_number) == false)
		{
			dump->SetEnabled(false);
			continue;
		}
	}
}

bool LLHlsStream::DumpSegment(const std::shared_ptr<mdl::Dump> &item, const int32_t &track_id, const int64_t &segment_number)
{
	if (item->IsEnabled() == false)
	{
		return false;
	}

	if (item->HasExtraData(track_id) == false)
	{
		item->SetExtraData(track_id, segment_number);
	}

	auto storage = GetStorage(track_id);
	if (storage == nullptr)
	{
		logtw("Could not find storage for track(%s/%d)", GetName().CStr(), track_id);
		return false;
	}

	// Get segment
	auto segment = storage->GetSegment(segment_number);
	if (segment == nullptr)
	{
		logtw("Could not get segment(%" PRId64 ") for dump", segment_number);
		return false;
	}

	auto segment_data = segment->GetData();

	// Get updated chunklist
	auto chunklist = GetChunklistWriter(track_id);
	if (chunklist == nullptr)
	{
		logtw("Could not find chunklist for track_id = %d", track_id);
		return false;
	}

	auto chunklist_data = chunklist->ToString("", false, true, false, true, item->GetFirstSegmentNumber(track_id)).ToData(false);

	auto segment_file_name = GetSegmentName(track_id, segment_number);
	auto chunklist_file_name = GetChunklistName(track_id);

	if (DumpData(item, segment_file_name, segment_data) == false)
	{
		logtw("Could not dump segment(%s)", segment_file_name.CStr());
		return false;
	}

	if (DumpData(item, chunklist_file_name, chunklist_data) == false)
	{
		logtw("Could not dump chunklist(%s)", chunklist_file_name.CStr());
		return false;
	}

	chunklist->SaveOldSegmentInfo(true);

	return true;
}

bool LLHlsStream::DumpData(const std::shared_ptr<mdl::Dump> &item, const ov::String &file_name, const std::shared_ptr<const ov::Data> &data)
{
	return item->DumpData(file_name, data);
}

std::tuple<LLHlsStream::RequestResult, std::shared_ptr<const ov::Data>> LLHlsStream::GetMasterPlaylist(const ov::String &file_name, const ov::String &chunk_query_string, bool gzip, bool legacy, bool rewind, bool include_path)
{
	if (GetState() != State::STARTED)
	{
		return {RequestResult::NotFound, nullptr};
	}

	if (IsReadyToPlay() == false)
	{
		return {RequestResult::Accepted, nullptr};
	}

	std::shared_ptr<LLHlsMasterPlaylist> master_playlist = nullptr;

	// _master_playlists_lock
	std::unique_lock<std::mutex> guard(_master_playlists_lock);
	auto it = _master_playlists.find(file_name);
	if (it == _master_playlists.end())
	{
		auto file_name_without_ext = file_name.Substring(0, file_name.IndexOfRev('.'));

		// Create master playlist
		auto playlist = GetPlaylist(file_name_without_ext);
		if (playlist == nullptr)
		{
			return {RequestResult::NotFound, nullptr};
		}

		master_playlist = CreateMasterPlaylist(playlist);

		// Cache
		_master_playlists[file_name] = master_playlist;
	}
	else
	{
		master_playlist = it->second;
	}
	guard.unlock();

	if (master_playlist == nullptr)
	{
		return {RequestResult::NotFound, nullptr};
	}

	if (gzip == true)
	{
		return {RequestResult::Success, master_playlist->ToGzipData(chunk_query_string, legacy, rewind)};
	}

	return {RequestResult::Success, master_playlist->ToString(chunk_query_string, legacy, rewind, include_path).ToData(false)};
}

std::tuple<LLHlsStream::RequestResult, std::shared_ptr<const ov::Data>> LLHlsStream::GetChunklist(const ov::String &query_string, const int32_t &track_id, int64_t msn, int64_t psn, bool skip, bool gzip, bool legacy, bool rewind) const
{
	auto chunklist = GetChunklistWriter(track_id);
	if (chunklist == nullptr)
	{
		logtw("Could not find chunklist for track_id = %d", track_id);
		return {RequestResult::NotFound, nullptr};
	}

	if (IsReadyToPlay() == false)
	{
		return {RequestResult::Accepted, nullptr};
	}

	if (msn >= 0 && psn >= 0)
	{
		int64_t last_msn, last_psn;
		if (chunklist->GetLastSequenceNumber(last_msn, last_psn) == false)
		{
			logtw("Could not get last sequence number for track_id = %d", track_id);
			return {RequestResult::NotFound, nullptr};
		}

		if (msn > last_msn || (msn >= last_msn && psn > last_psn))
		{
			// Hold the request until a Playlist contains a Segment with the requested Sequence Number
			logtt("Accepted chunklist for track_id = %d, msn = %ld, psn = %ld (last_msn = %ld, last_psn = %ld)", track_id, msn, psn, last_msn, last_psn);
			return {RequestResult::Accepted, nullptr};
		}
		else
		{
			logtt("Get chunklist for track_id = %d, msn = %ld, psn = %ld (last_msn = %ld, last_psn = %ld)", track_id, msn, psn, last_msn, last_psn);
		}
	}

	if (gzip == true)
	{
		return {RequestResult::Success, chunklist->ToGzipData(query_string, skip, legacy, rewind)};
	}

	return {RequestResult::Success, chunklist->ToString(query_string, skip, legacy, rewind).ToData(false)};
}

std::tuple<LLHlsStream::RequestResult, std::shared_ptr<ov::Data>> LLHlsStream::GetInitializationSegment(const int32_t &track_id) const
{
	auto storage = GetStorage(track_id);
	if (storage == nullptr)
	{
		logtw("Could not find storage for track_id = %d", track_id);
		return {RequestResult::NotFound, nullptr};
	}

	return {RequestResult::Success, storage->GetInitializationSection()};
}

std::tuple<LLHlsStream::RequestResult, std::shared_ptr<ov::Data>> LLHlsStream::GetSegment(const int32_t &track_id, const int64_t &segment_number) const
{
	auto storage = GetStorage(track_id);
	if (storage == nullptr)
	{
		logtw("Could not find storage for track_id = %d", track_id);
		return {RequestResult::NotFound, nullptr};
	}

	auto segment = storage->GetSegment(segment_number);
	if (segment == nullptr)
	{
		logtw("Could not find segment for track_id = %d, segment = %ld (last_segment = %ld)", track_id, segment_number, storage->GetLastSegmentNumber());
		return {RequestResult::NotFound, nullptr};
	}

	return {RequestResult::Success, segment->GetData()};
}

std::tuple<LLHlsStream::RequestResult, std::shared_ptr<ov::Data>> LLHlsStream::GetPartial(const int32_t &track_id, const int64_t &segment_number, const int64_t &partial_number) const
{
	logtt("LLHlsStream(%s) - GetChunk(%d, %ld, %ld)", GetName().CStr(), track_id, segment_number, partial_number);

	auto storage = GetStorage(track_id);
	if (storage == nullptr)
	{
		logtw("Could not find storage for track_id = %d", track_id);
		return {RequestResult::NotFound, nullptr};
	}

	auto [last_segment_number, last_partial_number] = storage->GetLastPartialSegmentNumber();

	if ((segment_number > last_segment_number) || (segment_number == last_segment_number && partial_number > last_partial_number))
	{
		logtt("Accepted chunk for track_id = %d, segment = %ld, chunk = %ld (last_segment = %ld, last_chunk = %ld)", track_id, segment_number, partial_number, last_segment_number, last_partial_number);
		// Hold the request until a Playlist contains a Segment with the requested Sequence Number
		return {RequestResult::Accepted, nullptr};
	}
	else
	{
		logtt("Get chunk for track_id = %d, segment = %ld, chunk = %ld (last_segment = %ld, last_chunk = %ld)", track_id, segment_number, partial_number, last_segment_number, last_partial_number);
	}

	auto partial = storage->GetPartialSegment(segment_number, partial_number);
	if (partial == nullptr)
	{
		logtw("Could not find partial segment for track_id = %d, segment = %ld, partial = %ld (last_segment = %ld, last_partial = %ld)", track_id, segment_number, partial_number, last_segment_number, last_partial_number);
		return {RequestResult::NotFound, nullptr};
	}

	return {RequestResult::Success, partial->GetData()};
}

void LLHlsStream::BufferMediaPacketUntilReadyToPlay(const std::shared_ptr<MediaPacket> &media_packet)
{
	if (_initial_media_packet_buffer.Size() >= MAX_INITIAL_MEDIA_PACKET_BUFFER_SIZE)
	{
		// Drop the oldest packet, for OOM protection
		_initial_media_packet_buffer.Dequeue(0);
	}

	_initial_media_packet_buffer.Enqueue(media_packet);
}

bool LLHlsStream::SendBufferedPackets()
{
	logtt("SendBufferedPackets - BufferSize (%zu)", _initial_media_packet_buffer.Size());
	while (_initial_media_packet_buffer.IsEmpty() == false)
	{
		auto buffered_media_packet = _initial_media_packet_buffer.Dequeue();
		if (buffered_media_packet.has_value() == false)
		{
			continue;
		}

		auto media_packet = buffered_media_packet.value();
		if (media_packet->GetMediaType() == cmn::MediaType::Data)
		{
			SendDataFrame(media_packet);
		}
		else
		{
			AppendMediaPacket(media_packet);
		}
	}

	return true;
}

void LLHlsStream::SendVideoFrame(const std::shared_ptr<MediaPacket> &media_packet)
{
	// If the stream is concluded, it will not be processed.
	if (IsConcluded() == true)
	{
		return;
	}

	if (media_packet == nullptr || media_packet->GetData() == nullptr)
	{
		return;
	}

	if (GetState() == State::CREATED)
	{
		BufferMediaPacketUntilReadyToPlay(media_packet);
		return;
	}

	if (_initial_media_packet_buffer.IsEmpty() == false)
	{
		SendBufferedPackets();
	}

	AppendMediaPacket(media_packet);
}

void LLHlsStream::SendAudioFrame(const std::shared_ptr<MediaPacket> &media_packet)
{
	// If the stream is concluded, it will not be processed.
	if (IsConcluded() == true)
	{
		return;
	}

	if (media_packet == nullptr || media_packet->GetData() == nullptr)
	{
		return;
	}

	if (GetState() == State::CREATED)
	{
		BufferMediaPacketUntilReadyToPlay(media_packet);
		return;
	}

	if (_initial_media_packet_buffer.IsEmpty() == false)
	{
		SendBufferedPackets();
	}

	AppendMediaPacket(media_packet);
}

void LLHlsStream::SendDataFrame(const std::shared_ptr<MediaPacket> &media_packet)
{
	if (GetState() == State::CREATED)
	{
		BufferMediaPacketUntilReadyToPlay(media_packet);
		return;
	}

	auto data_track = GetTrack(media_packet->GetTrackId());
	if (data_track == nullptr)
	{
		logtw("Could not find track. id: %d", media_packet->GetTrackId());
		return;
	}

	if (_initial_media_packet_buffer.IsEmpty() == false)
	{
		SendBufferedPackets();
	}

	if (media_packet->GetBitstreamFormat() == cmn::BitstreamFormat::ID3v2)
	{
		auto target_media_type = (media_packet->GetPacketType() == cmn::PacketType::VIDEO_EVENT) ? cmn::MediaType::Video : cmn::MediaType::Audio;

		for (const auto &it : GetTracks())
		{
			auto track = it.second;
			if (media_packet->GetPacketType() != cmn::PacketType::EVENT && track->GetMediaType() != target_media_type)
			{
				continue;
			}

			// Get Packager
			auto packager = GetPackager(track->GetId());
			if (packager == nullptr)
			{
				logtt("Could not find packager. track id: %d", track->GetId());
				continue;
			}
			logtt("AppendSample : track(%u) length(%zu)", media_packet->GetTrackId(), media_packet->GetDataLength());

			packager->ReserveDataPacket(media_packet);
		}
	}
	else if (media_packet->GetBitstreamFormat() == cmn::BitstreamFormat::CUE || media_packet->GetBitstreamFormat() == cmn::BitstreamFormat::SCTE35)
	{
		// milliseconds scale
		auto timestamp_ms = static_cast<double>(media_packet->GetDts()) / data_track->GetTimeBase().GetTimescale() * 1000.0;
		std::shared_ptr<ov::Data> data = media_packet->GetData() != nullptr ? media_packet->GetData()->Clone() : nullptr;
		if (InsertMarkerToAllPackagers(media_packet->GetTrackId(), media_packet->GetBitstreamFormat(), timestamp_ms, data) == false)
		{
			logte("Failed to insert marker to all packagers (track_id: %u, bitstream_format: %d, timestamp: %" PRId64 ")", media_packet->GetTrackId(), ov::ToUnderlyingType(media_packet->GetBitstreamFormat()), media_packet->GetDts());
			return;
		}

		// Parse data
		if (media_packet->GetBitstreamFormat() == cmn::BitstreamFormat::CUE)
		{
			auto cue_event = CueEvent::Parse(media_packet->GetData());
			if (cue_event == nullptr)
			{
				logte("Failed to parse cue event");
				return;
			}

			if (cue_event->GetCueType() == CueEvent::CueType::OUT)
			{
				// Make CUE-IN event
				auto cue_out_duration_ms = cue_event->GetDurationMsec();
				auto cue_in_timestamp_ms = timestamp_ms + cue_out_duration_ms;
				auto cue_in_data = CueEvent::Create(CueEvent::CueType::IN)->Serialize();

				if (InsertMarkerToAllPackagers(media_packet->GetTrackId(), cmn::BitstreamFormat::CUE, cue_in_timestamp_ms, cue_in_data) == false)
				{
					logte("Failed to insert CUE-IN marker to all packagers (track_id: %u, timestamp: %f)", media_packet->GetTrackId(), cue_in_timestamp_ms);
					return;
				}
			}
		}
		else if (media_packet->GetBitstreamFormat() == cmn::BitstreamFormat::SCTE35)
		{
			auto scte35_event = Scte35Event::Parse(media_packet->GetData());
			if (scte35_event == nullptr)
			{
				logte("Failed to parse scte35 event (track_id: %u, timestamp: %" PRId64 ")", media_packet->GetTrackId(), media_packet->GetDts());
				return;
			}

			if (scte35_event->IsOutOfNetwork() == true)
			{
				// Make SCTE35-IN event
				auto scte_out_duration_ms = scte35_event->GetDurationMsec();
				auto scte_in_timestamp_ms = timestamp_ms + scte_out_duration_ms;
				auto scte_in_data = Scte35Event::Create(mpegts::SpliceCommandType::SPLICE_INSERT, scte35_event->GetID(), false, scte_in_timestamp_ms, scte_out_duration_ms, false)->Serialize();

				// xxx-OUT marker will create one more segment, so we need to shift the sequence number by 1
				if (InsertMarkerToAllPackagers(media_packet->GetTrackId(), cmn::BitstreamFormat::SCTE35, scte_in_timestamp_ms, scte_in_data) == false)
				{
					logte("Failed to insert SCTE35-IN marker to all packagers (track_id: %u, timestamp: %f)", media_packet->GetTrackId(), scte_in_timestamp_ms);
					return;
				}
			}
		}
	}
	else if (media_packet->GetBitstreamFormat() == cmn::BitstreamFormat::WebVTT)
	{
		// Convert DataFrame to WebVTT
		auto webvtt_frame = WebVTTFrame::Parse(media_packet->GetData());
		if (webvtt_frame == nullptr)
		{
			logte("Failed to parse WebVTT frame from data packet (track_id: %u, dts: %" PRId64 ")", media_packet->GetTrackId(), media_packet->GetDts());
			return;
		}

		// Get Packager
		auto packager = GetVttPackager(data_track->GetId());
		if (packager == nullptr)
		{
			logte("Could not find WebVTT packager for label: %s", webvtt_frame->GetLabel().CStr());
			return;
		}
		
		packager->AddFrame(webvtt_frame);
	}
}

std::tuple<bool, ov::String> LLHlsStream::CanInsertMarker(cmn::BitstreamFormat bitstream_format, int64_t timestamp_ms, const std::shared_ptr<ov::Data> &data) const
{
	auto data_track = GetFirstTrackByType(cmn::MediaType::Data);
	if (data_track == nullptr)
	{
		return {false, "Could not find data track"};
	}

	auto first_video_media_track = GetFirstTrackByType(cmn::MediaType::Video);
	auto first_video_packager = GetPackager(first_video_media_track->GetId());

	// Insert marker to all packagers
	for (const auto &it : GetTracks())
	{
		auto track = it.second;
		// Only video and audio tracks are supported
		if (track->GetMediaType() != cmn::MediaType::Video && track->GetMediaType() != cmn::MediaType::Audio)
		{
			continue;
		}

		// Get Packager
		auto packager = GetPackager(track->GetId());
		if (packager == nullptr)
		{
			logtt("Could not find packager. track id: %d", track->GetId());
			continue;
		}

		auto timestamp_media_scale = static_cast<double>(timestamp_ms) / data_track->GetTimeBase().GetTimescale() * track->GetTimeBase().GetTimescale();
		auto marker = Marker::CreateMarker(bitstream_format, timestamp_media_scale, timestamp_ms, data);
		if (marker == nullptr)
		{
			logte("(%s/%s) Failed to create the marker", GetApplication()->GetVHostAppName().CStr(), GetName().CStr());
			return {false, "Failed to create the marker"};
		}

		auto [result, message] = packager->CanInsertMarker(marker);
		if (result == false)
		{
			logte("Failed to insert marker (timestamp: %" PRId64 ", tag: %s)", marker->GetTimestamp(), marker->GetTag().CStr());
			return {false, message};
		}
	}

	return {true, ""};
}

bool LLHlsStream::InsertMarkerToAllPackagers(uint32_t data_track_id, cmn::BitstreamFormat bitstream_format, int64_t timestamp_ms, const std::shared_ptr<ov::Data> &data)
{
	auto data_track = GetTrack(data_track_id);
	if (data_track == nullptr)
	{
		logtw("Could not find track. id: %d", data_track_id);
		return false;
	}

	auto first_video_media_track = GetFirstTrackByType(cmn::MediaType::Video);
	auto first_video_packager = GetPackager(first_video_media_track->GetId());

	// Create marker
	int64_t estimated_seq = first_video_packager->GetEstimatedSequenceNumber(timestamp_ms);
	int64_t max_current_seq = 0;
	// 0: check if it can insert
	// 1: insert
	for (int i = 0; i < 2; i++)
	{
		if (i == 1)
		{
			logtd("InsertMarkerToAllPackagers - Estimated sequence number: %" PRId64 " Max current sequence number: %" PRId64 "", estimated_seq, max_current_seq);

			if (max_current_seq > estimated_seq)
			{
				logtw("Estimated sequence number is smaller than the current sequence number. estimated_seq: %" PRId64 ", max_current_seq: %" PRId64 "", estimated_seq, max_current_seq);
				estimated_seq = max_current_seq;
			}
		}

		// Insert marker to all packagers
		for (const auto &it : GetTracks())
		{
			auto track = it.second;
			// Only video and audio tracks are supported
			if (track->GetMediaType() != cmn::MediaType::Video && track->GetMediaType() != cmn::MediaType::Audio)
			{
				continue;
			}

			// Get Packager
			auto packager = GetPackager(track->GetId());
			if (packager == nullptr)
			{
				logtt("Could not find packager. track id: %d", track->GetId());
				continue;
			}

			auto timestamp_media_scale = static_cast<double>(timestamp_ms) / data_track->GetTimeBase().GetTimescale() * track->GetTimeBase().GetTimescale();
			auto marker = Marker::CreateMarker(bitstream_format, timestamp_media_scale, timestamp_ms, data);
			if (marker == nullptr)
			{
				logte("(%s/%s) Failed to create the marker", GetApplication()->GetVHostAppName().CStr(), GetName().CStr());
				return false;
			}

			if (i == 0)	 // check
			{
				max_current_seq = std::max(max_current_seq, packager->GetCurrentSequenceNumber());
				auto [result, msg] = packager->CanInsertMarker(marker);
				if (result == false)
				{
					logte("Failed to insert marker (timestamp: %" PRId64 ", tag: %s, msg: %s)", marker->GetTimestamp(), marker->GetTag().CStr(), msg.CStr());
					return false;
				}
			}
			else
			{
				logtd("Packager(%u) - Insert marker: %s Estimated sequence number: %" PRId64 "", track->GetId(), marker->GetTag().CStr(), estimated_seq);

				marker->SetDesiredSequenceNumber(estimated_seq);
				auto result = packager->InsertMarker(marker);
				if (result == false)
				{
					// We checked it can be inserted, so it should not fail
					logtc("Failed to insert marker (timestamp: %" PRId64 ", tag: %s)", marker->GetTimestamp(), marker->GetTag().CStr());
					return false;
				}
			}
		}
	}

	return true;
}

void LLHlsStream::OnEvent(const std::shared_ptr<MediaEvent> &event)
{
	if (event == nullptr)
	{
		return;
	}

	switch (event->GetCommandType())
	{
		case EventCommand::Type::ConcludeLive: {
			auto [result, message] = ConcludeLive();
			if (result == true)
			{
				logti("LLHlsStream(%s/%s) - Live has concluded.", GetApplication()->GetVHostAppName().CStr(), GetName().CStr());
			}
			else
			{
				logte("LLHlsStream(%s/%s) - Failed to conclude live(%s)", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), message.CStr());
			}
			break;
		}
		case EventCommand::Type::UpdateSubtitleLanguage: {
			std::unique_lock<std::mutex> guard(_master_playlists_lock);
			logti("LLHlsStream(%s/%s) - Clear master playlist cache for subtitle language update.", GetApplication()->GetVHostAppName().CStr(), GetName().CStr());
			_master_playlists.clear();
			guard.unlock();
			break;
		}
		default:
			break;
	}
}

bool LLHlsStream::AppendMediaPacket(const std::shared_ptr<MediaPacket> &media_packet)
{
	auto track = GetTrack(media_packet->GetTrackId());
	if (track == nullptr)
	{
		logtw("Could not find track. id: %d", media_packet->GetTrackId());
		return false;
	}

	if (IsSupportedMediaCodec(track->GetCodecId()) == false)
	{
		return true;
	}

	// Get Packager
	auto packager = GetPackager(track->GetId());
	if (packager == nullptr)
	{
		logtw("Could not find packager. track id: %d", track->GetId());
		return false;
	}

	logtt("AppendSample : track(%u) length(%zu)", media_packet->GetTrackId(), media_packet->GetDataLength());

	packager->AppendSample(media_packet);

	return true;
}

double LLHlsStream::ComputeOptimalPartDuration(const std::shared_ptr<const MediaTrack> &track) const
{
	auto part_target = _packager_config.chunk_duration_ms;
	double optimal_part_target = part_target;

	if (track->GetMediaType() == cmn::MediaType::Audio)
	{
		// Duration of a frame is 1024 samples / sample rate
		auto frame_duration = static_cast<double>(track->GetAudioSamplesPerFrame()) / static_cast<double>(track->GetSampleRate());
		auto frame_duration_ms = frame_duration * 1000.0;

		// Find the closest multiple of frame_duration_ms to part_target
		auto optimal_frame_count = std::round(part_target / frame_duration_ms);
		optimal_part_target = optimal_frame_count * frame_duration_ms;

		logti("LLHlsStream::ComputeOptimalPartDuration() - Audio track(%d) SampleRate(%d) frame_duration_ms(%f) optimal_frame_count(%f) part_target(%f) optimal_part_target(%f)", track->GetId(), track->GetSampleRate(), frame_duration_ms, optimal_frame_count, part_target, optimal_part_target);
	}
	else if (track->GetMediaType() == cmn::MediaType::Video)
	{
		// Duration of a frame is 1 / frame rate
		auto frame_duration = 1.0 / track->GetFrameRate();
		auto frame_duration_ms = frame_duration * 1000.0;

		// Find the closest multiple of frame_duration_ms to part_target
		auto optimal_frame_count = std::round(part_target / frame_duration_ms);
		optimal_part_target = optimal_frame_count * frame_duration_ms;

		logti("LLHlsStream::ComputeOptimalPartDuration() - Video track(%d) FrameRate(%f) frame_duration_ms(%f) optimal_frame_count(%f) part_target(%f) optimal_part_target(%f)", track->GetId(), track->GetFrameRate(), frame_duration_ms, optimal_frame_count, part_target, optimal_part_target);
	}

	return optimal_part_target;
}

// Create and Get fMP4 packager with track info, storage and packager_config
bool LLHlsStream::AddPackager(const std::shared_ptr<const MediaTrack> &media_track, const std::shared_ptr<const MediaTrack> &data_track)
{
	if (media_track->GetCodecId() == cmn::MediaCodecId::WebVTT)
	{
		if (AddVttPackager(media_track) == false)
		{
			logte("LLHlsStream(%s/%s) - Failed to add VTT packager for track(%u)", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), media_track->GetId());
			return false;
		}

		_vtt_enabled = true;
		return true;
	}

	auto packager_config = _packager_config;

	packager_config.chunk_duration_ms = std::round(ComputeOptimalPartDuration(media_track));

	logti("LLHlsStream::AddPackager() - Track(%d) ChunkDuration(%f)", media_track->GetId(), packager_config.chunk_duration_ms);

	auto cenc_property = _cenc_property;

	auto tag = ov::String::FormatString("%s/%s", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr());

	if (cenc_property.scheme != bmff::CencProtectScheme::None)
	{
		if (media_track->GetCodecId() == cmn::MediaCodecId::H264)
		{
			// Supported
		}
		else if (media_track->GetCodecId() == cmn::MediaCodecId::Aac)
		{
			// Supported
		}
		else
		{
			cenc_property.scheme = bmff::CencProtectScheme::None;
			// Not yet support for other codec
			logte("LLHlsStream::AddPackager() - CENC is not supported for this codec(%s), this track will be excluded from CENC protection", cmn::GetCodecIdString(media_track->GetCodecId()));
			_cenc_property.scheme = bmff::CencProtectScheme::None;
		}
	}

	// Create Storage
	auto storage = std::make_shared<bmff::FMP4Storage>(bmff::FMp4StorageObserver::GetSharedPtr(), media_track, _storage_config, tag);

	// Create fMP4 Packager
	packager_config.cenc_property = cenc_property;
	auto packager = std::make_shared<bmff::FMP4Packager>(storage, media_track, data_track, packager_config);

	// Create Initialization Segment
	if (packager->CreateInitializationSegment() == false)
	{
		logtc("LLHlsStream::AddPackager() - Failed to create initialization segment");
		return false;
	}

	// milliseconds to seconds
	auto segment_count = _storage_config.max_segments;

	// segment_duration used for mark X-TARGETDURATION, and must rounded to the nearest integer number of seconds.

	// Note that in protocol version 6, the semantics of the EXT-
	// X-TARGETDURATION tag changed slightly.  In protocol version 5 and
	// earlier it indicated the maximum segment duration; in protocol
	// version 6 and later it indicates the the maximum segment duration
	// rounded to the nearest integer number of seconds.

	auto segment_duration = std::round(static_cast<double>(_storage_config.segment_duration_ms) / 1000.0);
	auto chunk_duration = static_cast<double>(packager_config.chunk_duration_ms) / 1000.0;
	auto track_id = media_track->GetId();

	auto chunklist = std::make_shared<LLHlsChunklist>(GetChunklistName(track_id),
													  GetTrack(track_id),
													  segment_count,
													  segment_duration,
													  chunk_duration,
													  GetInitializationSegmentName(track_id),
													  _preload_hint_enabled);

	if (cenc_property.scheme != bmff::CencProtectScheme::None)
	{
		chunklist->EnableCenc(cenc_property);
	}

	{
		std::lock_guard<std::shared_mutex> storage_lock(_storage_map_lock);
		_storage_map.emplace(media_track->GetId(), storage);
	}

	{
		std::lock_guard<std::shared_mutex> packager_lock(_packager_map_lock);
		_packager_map.emplace(media_track->GetId(), packager);
	}

	{
		std::unique_lock<std::shared_mutex> lock(_chunklist_map_lock);
		_chunklist_map.emplace(track_id, chunklist);
	}

	return true;
}

bool LLHlsStream::AddVttPackager(const std::shared_ptr<const MediaTrack> &track)
{
	if (track->GetMediaType() != cmn::MediaType::Subtitle || track->GetCodecId() != cmn::MediaCodecId::WebVTT)
	{
		logte("Track is not WebVTT. id: %d, media_type: %s, codec_id: %s", track->GetId(), cmn::GetMediaTypeString(track->GetMediaType()), cmn::GetCodecIdString(track->GetCodecId()));
		return false;
	}

	// packager
	auto packager = std::make_shared<webvtt::Packager>(track);
	{
		std::lock_guard<std::shared_mutex> lock(_vtt_packagers_lock);
		_vtt_packagers[track->GetId()] = packager;
	}

	// storage
	{
		// The VTT packager also functions as storage.
		std::lock_guard<std::shared_mutex> storage_lock(_storage_map_lock);
		_storage_map.emplace(track->GetId(), packager);
	}

	// Chunklist
	auto segment_duration = std::round(static_cast<double>(_storage_config.segment_duration_ms) / 1000.0);
	auto chunk_duration = static_cast<double>(_packager_config.chunk_duration_ms) / 1000.0;
	auto refer_track = GetTrack(_vtt_reference_track_id);
	if (refer_track != nullptr)
	{
		chunk_duration = std::round(ComputeOptimalPartDuration(refer_track)) / 1000.0;
	}

	auto chunklist = std::make_shared<LLHlsChunklist>(GetChunklistName(track->GetId()),
													  track,
													  _storage_config.max_segments,
													  segment_duration,
													  chunk_duration,
													  "", // No initialization segment for VTT
													  _preload_hint_enabled);

	{
		std::unique_lock<std::shared_mutex> lock(_chunklist_map_lock);
		_chunklist_map.emplace(track->GetId(), chunklist);
	}

	return true;
}

std::shared_ptr<webvtt::Packager> LLHlsStream::GetVttPackager(const int32_t &track_id) const
{
	std::shared_lock<std::shared_mutex> lock(_vtt_packagers_lock);
	auto it = _vtt_packagers.find(track_id);
	if (it == _vtt_packagers.end())
	{
		logtw("Could not find WebVTT packager for track_id = %d", track_id);
		return nullptr;
	}

	return it->second;
}

std::map<int32_t, std::shared_ptr<webvtt::Packager>> LLHlsStream::GetVttPackagers() const
{
	std::shared_lock<std::shared_mutex> lock(_vtt_packagers_lock);
	return _vtt_packagers;
}

// Get storage with the track id
std::shared_ptr<base::modules::SegmentStorage> LLHlsStream::GetStorage(const int32_t &track_id) const
{
	std::shared_lock<std::shared_mutex> lock(_storage_map_lock);
	auto it = _storage_map.find(track_id);
	if (it == _storage_map.end())
	{
		return nullptr;
	}

	return it->second;
}

// Get fMP4 packager with the track id
std::shared_ptr<bmff::FMP4Packager> LLHlsStream::GetPackager(const int32_t &track_id) const
{
	std::shared_lock<std::shared_mutex> lock(_packager_map_lock);
	auto it = _packager_map.find(track_id);
	if (it == _packager_map.end())
	{
		return nullptr;
	}

	return it->second;
}

std::shared_ptr<LLHlsChunklist> LLHlsStream::GetChunklistWriter(const int32_t &track_id) const
{
	std::shared_lock<std::shared_mutex> lock(_chunklist_map_lock);
	auto it = _chunklist_map.find(track_id);
	if (it == _chunklist_map.end())
	{
		return nullptr;
	}

	return it->second;
}

ov::String LLHlsStream::GetChunklistName(const int32_t &track_id) const
{
	// chunklist_<track id>_<media type>_<stream key>_llhls.m3u8
	return ov::String::FormatString("chunklist_%d_%s_%s_llhls.m3u8",
									track_id,
									ov::String(cmn::GetMediaTypeString(GetTrack(track_id)->GetMediaType())).LowerCaseString().CStr(),
									_stream_key.CStr());
}

ov::String LLHlsStream::GetInitializationSegmentName(const int32_t &track_id) const
{
	// init_<track id>_<media type>_<random str>_llhls.m4s
	return ov::String::FormatString("init_%d_%s_%s_llhls.m4s",
									track_id,
									ov::String(cmn::GetMediaTypeString(GetTrack(track_id)->GetMediaType())).LowerCaseString().CStr(),
									_stream_key.CStr());
}

ov::String LLHlsStream::GetSegmentName(const int32_t &track_id, const int64_t &segment_number) const
{
	ov::String ext;
	if (GetTrack(track_id)->GetMediaType() == cmn::MediaType::Subtitle)
	{
		ext = "vtt";
	}
	else
	{
		ext = "m4s";
	}

	// seg_<track id>_<segment number>_<media type>_<random str>_llhls.m4s
	return ov::String::FormatString("seg_%d_%" PRId64 "_%s_%s_llhls.%s",
									track_id,
									segment_number,
									ov::String(cmn::GetMediaTypeString(GetTrack(track_id)->GetMediaType())).LowerCaseString().CStr(),
									_stream_key.CStr(),
									ext.CStr());
}

ov::String LLHlsStream::GetPartialSegmentName(const int32_t &track_id, const int64_t &segment_number, const int64_t &partial_number) const
{
	ov::String ext;
	if (GetTrack(track_id)->GetMediaType() == cmn::MediaType::Subtitle)
	{
		ext = "vtt";
	}
	else
	{
		ext = "m4s";
	}

	// part_<track id>_<segment number>_<partial number>_<media type>_<random str>_llhls.m4s
	return ov::String::FormatString("part_%d_%" PRId64 "_%" PRId64 "_%s_%s_llhls.%s",
									track_id,
									segment_number,
									partial_number,
									ov::String(cmn::GetMediaTypeString(GetTrack(track_id)->GetMediaType())).LowerCaseString().CStr(),
									_stream_key.CStr(),
									ext.CStr());
}

ov::String LLHlsStream::GetNextPartialSegmentName(const int32_t &track_id, const int64_t &segment_number, const int64_t &partial_number, bool last_chunk) const
{
	ov::String ext;
	if (GetTrack(track_id)->GetMediaType() == cmn::MediaType::Subtitle)
	{
		ext = "vtt";
	}
	else
	{
		ext = "m4s";
	}

	auto next_segment_number = 0;
	auto next_partial_number = 0;

	if (last_chunk == true)
	{
		next_segment_number = segment_number + 1;
		next_partial_number = 0;
	}
	else
	{
		next_segment_number = segment_number;
		next_partial_number = partial_number + 1;
	}

	// part_<track id>_<segment number>_<partial number>_<media type>_<random str>_llhls.m4s
	return ov::String::FormatString("part_%d_%d_%d_%s_%s_llhls.%s",
									track_id,
									next_segment_number,
									next_partial_number,
									ov::String(cmn::GetMediaTypeString(GetTrack(track_id)->GetMediaType())).LowerCaseString().CStr(),
									_stream_key.CStr(),
									ext.CStr());
}

void LLHlsStream::OnFMp4StorageInitialized(const int32_t &track_id)
{
	// Not to do anything
}

bool LLHlsStream::IsReadyToPlay() const
{
	return _playlist_ready;
}

bool LLHlsStream::CheckPlaylistReady()
{
	// lock
	std::lock_guard<std::shared_mutex> lock(_playlist_ready_lock);
	if (_playlist_ready == true)
	{
		return true;
	}

	std::shared_lock<std::shared_mutex> storage_lock(_storage_map_lock);

	for (const auto &[track_id, storage] : _storage_map)
	{
		// At least one segment must be created.
		if (storage->GetSegmentCount() <= 1)
		{
			return false;
		}

		_max_chunk_duration_ms = std::max(_max_chunk_duration_ms, storage->GetMaxPartialDurationMs());
		_min_chunk_duration_ms = std::min(_min_chunk_duration_ms, storage->GetMinPartialDurationMs());
	}

	storage_lock.unlock();

	std::shared_lock<std::shared_mutex> chunklist_lock(_chunklist_map_lock);

	double min_part_hold_back = (static_cast<double>(_max_chunk_duration_ms) / 1000.0f) * 3.0f;
	double final_part_hold_back = std::max(min_part_hold_back, _configured_part_hold_back);
	for (const auto &[track_id, chunklist] : _chunklist_map)
	{
		chunklist->SetPartHoldBack(final_part_hold_back);

		DumpInitSegmentOfAllItems(chunklist->GetTrack()->GetId());
	}

	chunklist_lock.unlock();

	logti("LLHlsStream(%s/%s) - Ready to play : Part Hold Back = %f", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), final_part_hold_back);

	_playlist_ready = true;

	auto alert = MonitorInstance->GetAlert();
	auto stream_metrics = StreamMetrics(*std::static_pointer_cast<info::Stream>(pub::Stream::GetSharedPtr()));

	alert->SendStreamMessage(mon::alrt::Message::Code::EGRESS_LLHLS_READY, stream_metrics);

	// Dump master playlist if configured
	DumpMasterPlaylistsOfAllItems();

	return true;
}

void LLHlsStream::OnMediaSegmentCreated(const int32_t &track_id, const uint32_t &segment_number)
{
	// Check whether at least one segment of every track has been created.
	CheckPlaylistReady();

	auto playlist = GetChunklistWriter(track_id);
	if (playlist == nullptr)
	{
		logte("Playlist is not found : track_id = %d", track_id);
		return;
	}

	auto storage = GetStorage(track_id);
	if (storage == nullptr)
	{
		logte("Storage is not found : stream = %s, track_id = %d", GetName().CStr(), track_id);
		return;
	}

	auto segment = storage->GetSegment(segment_number);
	if (segment == nullptr)
	{
		logte("Segment is not found : stream = %s, track_id = %d, segment_number = %u", GetName().CStr(), track_id, segment_number);
		return;
	}

	// Empty segment
	auto segment_info = LLHlsChunklist::SegmentInfo(segment->GetNumber(), GetSegmentName(track_id, segment->GetNumber()));

	playlist->CreateSegmentInfo(segment_info);

	if (IsVttEnabled() && track_id == _vtt_reference_track_id)
	{
		// If this is a VTT reference track, we need to create a chunklist for vtt chunklists as well
		for (const auto &it : _vtt_packagers)
		{
			auto vtt_track_id = it.first;
			auto segment_info = LLHlsChunklist::SegmentInfo(segment->GetNumber(), GetSegmentName(vtt_track_id, segment->GetNumber()));
			auto vtt_playlist = GetChunklistWriter(vtt_track_id);
			if (vtt_playlist == nullptr)
			{
				logte("VTT Playlist is not found : track_id = %d", vtt_track_id);
				continue;
			}

			vtt_playlist->CreateSegmentInfo(segment_info);				
		}
	}

	logtt("Media segment updated : track_id = %d, segment_number = %d", track_id, segment_number);
}

void LLHlsStream::OnMediaChunkUpdated(const int32_t &track_id, const uint32_t &segment_number, const uint32_t &chunk_number, bool last_chunk)
{
	auto playlist = GetChunklistWriter(track_id);
	if (playlist == nullptr)
	{
		logte("Playlist is not found : track_id = %d", track_id);
		return;
	}

	auto storage = GetStorage(track_id);
	if (storage == nullptr)
	{
		logte("Storage is not found : stream = %s, track_id = %d", GetName().CStr(), track_id);
		return;
	}

	auto partial_segment = storage->GetPartialSegment(segment_number, chunk_number);
	if (partial_segment == nullptr)
	{
		logte("Chunk is not found : stream = %s, track_id = %d, segment_number = %u, chunk_number = %u", GetName().CStr(), track_id, segment_number, chunk_number);
		return;
	}

	// Milliseconds to seconds
	auto chunk_duration = static_cast<double>(partial_segment->GetDurationMs()) / static_cast<double>(1000.0);

	// Human readable timestamp
	if (_first_chunk == true)
	{
		_first_chunk = false;

		auto first_chunk_timestamp_ms = (static_cast<double>(partial_segment->GetStartTimestamp()) / GetTrack(track_id)->GetTimeBase().GetTimescale()) * 1000.0;

		_wallclock_offset_ms = std::chrono::duration_cast<std::chrono::milliseconds>(GetInputStreamPublishedTime().time_since_epoch()).count() - first_chunk_timestamp_ms;

		for (const auto &[track_id, chunklist] : _chunklist_map)
		{
			chunklist->SetWallclockOffset(_wallclock_offset_ms);
		}
	}

	auto start_timestamp = (static_cast<double>(partial_segment->GetStartTimestamp()) / GetTrack(track_id)->GetTimeBase().GetTimescale()) * 1000.0;
	start_timestamp += _wallclock_offset_ms;

	auto partial_info = LLHlsChunklist::SegmentInfo(partial_segment->GetNumber(), start_timestamp, chunk_duration, partial_segment->GetDataLength(),
												  GetPartialSegmentName(track_id, segment_number, partial_segment->GetNumber()),
												  GetNextPartialSegmentName(track_id, segment_number, partial_segment->GetNumber(), last_chunk),
												  partial_segment->IsIndependent(), last_chunk);

	// Set markers to chunk_info
	auto segment = storage->GetSegment(segment_number);
	if (segment != nullptr)
	{
		if (segment->HasMarker() == true)
		{
			logtd("Media chunk has markers : track_id = %d, segment_number = %d, chunk_number = %d", track_id, segment_number, chunk_number);
			for (const auto &marker : segment->GetMarkers())
			{
				logtd("Marker : timestamp = %" PRId64 ", tag = %s", marker->GetTimestamp(), marker->GetTag().CStr());
			}
			partial_info.SetMarkers(segment->GetMarkers());
		}
	}

	playlist->AppendPartialSegmentInfo(segment_number, partial_info);

	logtt("Media chunk updated : track_id = %u, segment_number = %u, chunk_number = %d, start_timestamp = %" PRId64 ", chunk_duration = %f", track_id, segment_number, chunk_number, partial_segment->GetStartTimestamp(), chunk_duration);

	// Make Subtitle
	if (IsVttEnabled() && track_id == _vtt_reference_track_id)
	{
		// If this is a VTT reference track, we need to create a chunklist for vtt chunklists as well
		std::shared_lock<std::shared_mutex> vtt_packagers_lock(_vtt_packagers_lock);
		auto vtt_packagers = _vtt_packagers; // Copy to avoid deadlock
		vtt_packagers_lock.unlock();
		for (const auto &it : vtt_packagers)
		{
			auto vtt_track_id = it.first;

			// Make Subtitle chunk
			auto vtt_packager = GetVttPackager(vtt_track_id);
			if (vtt_packager == nullptr)
			{
				logte("Could not find WebVTT packager for track_id = %d", vtt_track_id);
				continue;
			}
			
			auto vtt_chunk_start_timestamp = (static_cast<double>(partial_segment->GetStartTimestamp()) / GetTrack(track_id)->GetTimeBase().GetTimescale()) * 1000.0;
			if (vtt_packager->MakePartialSegment(segment_number, chunk_number, vtt_chunk_start_timestamp, partial_segment->GetDurationMs()) == false)
			{
				logte("Failed to make partial segment for VTT track_id = %d, segment_number = %d, chunk_number = %d", vtt_track_id, segment_number, chunk_number);
				continue;
			}

			if (last_chunk == true)
			{
				// Make segment
				auto vtt_segment_start_timestamp = (static_cast<double>(segment->GetStartTimestamp()) / GetTrack(track_id)->GetTimeBase().GetTimescale()) * 1000.0;
				if (vtt_packager->MakeSegment(segment_number, vtt_segment_start_timestamp, segment->GetDurationMs()) == false)
				{
					logte("Failed to make segment for VTT track_id = %d, segment_number = %d", vtt_track_id, segment_number);
					continue;
				}

				if (segment->HasMarker() == true)
				{
					auto vtt_segment = vtt_packager->GetSegment(segment_number);
					if (vtt_segment != nullptr)
					{
						vtt_segment->SetMarkers(segment->GetMarkers());
					}
				}
			}
			
			// Update chunklist
			OnMediaChunkUpdated(vtt_track_id, segment_number, chunk_number, last_chunk);
		}
	}

	// Notify
	NotifyPlaylistUpdated(track_id, segment_number, chunk_number);

	if (last_chunk == true)
	{
		DumpSegmentOfAllItems(track_id, segment_number);
	}
}

void LLHlsStream::OnMediaSegmentDeleted(const int32_t &track_id, const uint32_t &segment_number)
{
	auto playlist = GetChunklistWriter(track_id);
	if (playlist == nullptr)
	{
		logte("Playlist is not found : track_id = %d", track_id);
		return;
	}

	if (IsVttEnabled() && track_id == _vtt_reference_track_id)
	{
		std::shared_lock<std::shared_mutex> vtt_packagers_lock(_vtt_packagers_lock);
		// If this is a VTT reference track, we need to delete a chunklist for vtt chunklists as well
		for (const auto &it : _vtt_packagers)
		{
			auto vtt_track_id = it.first;
			auto vtt_packager = it.second;
			auto vtt_playlist = GetChunklistWriter(vtt_track_id);
			if (vtt_playlist == nullptr)
			{
				logte("VTT Playlist is not found : track_id = %d", vtt_track_id);
				continue;
			}

			vtt_playlist->RemoveSegmentInfo(segment_number);
			vtt_packager->DeleteSegment(segment_number);
		}
	}

	playlist->RemoveSegmentInfo(segment_number);

	logtt("Media segment deleted : track_id = %d, segment_number = %d", track_id, segment_number);
}

void LLHlsStream::NotifyPlaylistUpdated(const int32_t &track_id, const int64_t &msn, const int64_t &part)
{
	// Make std::any for broadcast
	// I think make_shared is better than copy sizeof(PlaylistUpdatedEvent) to all sessions
	auto event = std::make_shared<PlaylistUpdatedEvent>(track_id, msn, part);
	auto notification = std::make_any<std::shared_ptr<PlaylistUpdatedEvent>>(event);
	BroadcastPacket(notification);
}

int64_t LLHlsStream::GetMinimumLastSegmentNumber() const
{
	// lock storage map
	std::shared_lock<std::shared_mutex> storage_lock(_storage_map_lock);
	int64_t min_segment_number = std::numeric_limits<int64_t>::max();
	for (const auto &it : _storage_map)
	{
		auto storage = it.second;
		if (storage == nullptr)
		{
			continue;
		}

		auto segment_number = storage->GetLastSegmentNumber();
		if (segment_number < min_segment_number)
		{
			min_segment_number = segment_number;
		}
	}

	return min_segment_number;
}

std::tuple<bool, ov::String> LLHlsStream::StartDump(const std::shared_ptr<info::Dump> &info)
{
	std::lock_guard<std::shared_mutex> lock(_dumps_lock);

	for (const auto &it : _dumps)
	{
		// Check duplicate ID
		if (it.second->GetId() == info->GetId())
		{
			return {false, "Duplicate ID"};
		}

		// Check duplicate infoFile
		if ((it.second->GetInfoFileUrl().IsEmpty() == false) && it.second->GetInfoFileUrl() == info->GetInfoFileUrl())
		{
			return {false, "Duplicate info file"};
		}
	}

	auto dump_info = std::make_shared<mdl::Dump>(info);
	dump_info->SetEnabled(true);

	// lock playlist ready
	std::shared_lock<std::shared_mutex> lock_playlist_ready(_playlist_ready_lock);
	if (IsReadyToPlay() == false)
	{
		// If the playlist is not ready, add it to the queue and wait for the playlist to be ready.
		// It will work when the playlist is ready (CheckPlaylistReady()).
		_dumps.emplace(dump_info->GetId(), dump_info);
		return {true, ""};
	}
	lock_playlist_ready.unlock();

	// Dump Init Segment for all tracks
	std::shared_lock<std::shared_mutex> storage_lock(_storage_map_lock);
	auto storage_map = _storage_map;
	storage_lock.unlock();

	// Find minimum segment number
	int64_t min_segment_number = GetMinimumLastSegmentNumber();

	logti("Start dump requested: stream_name = %s, dump_id = %s, min_segment_number = %" PRId64, GetName().CStr(), dump_info->GetId().CStr(), min_segment_number);

	for (const auto &it : storage_map)
	{
		auto track_id = it.first;
		if (DumpInitSegment(dump_info, track_id) == false)
		{
			return {false, "Could not dump init segment"};
		}

		// Dump from min_segment_number to last segment
		auto storage = it.second;
		if (storage == nullptr)
		{
			continue;
		}

		auto last_segment_number = storage->GetLastSegmentNumber();
		for (int64_t segment_number = min_segment_number; segment_number <= last_segment_number; segment_number++)
		{
			if (DumpSegment(dump_info, track_id, segment_number) == false)
			{
				return {false, "Could not dump segment"};
			}

			logti("Dump base segment : stream_name = %s, dump_id = %s, track_id = %d, segment_number = %" PRId64 ", min_segment_number = %" PRId64 ", last_segment_number = %" PRId64 , GetName().CStr(), dump_info->GetId().CStr(), track_id, segment_number, min_segment_number, last_segment_number);
		}
	}

	// Dump Master Playlist
	if (DumpMasterPlaylist(dump_info) == false)
	{
		StopToSaveOldSegmentsInfo();
		return {false, "Could not dump master playlist"};
	}

	_dumps.emplace(dump_info->GetId(), dump_info);

	return {true, ""};
}

std::tuple<bool, ov::String> LLHlsStream::StopDump(const std::shared_ptr<info::Dump> &dump_info)
{
	std::shared_lock<std::shared_mutex> lock(_dumps_lock);

	if (dump_info->GetId().IsEmpty() == false)
	{
		auto it = _dumps.find(dump_info->GetId());
		if (it == _dumps.end())
		{
			return {false, "Could not find dump info"};
		}
		auto dump_item = it->second;
		dump_item->SetEnabled(false);
		dump_item->CompleteDump();
	}
	// All stop
	else
	{
		for (const auto &it : _dumps)
		{
			auto dump_item = it.second;
			dump_item->SetEnabled(false);
			dump_item->CompleteDump();
		}
	}

	StopToSaveOldSegmentsInfo();

	lock.unlock();

	return {true, ""};
}

// It must be called in the lock of _dumps_lock
bool LLHlsStream::StopToSaveOldSegmentsInfo()
{
	// check if all dumps are disabled
	bool all_disabled = true;
	for (const auto &it : _dumps)
	{
		auto dump_item = it.second;
		if (dump_item->IsEnabled())
		{
			all_disabled = false;
			break;
		}
	}

	if (all_disabled == true)
	{
		// stop to keep old segments in _chunklist_map
		// shared lock
		std::shared_lock<std::shared_mutex> chunk_lock(_chunklist_map_lock);
		for (const auto &it : _chunklist_map)
		{
			auto chunklist = it.second;
			chunklist->SaveOldSegmentInfo(false);
		}
	}

	return true;
}

// Get dump info
std::shared_ptr<const mdl::Dump> LLHlsStream::GetDumpInfo(const ov::String &dump_id)
{
	std::shared_lock<std::shared_mutex> lock(_dumps_lock);
	auto it = _dumps.find(dump_id);
	if (it == _dumps.end())
	{
		return nullptr;
	}
	return it->second;
}

// Get dumps
std::vector<std::shared_ptr<const mdl::Dump>> LLHlsStream::GetDumpInfoList()
{
	std::vector<std::shared_ptr<const mdl::Dump>> dump_list;
	std::shared_lock<std::shared_mutex> lock(_dumps_lock);
	for (const auto &it : _dumps)
	{
		dump_list.push_back(it.second);
	}
	return dump_list;
}

bool LLHlsStream::IsVttEnabled() const
{
	return _vtt_enabled;
}