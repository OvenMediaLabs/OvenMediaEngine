#pragma once

#include <base/mediarouter/media_type.h>
#include <base/ovlibrary/ovlibrary.h>

#include <optional>
#include <set>
#include <vector>

#define OVT_SIGNALING_VERSION 0x12

namespace ovt
{
	// `ovt.version`: the generation of the build that sent the message, on requests and responses alike.
	// It is never a gate. A peer at any value is accepted, and a lower one only means the sender may
	// hold back something added later; refusing on it would put the older side, the one that knows less,
	// in charge of the judgment.
	// It counts up in any release whose behavior changes, including a parser fix, because the value
	// exists so that a later release can say "peers at or below N mishandle this, so do not send it".
	// It is not the OME release version and not the OVT1/OVT2 number.
	// A peer that cannot be told apart is the one this field is for, so it ships from the first release:
	// adding it later leaves every build already deployed anonymous for good.
	constexpr uint32_t OVT_PROTOCOL_VERSION		= 1;

	// `ovt.mediaHeaderVersion`: the layout of the 36-byte media packet header this origin writes.
	// It counts up only when that layout changes so that the old rules would misread it,
	// and never for a feature or a new describe field.
	// Only a response carries it: an edge sends no media, so it has no layout to declare.
	// An edge refuses an origin above this value and reads anything at or below it,
	// because a build knows every layout up to its own.
	constexpr uint32_t OVT_MEDIA_HEADER_VERSION = 1;

	// `ovt.agent`: identifies this build to the peer for diagnostics.
	// Nothing is ever decided on it.
	ov::String GetOvtAgent();

	// A peer-chosen string, made fit to keep and to log (`ovt.agent`, an unknown `application`).
	// A message may carry up to `MAX_MESSAGE_BUFFER_SIZE` of it, so it is cut to this length,
	// and a control character becomes `.` so it cannot forge a log line.
	constexpr size_t MAX_LOG_STRING_LENGTH = 128;
	ov::String SanitizeForLog(const ov::String &text);

	// An echoed field of a failure response. The peer chooses the string this is built from
	// (the request's `application`, or a message that quotes its `target`), so the cut happens here
	// rather than at each of the sites that format one. A cut value ends with `...`.
	constexpr size_t MAX_RESPONSE_FIELD_LENGTH = 256;
	ov::String TruncateForResponse(const ov::String &text);

	// Peer detection: the `ovt` object is the only signal.
	// Its absence, and a `null` in its place, mean OVT1.
	inline bool HasOvtObject(const Json::Value &root)
	{
		return root.isObject() && root["ovt"].isObject();
	}

	// `ovt` filled with anything but an object: neither an OVT1 peer, which leaves the slot empty,
	// nor a readable OVT2 one. Both directions refuse such a message rather than pick a generation for it,
	// because reading it as OVT1 would turn off `required` and the track skip without the peer knowing.
	inline bool HasNonObjectOvt(const Json::Value &root)
	{
		if (root.isObject() == false)
		{
			return false;
		}

		const Json::Value &slot = root["ovt"];

		return (slot.isNull() == false) && (slot.isObject() == false);
	}

	// The origin's `ovt.mediaHeaderVersion` from a response. An OVT2 origin must declare it,
	// so `nullopt` on a response `HasOvtObject()` accepted is a broken OVT2 response, not an OVT1 one.
	inline std::optional<uint32_t> GetMediaHeaderVersion(const Json::Value &root)
	{
		if (HasOvtObject(root) == false)
		{
			return std::nullopt;
		}

		const Json::Value &version = root["ovt"]["mediaHeaderVersion"];
		if (version.isUInt() == false)
		{
			return std::nullopt;
		}

		return version.asUInt();
	}

	// The peer's `ovt.version`, from either direction. A missing or unreadable one is 0,
	// which is the oldest generation and so the most conservative thing to assume.
	// It is not refused: this field never decides whether a peer is served.
	inline uint32_t GetPeerVersion(const Json::Value &root)
	{
		if (HasOvtObject(root) == false)
		{
			return 0;
		}

		const Json::Value &version = root["ovt"]["version"];

		return version.isUInt() ? version.asUInt() : 0;
	}

	// What a describe or play response says about the origin that sent it.
	// The whole judgment is here so that it can be exercised on a `Json::Value` alone:
	// the caller only maps a verdict to a log line and a return.
	enum class OriginVerdict : uint8_t
	{
		// No `ovt` object. An OVT1 origin, read under the OVT1 rules
		Ovt1,
		// An `ovt` object this build can work with
		Ovt2,
		// `ovt` filled with something that is neither an object nor null
		NotAnObject,
		// Declared OVT2 without a readable `mediaHeaderVersion`, so the layout of the media
		// it is about to send is unknown. Refused rather than read as OVT1, which would also
		// turn off `required` and the track skip the origin is counting on
		NoMediaHeaderVersion,
		// Writes a media packet header layout above the one this build reads
		MediaHeaderTooNew,
	};

	OriginVerdict JudgeOriginResponse(const Json::Value &root, uint32_t max_media_header_version = OVT_MEDIA_HEADER_VERSION);

	// `ovt` object of an edge request: `agent` and `version`
	Json::Value MakeRequestOvtObject();
	// `ovt` object of an origin response: `agent`, `version` and `mediaHeaderVersion`
	Json::Value MakeResponseOvtObject();

