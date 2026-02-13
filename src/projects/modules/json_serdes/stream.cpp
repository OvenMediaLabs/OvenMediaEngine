//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "application.h"
#include "common.h"

namespace serdes
{
	static void SetConnection(Json::Value &parent_object, const char *key, const void *temp, Optional optional)
	{
	}

	static void SetTimebase(Json::Value &parent_object, const char *key, const cmn::Timebase &timebase, Optional optional)
	{
		CONVERTER_RETURN_IF(timebase.GetDen() > 0, Json::objectValue);

		SetInt(object, "num", timebase.GetNum());
		SetInt(object, "den", timebase.GetDen());
	}

	static void SetVideoTrack(Json::Value &parent_object, const char *key, const std::shared_ptr<const MediaTrack> &track, Optional optional)
	{
		CONVERTER_RETURN_IF(track == nullptr, Json::objectValue);

		SetBool(object, "bypass", track->IsBypass());

		SetString(object, "codec", cmn::GetCodecIdString(track->GetCodecId()), Optional::False);
		SetInt(object, "width", track->GetWidth());
		SetInt(object, "maxWidth", track->GetMaxWidth());
		SetInt(object, "height", track->GetHeight());
		SetInt(object, "maxHeight", track->GetMaxHeight());
		SetInt(object, "bitrate", track->GetBitrate());
		SetInt(object, "bitrateConf", track->GetBitrateByConfig());
		SetInt(object, "bitrateAvg", track->GetBitrateByMeasured());
		SetInt(object, "bitrateLatest", track->GetBitrateLastSecond());
		SetFloat(object, "framerate", track->GetFrameRate());
		SetFloat(object, "framerateConf", track->GetFrameRateByConfig());
		SetFloat(object, "framerateAvg", track->GetFrameRateByMeasured());
		SetFloat(object, "framerateLatest", track->GetFrameRateLastSecond());
		SetTimebase(object, "timebase", track->GetTimeBase(), Optional::False);
		SetBool(object, "hasBframes", track->HasBframes());
		SetFloat(object, "keyFrameInterval", track->GetKeyFrameInterval());
		SetFloat(object, "keyFrameIntervalConf", track->GetKeyFrameIntervalByConfig());
		SetFloat(object, "keyFrameIntervalAvg", track->GetKeyFrameIntervalByMeasured());
		SetFloat(object, "keyFrameIntervalLatest", track->GetKeyFrameIntervalLatest());
		SetInt(object, "deltaFramesSinceLastKeyFrame", track->GetDeltaFramesSinceLastKeyFrame());
	}

	static void SetAudioChannel(Json::Value &parent_object, const char *key, const cmn::AudioChannel &channel, Optional optional)
	{
		CONVERTER_RETURN_IF(channel.GetLayout() == cmn::AudioChannel::Layout::LayoutUnknown, Json::objectValue);

		SetString(object, "layout", channel.GetName(), Optional::False);
		SetInt(object, "count", channel.GetCounts());
	}

	static void SetAudioTrack(Json::Value &parent_object, const char *key, const std::shared_ptr<const MediaTrack> &track, Optional optional)
	{
		CONVERTER_RETURN_IF(track == nullptr, Json::objectValue);

		SetBool(object, "bypass", track->IsBypass());

		SetString(object, "codec", cmn::GetCodecIdString(track->GetCodecId()), Optional::False);
		SetInt(object, "samplerate", track->GetSampleRate());
		// SetAudioChannel(object, "channel", track->GetChannel(), Optional::False);
		SetInt(object, "channel", track->GetChannel().GetCounts());
		SetInt(object, "bitrate", track->GetBitrate());
		SetInt(object, "bitrateConf", track->GetBitrateByConfig());
		SetInt(object, "bitrateAvg", track->GetBitrateByMeasured());
		SetInt(object, "bitrateLatest", track->GetBitrateLastSecond());
		SetTimebase(object, "timebase", track->GetTimeBase(), Optional::False);
	}

	static void SetTrack(Json::Value &parent_object, const char *key, const std::shared_ptr<const MediaTrack> &track, Optional optional)
	{
		CONVERTER_RETURN_IF(false, Json::objectValue);

		SetInt(object, "id", track->GetId());
		SetString(object, "name", track->GetVariantName(), Optional::False);
		SetString(object, "type", cmn::GetMediaTypeString(track->GetMediaType()), Optional::False);

		switch (track->GetMediaType())
		{
			case cmn::MediaType::Video:
				SetVideoTrack(object, "video", track, Optional::False);
				break;
			case cmn::MediaType::Audio:
				SetAudioTrack(object, "audio", track, Optional::False);
				break;

			case cmn::MediaType::Unknown:
				[[fallthrough]];
			case cmn::MediaType::Data:
				[[fallthrough]];
			case cmn::MediaType::Subtitle:
				[[fallthrough]];
			case cmn::MediaType::Attachment:
				[[fallthrough]];
			case cmn::MediaType::Nb:
				break;
		}
	}

