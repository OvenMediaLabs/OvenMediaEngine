#pragma once

#define OVT_SIGNALING_VERSION 0x12

namespace ovt
{
	/// Tri-state for runtime-negotiated OVT capabilities (e.g. runtime widening).
	///
	/// Edges start at `UNKNOWN` and transition based on what the origin advertises
	/// in its `describe` response or how it answers a runtime command.
	enum class CapabilitySupport : uint8_t
	{
		/// No negotiation has happened yet, or the origin is silent on this
		/// capability. Treated optimistically: edges may try the capability
		/// and downgrade on rejection.
		UNKNOWN = 0,

		/// The origin advertised the capability or accepted a request that
		/// exercises it. Edges may use it freely.
		SUPPORTED,

		/// The origin explicitly disclaimed the capability or rejected a
		/// request that needed it. Edges must avoid it (e.g. fall back to
		/// compatibility paths).
		UNSUPPORTED,
	};
}  // namespace ovt

/*
	request:
	{
		"id": 1,
		"application": "subscribe",
		"target": "ovt://host:9000/app/stream",
		"contents":
		{
			"fullStream": true,
			"trackIds": [1001, 1002] // Optional, exact union track set when fullStream=false
		}
	}

	"version": 0x12,
	"capabilities":
	{
		"runtimeWidening": true // Optional
	},
	"stream" :
	{
		"appName" : "app",
		"streamName" : "stream_720p",
		"streamUUID" : "OvenMediaEngine_90b8b53e-3140-4e59-813d-9ace51c0e186/default/#default#app/stream",
		"playlists":[
			{
				"name" : "for llhls",
				"fileName" : "llhls_abr.oven",
				"options" :	// Optional
				{
					"webrtcAutoAbr" : true // default true
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