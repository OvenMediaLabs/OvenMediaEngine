#pragma once

#define OVT_SIGNALING_VERSION 0x12

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