	static void SetTracks(Json::Value &parent_object, const char *key, const std::map<int32_t, std::shared_ptr<MediaTrack>> &tracks, Optional optional)
	{
		CONVERTER_RETURN_IF(false, Json::arrayValue);

		for (auto &item : tracks)
		{
			auto &track = item.second;

			Json::Value track_value;

			SetTrack(track_value, nullptr, track, Optional::False);

			object.append(track_value);
		}
	}

	static void SetRenditions(Json::Value &parent_object, const char *key, const std::vector<std::shared_ptr<info::Rendition>> &renditions, Optional optional)
	{
		CONVERTER_RETURN_IF(false, Json::arrayValue);

		for (auto &rendition : renditions)
		{
			Json::Value rendition_value;

			SetString(rendition_value, "name", rendition->GetName(), Optional::False);
			SetString(rendition_value, "videoVariantName", rendition->GetVideoVariantName(), Optional::False);
			SetString(rendition_value, "audioVariantName", rendition->GetAudioVariantName(), Optional::False);

			object.append(rendition_value);
		}
	}

	static void SetOptions(Json::Value &parent_object, const char *key, const std::shared_ptr<const info::Playlist> &playlist, Optional optional)
	{
		CONVERTER_RETURN_IF(false, Json::objectValue);

		object["webrtcAutoAbr"] = playlist->IsWebRtcAutoAbr();
		object["hlsChunklistPathDepth"] = playlist->GetHlsChunklistPathDepth();
		object["enableTsPackaging"] = playlist->IsTsPackagingEnabled();
	}

	static void SetPlaylists(Json::Value &parent_object, const char *key, const std::map<ov::String, std::shared_ptr<const info::Playlist>> &playlist, Optional optional)
	{
		CONVERTER_RETURN_IF(false, Json::arrayValue);

		for (auto &item : playlist)
		{
			auto &playlist = item.second;

			Json::Value playlist_value;

			SetString(playlist_value, "name", playlist->GetName(), Optional::False);
			SetString(playlist_value, "fileName", playlist->GetFileName(), Optional::False);
			SetOptions(playlist_value, "options", playlist, Optional::True);
			SetRenditions(playlist_value, "renditions", playlist->GetRenditionList(), Optional::True);

			object.append(playlist_value);
		}
	}

	static void SetInputStream(Json::Value &parent_object, const char *key, const std::shared_ptr<const mon::StreamMetrics> &stream, Optional optional)
	{
		auto common_metrics = std::static_pointer_cast<const mon::CommonMetrics>(stream);

		CONVERTER_RETURN_IF(common_metrics == nullptr, Json::objectValue);

		SetString(object, "sourceType", ::StringFromStreamSourceType(stream->GetSourceType()), Optional::False);
		SetString(object, "sourceUrl", stream->GetMediaSource(), Optional::True);
		// TODO(dimiden): Complete this function
		SetConnection(object, "connection", stream.get(), Optional::True);
		SetTracks(object, "tracks", stream->GetTracks(), Optional::False);
		SetTimestamp(object, "createdTime", common_metrics->GetCreatedTime());
	}

	static void SetOutputStreams(Json::Value &parent_object, const char *key, const std::vector<std::shared_ptr<mon::StreamMetrics>> &output_streams, Optional optional)
	{
		CONVERTER_RETURN_IF(false, Json::arrayValue);

		for (auto &output_stream : output_streams)
		{
			Json::Value output_value;

			SetString(output_value, "name", output_stream->GetName(), Optional::False);
			SetTracks(output_value, "tracks", output_stream->GetTracks(), Optional::False);
			SetPlaylists(output_value, "playlists", output_stream->GetPlaylists(), Optional::True);

			object.append(output_value);
		}
	}

	Json::Value JsonFromTrack(const std::shared_ptr<const MediaTrack> &track)
	{
		Json::Value response(Json::ValueType::objectValue);
		SetTrack(response, nullptr, track, Optional::False);
		return response;
	}

	Json::Value JsonFromTracks(const std::map<int32_t, std::shared_ptr<MediaTrack>> &tracks)
	{
		Json::Value response(Json::ValueType::arrayValue);
		SetTracks(response, nullptr, tracks, Optional::False);
		return response;
	}

	Json::Value JsonFromStream(const std::shared_ptr<const mon::StreamMetrics> &stream)
	{
		Json::Value response(Json::ValueType::objectValue);
		SetString(response, "name", stream->GetName(), Optional::False);
		SetInputStream(response, nullptr, stream, Optional::False);
		return response;
	}

	Json::Value JsonFromStream(const std::shared_ptr<const mon::StreamMetrics> &stream, const std::vector<std::shared_ptr<mon::StreamMetrics>> &output_streams)
	{
		Json::Value response(Json::ValueType::objectValue);

		SetString(response, "name", stream->GetName(), Optional::False);
		SetInputStream(response, "input", stream, Optional::False);
		SetOutputStreams(response, "outputs", output_streams, Optional::False);
		
		return response;
	}
}  // namespace serdes