	// `application` of a request or of an origin-initiated message.
	// Every release writes these spellings and compares what it receives case-insensitively,
	// so the spelling here is the wire form and `IsApplication()` is the only way to test one.
	constexpr const char *APPLICATION_DESCRIBE		  = "describe";
	constexpr const char *APPLICATION_PLAY			  = "play";
	constexpr const char *APPLICATION_STOP			  = "stop";
	constexpr const char *APPLICATION_NOTIFY		  = "notify";

	// `ovt.reason` on a describe or play whose filtered track set is empty.
	// The response code stays 200 because v0.21.0.0 answered an empty set that way,
	// and an OVT1 edge must keep seeing that response.
	// An OVT2 edge reads this token instead and stops rather than waiting for media that never comes.
	constexpr const char *REASON_NO_DELIVERABLE_TRACK = "no-deliverable-track";

	inline bool IsApplication(const ov::String &application, const char *name)
	{
		return application.LowerCaseString() == name;
	}

	// `ovt.required` and PT 40 tokens: `<category>/<name>`.
	// The names are the `Get*String()` outputs and the features are the fixed list in `ovt_signaling.cpp`;
	// the vocabulary is defined nowhere else.
	// An edge compares tokens with `==` against the set it supports and never splits or case-folds them,
	// so a category can be added without touching the check.
	// Media packet flags are not tokens: a flag is a bit, not a value.

	// From the `codec` string of a describe track (the `GetCodecIdString()` spelling)
	ov::String CodecToken(const ov::String &codec_name);
	ov::String CodecToken(cmn::MediaCodecId codec_id);
	ov::String BitstreamToken(cmn::BitstreamFormat format);
	ov::String PacketTypeToken(cmn::PacketType packet_type);

	// Every token above, plus the features
	const std::set<ov::String> &SupportedRequiredTokens();
	inline bool IsSupportedRequiredToken(const ov::String &token)
	{
		return SupportedRequiredTokens().count(token) > 0;
	}

	// A track id array of an `ovt` object: `trackIds` of a play request, `allowedTrackIds` of its response.
	// `nullopt` means the peer named no set, which is the key being absent or holding something else.
	// A non-integer entry is skipped rather than voiding the list, and `ignored` counts those;
	// one malformed element must not turn a selection into "send everything".
	std::optional<std::set<uint32_t>> ParseTrackIdArray(const Json::Value &value, size_t *ignored = nullptr);

	// PT 40 payload: `{"required":[...]}`
	ov::String MakeRequiredPayload(const std::vector<ov::String> &tokens);
	// The `required` array of a PT 40 payload or of an `ovt` object; nullopt when it is not an array of strings
	std::optional<std::vector<ov::String>> ParseRequiredTokens(const Json::Value &required);

	// Reason an origin gives when it stops a stream (`application: "stop"`, `message: <reason>`).
	// Fixed strings and never numeric codes; a receiver counts a string it does not know as unknown.
	enum class StopReason : uint8_t
	{
		StreamDeleted,
		OriginShutdown,
		Evicted,
		Internal,
	};

	constexpr const char *ToString(StopReason reason)
	{
		switch (reason)
		{
			case StopReason::StreamDeleted:
				return "stream-deleted";
			case StopReason::OriginShutdown:
				return "origin-shutdown";
			case StopReason::Evicted:
				return "evicted";
			case StopReason::Internal:
				return "internal";
		}

		return "internal";
	}

	inline std::optional<StopReason> ParseStopReason(const ov::String &reason)
	{
		for (auto candidate : {StopReason::StreamDeleted, StopReason::OriginShutdown, StopReason::Evicted, StopReason::Internal})
		{
			if (reason == ToString(candidate))
			{
				return candidate;
			}
		}

		return std::nullopt;
	}
}  // namespace ovt

/*
	"version": 0x12,
	"stream" :
	{
		"appName" : "app",
		"streamName" : "stream_720p",
		"streamUUID" : "OvenMediaEngine_90b8b53e-3140-4e59-813d-9ace51c0e186/default/#default#app/stream",
		"playlists":[
			{
				"name" : "for llhls",
				"fileName" : "llhls_abr.oven",
				"options" :	// Required - the receiver rejects the entire describe payload if options is not an object
				{
					"webrtcAutoAbr" : true // the receiver assumes false when this key is omitted
				},
				"renditions":
				[
					{
						"name" : "1080p",
						"videoTrackName" : "1080p",
						"audioTrackName" : "default",
					},
					{
						"name" : "720",
						"videoTrackName" : "720p",
						"audioTrackName" : "default",
					}
				],
				[
					...
				]
			},
			{
				...
			}
		],
		"tracks":
		[
			{
				"id" : 3291291,
				"name" : "1080p",
				"codecId" : 32198392,
				"mediaType" : 0 | 1 | 2, # video | audio | data
				"timebaseNum" : 90000,
				"timebaseDen" : 90000,
				"bitrate" : 5000000,
				"startFrameTime" : 1293219321,
				"lastFrameTime" : 1932193921,

			### videoTrack or audioTrack
				"videoTrack" :
				{
					"framerate" : 29.97,
					"width" : 1280,
					"height" : 720
				},
				"audioTrack" :
				{
					"samplerate" : 44100,
					"sampleFormat" : "s16",
					"layout" : "stereo"
				},
				
			### decoderConfig : Decoder configuration record (avcc, hvcc, aac, etc...), base64 encoded string
				"decoderConfig" : "Z2Q="
			}
		]
	}
*/