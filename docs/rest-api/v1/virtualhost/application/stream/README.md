---
title: Stream
description: "List and manage OvenMediaEngine streams within an application through the v1 REST API."
sidebar_position: 46
---

## Get Stream List

Get all stream names in the &#x7B;vhost name&#x7D;/&#x7B;app name&#x7D; application.

> #### Request

<details>

<summary><span class="http-method http-method-get">GET</span> /v1/vhosts/&#x7B;vhost&#x7D;/apps/&#x7B;app&#x7D;/streams</summary>

**Header**

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
```

</details>

> #### Responses

<details>

<summary><span class="http-method http-method-200">200</span> Ok</summary>

The request has succeeded

**Header**

```
Content-Type: application/json
```

**Body**

```json
{
	"statusCode": 200,
	"message": "OK",
	"response": [
		"stream",
		"stream2"
	]
}

# statusCode
	Same as HTTP Status Code
# message
	A human-readable description of the response code
# response
	Json array containing a list of stream names
```

</details>

<details>

<summary><span class="http-method http-method-401">401</span> Unauthorized</summary>

Authentication required

**Header**

```http
WWW-Authenticate: Basic realm=”OvenMediaEngine”
```

**Body**

```json
{
    "message": "[HTTP] Authorization header is required to call API (401)",
    "statusCode": 401
}
```

</details>

<details>

<summary><span class="http-method http-method-404">404</span> Not Found</summary>

The given vhost name or app name could not be found.

**Header**

```json
Content-Type: application/json
```

**Body**

```json
{
    "statusCode": 404,
    "message": "Could not find the application: [default/non-exists] (404)"
}
```

</details>

## Create Stream (Pull)

Create a stream by pulling an external URL. External URL protocols currently support RTSP and OVT.

> #### Request

<details>

<summary><span class="http-method http-method-post">POST</span> /v1/vhosts/&#x7B;vhost&#x7D;/apps/&#x7B;app&#x7D;/streams</summary>

**Header**

```http
Authorization: Basic {credentials}
Content-Type: application/json

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
```

**Body**

```json
{
	"name": "new_stream_name",
	"urls": [
		"rtsp://192.168.0.160:553/app/stream",
		"url to pull the stream from - support OVT/RTSP",
		"Only urls with the same scheme can be sent as a group."
  	],
  	"properties":{
		"persistent": false,
		"noInputFailoverTimeoutMs": 3000,
		"unusedStreamDeletionTimeoutMs": 60000,
		"ignoreRtcpSRTimestamp": false,
		"relay": false
  	}
}

# name (required)
	Stream name to create
# urls (required)
	A list of URLs to pull streams from, in Json array format. 
	All URLs must have the same scheme.
# properties (optional)
	## persistent
		Created as a persistent stream, not deleted until DELETE
	## noInputFailoverTimeoutMs
		If no data is input during this period, the stream is deleted, 
		but ignored if persistent is true
	## unusedStreamDeletionTimeoutMs
		If no data is output during this period (if there is no viewer), 
		the stream is deleted, but ignored if persistent is true
	## ignoreRtcpSRTimestamp
		No waits RTCP SR and start stream immediately
	## relay
		If true, the pulled stream is registered as a Relay (passthrough) 
		instead of a transcoded Source. Useful for OVT upstreams that 
		already expose multiple renditions, so tracks pass through without 
		per-profile transcoding. Defaults to false.
```

</details>

> #### Responses

<details>

<summary><span class="http-method http-method-201">201</span> Created</summary>

A stream has been created.

**Header**

```http
Content-Type: application/json
```

**Body**

```json
{
    "message": "Created",
    "statusCode": 201
}

# statusCode
    Same as HTTP Status Code
# message
    A human-readable description of the response code
```

</details>

<details>

<summary><span class="http-method http-method-400">400</span> Bad Request</summary>

Invalid request. Body is not a Json Object or does not have a required value

</details>

<details>

<summary><span class="http-method http-method-401">401</span> Unauthorized</summary>

Authentication required

**Header**

```http
WWW-Authenticate: Basic realm=”OvenMediaEngine”
```

**Body**

```json
{
    "message": "[HTTP] Authorization header is required to call API (401)",
    "statusCode": 401
}
```

</details>

<details>

<summary><span class="http-method http-method-404">404</span> Not Found</summary>

The given vhost name or app name could not be found.

**Body**

```json
{
    "statusCode": 404,
    "message": "Could not find the application: [default/non-exists] (404)"
}
```

</details>

<details>

<summary><span class="http-method http-method-409">409</span> Conflict</summary>

A stream with the same name already exists

</details>

<details>

<summary><span class="http-method http-method-502">502</span> Bad Gateway</summary>

Failed to pull provided URL

</details>

<details>

<summary><span class="http-method http-method-500">500</span> Internal Server Error</summary>

Unknown error

</details>

## Get Stream Info

Get detailed information of stream.

> #### Request

<details>

<summary><span class="http-method http-method-get">GET</span> /v1/vhosts/&#x7B;vhost&#x7D;/apps/&#x7B;app&#x7D;/streams/&#x7B;stream&#x7D;</summary>

**Header**

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
```

</details>

> #### Responses

<details>

<summary><span class="http-method http-method-200">200</span> Ok</summary>

The request has succeeded

**Header**

```
Content-Type: application/json
```

**Body**

```json
{
	"statusCode": 200,
	"message": "OK",
	"response": {
		"name": "stream",
		"input": {
			"createdTime": "2026-07-29T15:04:21.879+09:00",
			"sourceType": "Rtmp",
			"sourceUrl": "tcp://192.168.0.200:41008",
			"tracks": [
				{
					"id": 0,
					"name": "Video",
					"type": "Video",
					"video": {
						"bypass": false,
						"codec": "H264",
						"codecModule": "none",
						"width": 1280,
						"height": 720,
						"maxWidth": 1280,
						"maxHeight": 720,
						"bitrate": 2500000,
						"bitrateConf": 2500000,
						"bitrateAvg": 2493184,
						"bitrateLatest": 2501120,
						"framerate": 30.0,
						"framerateConf": 30.0,
						"framerateAvg": 30.0,
						"framerateLatest": 30.0,
						"maxFramerate": 30.0,
						"hasBframes": false,
						"keyFrameInterval": 30.0,
						"keyFrameIntervalConf": 0.0,
						"keyFrameIntervalAvg": 30.0,
						"keyFrameIntervalLatest": 30.0,
						"deltaFramesSinceLastKeyFrame": 12,
						"configChangeCount": 0,
						"timebase": {
							"num": 1,
							"den": 1000
						}
					}
				},
				{
					"id": 1,
					"name": "Audio",
					"type": "Audio",
					"audio": {
						"bypass": false,
						"codec": "AAC",
						"codecModule": "default",
						"samplerate": 48000,
						"channel": 2,
						"bitrate": 128000,
						"bitrateConf": 128000,
						"bitrateAvg": 127488,
						"bitrateLatest": 128512,
						"configChangeCount": 0,
						"timebase": {
							"num": 1,
							"den": 1000
						}
					}
				}
			]
		},
		"outputs": [
			{
				"name": "stream",
				"tracks": [
					{
						"id": 0,
						"name": "bypass_video",
						"type": "Video",
						"video": {
							"bypass": true,
							"codec": "H264",
							"codecStatus": "Ready",
							"width": 1280,
							"height": 720,
							"maxWidth": 1280,
							"maxHeight": 720,
							"bitrate": 2493184,
							"bitrateConf": 0,
							"bitrateAvg": 2493184,
							"bitrateLatest": 2501120,
							"framerate": 30.0,
							"framerateConf": 0.0,
							"framerateAvg": 30.0,
							"framerateLatest": 30.0,
							"maxFramerate": 30.0,
							"hasBframes": false,
							"keyFrameInterval": 30.0,
							"keyFrameIntervalConf": 0.0,
							"keyFrameIntervalAvg": 30.0,
							"keyFrameIntervalLatest": 30.0,
							"deltaFramesSinceLastKeyFrame": 7,
							"configChangeCount": 0,
							"timebase": {
								"num": 1,
								"den": 1000
							}
						}
					},
					{
						"id": 1,
						"name": "opus_audio",
						"type": "Audio",
						"audio": {
							"bypass": false,
							"codec": "OPUS",
							"codecModule": "libopus",
							"codecStatus": "Ready",
							"samplerate": 48000,
							"channel": 2,
							"bitrate": 128000,
							"bitrateConf": 128000,
							"bitrateAvg": 127232,
							"bitrateLatest": 128256,
							"configChangeCount": 0,
							"timebase": {
								"num": 1,
								"den": 48000
							}
						}
					}
				],
				"playlists": [
					{
						"name": "llhls_default",
						"fileName": "llhls",
						"options": {
							"webrtcAutoAbr": true,
							"hlsChunklistPathDepth": -1,
							"enableTsPackaging": false
						},
						"renditions": [
							{
								"name": "bypass_video_opus_audio",
								"videoVariantName": "bypass_video",
								"audioVariantName": "opus_audio"
							}
						]
					}
				]
			}
		]
	}
}


# statusCode
	Same as HTTP Status Code
# message
	A human-readable description of the response code
# response
	Details of the stream
```


```
keyFrameInterval is GOP size
```

Notes on the track fields

- `bitrate`, `framerate` and `keyFrameInterval` report the configured value when one is set, and the measured value otherwise. That configured value comes from the output profile on an encoded track, and from what the source declared on an input track. The `*Conf` fields report the configured value only, so they are 0 when nothing is configured. `keyFrameIntervalConf` is set by the output profile alone, so it stays 0 on an input track.
- The `*Avg` and `*Latest` fields, `hasBframes`, `deltaFramesSinceLastKeyFrame` and `configChangeCount` come from the runtime measurement of the track. They are omitted for a track that has no measurement.
- `lastConfigChanged` appears only when `configChangeCount` is greater than 0.
- `maxWidth`, `maxHeight` and `maxFramerate` are high water marks of the track. They never decrease while the stream lives.
- `codecModule` is the codec module in use, such as `default`, `x264`, `libopus` or `none`. It is omitted for a bypass track because no codec is involved.
- `codecStatus` is always `Ready` on a bypass track, because such a track has no codec to initialize. On other tracks it appears once the codec reports `Ready` or `Failed`.
- Values of the `framerate` and `keyFrameInterval` families are serialized from single precision floats, so a value such as 29.97 can be rendered as `29.969999313354492`. Round them on the client side instead of comparing them for equality.

</details>

<details>

<summary><span class="http-method http-method-401">401</span> Unauthorized</summary>

Authentication required

**Header**

```http
WWW-Authenticate: Basic realm=”OvenMediaEngine”
```

**Body**

```json
{
    "message": "[HTTP] Authorization header is required to call API (401)",
    "statusCode": 401
}
```

</details>

<details>

<summary><span class="http-method http-method-404">404</span> Not Found</summary>

The given virtual host, application or stream could not be found.

**Header**

```json
Content-Type: application/json
```

**Body**

```json
{
    "statusCode": 404,
    "message": "[HTTP] Could not find the stream: [default/app/stream] (404)"
}
```

The message names the object that could not be found, so it differs per case. The virtual host case reads `[HTTP] Could not find the virtual host: [default] (404)`, and the application case reads `[HTTP] Could not find the application: [default/app] (404)`.

</details>

<details>

<summary>OpenAPI Specification</summary>

OpenAPI 3.0 specification

```yaml
openapi: 3.0.0
info:
  title: Stream API
  version: 1.0.0
  description: API for stream information

servers:
  - url: http://{server}:{port}/v1
    variables:
      server:
        default: localhost
      port:
        default: '8081'

security:
  - basicAuth: []

paths:
  /vhosts/{vhost}/apps/{app}/streams/{stream}:
    get:
      summary: Get stream information
      parameters:
        - name: vhost
          in: path
          required: true
          description: The name of the virtual host.
          schema:
            type: string
        - name: app
          in: path
          required: true
          description: The name of the application.
          schema:
            type: string
        - name: stream
          in: path
          required: true
          description: The name of the stream.
          schema:
            type: string
      responses:
        '200':
          description: Successful response
          content:
            application/json:
              schema:
                $ref: '#/components/schemas/SuccessResponse'
        '401':
          description: Unauthorized
          content:
            application/json:
              schema:
                $ref: '#/components/schemas/Error401'
        '404':
          description: Not found
          content:
            application/json:
              schema:
                $ref: '#/components/schemas/Error404'

components:
  securitySchemes:
    basicAuth:
      type: http
      scheme: basic

  schemas:
    SuccessResponse:
      type: object
      required:
        - statusCode
        - message
        - response
      properties:
        statusCode:
          type: integer
        message:
          type: string
        response:
          type: object
          required:
            - name
            - input
            - outputs
          properties:
            name:
              type: string
            input:
              $ref: '#/components/schemas/Input'
            outputs:
              type: array
              description: >-
                For a source or transcoded stream, the output streams created by the output profiles,
                or an empty array until one exists. For a relay stream with no child output stream,
                the relay stream itself as the single entry.
              items:
                $ref: '#/components/schemas/Output'
    Error401:
      type: object
      required:
        - statusCode
        - message
      properties:
        statusCode:
          type: integer
          enum: [401]
        message:
          type: string
          enum: ["[HTTP] Authorization header is required to call API (401)"]
          
    Error404:
      type: object
      required:
        - statusCode
        - message
      properties:
        statusCode:
          type: integer
          enum: [404]
        message:
          type: string
          description: >-
            Names the object that could not be found, so the text differs per case.
            For example "[HTTP] Could not find the virtual host: [default] (404)",
            "[HTTP] Could not find the application: [default/app] (404)" or
            "[HTTP] Could not find the stream: [default/app/stream] (404)".
          
          
    VideoTrack:
      type: object
      required:
        - id
        - name
        - type
        - video
      properties:
        id:
          type: integer
        name:
          type: string
          description: Variant name of the track. Falls back to the media type name, such as Video or Audio, when no variant name is set.
        type:
          type: string
          enum:
            - Video
        video:
          type: object
          required:
            - bypass
            - codec
            - width
            - height
            - maxWidth
            - maxHeight
            - bitrate
            - bitrateConf
            - framerate
            - framerateConf
            - maxFramerate
            - keyFrameInterval
            - keyFrameIntervalConf
          properties:
            bypass:
              type: boolean
            codec:
              type: string
            codecModule:
              type: string
              description: Present only when bypass is false.
            codecStatus:
              type: string
              description: A bypass track always reports Ready. Other tracks omit this field until a codec reports its state.
              enum:
                - Ready
                - Failed
            language:
              type: string
            characteristics:
              type: string
            width:
              type: integer
            height:
              type: integer
            maxWidth:
              type: integer
            maxHeight:
              type: integer
            bitrate:
              type: integer
            bitrateConf:
              type: integer
            bitrateAvg:
              type: integer
            bitrateLatest:
              type: integer
            framerate:
              type: number
            framerateConf:
              type: number
            framerateAvg:
              type: number
            framerateLatest:
              type: number
            maxFramerate:
              type: number
            hasBframes:
              type: boolean
            keyFrameInterval:
              type: number
            keyFrameIntervalConf:
              type: number
            keyFrameIntervalAvg:
              type: number
            keyFrameIntervalLatest:
              type: number
            deltaFramesSinceLastKeyFrame:
              type: integer
            configChangeCount:
              type: integer
            lastConfigChanged:
              type: string
              format: date-time
              description: Present only when configChangeCount is greater than 0.
            timebase:
              $ref: '#/components/schemas/Timebase'

    AudioTrack:
      type: object
      required:
        - id
        - name
        - type
        - audio
      properties:
        id:
          type: integer
        name:
          type: string
          description: Variant name of the track. Falls back to the media type name, such as Video or Audio, when no variant name is set.
        type:
          type: string
          enum:
            - Audio
        audio:
          type: object
          required:
            - bypass
            - codec
            - samplerate
            - channel
            - bitrate
            - bitrateConf
          properties:
            bypass:
              type: boolean
            codec:
              type: string
            codecModule:
              type: string
              description: Present only when bypass is false.
            codecStatus:
              type: string
              description: A bypass track always reports Ready. Other tracks omit this field until a codec reports its state.
              enum:
                - Ready
                - Failed
            language:
              type: string
            characteristics:
              type: string
            samplerate:
              type: integer
            channel:
              type: integer
            bitrate:
              type: integer
            bitrateConf:
              type: integer
            bitrateAvg:
              type: integer
            bitrateLatest:
              type: integer
            configChangeCount:
              type: integer
            lastConfigChanged:
              type: string
              format: date-time
              description: Present only when configChangeCount is greater than 0.
            timebase:
              $ref: '#/components/schemas/Timebase'

    DataTrack:
      type: object
      required:
        - id
        - name
        - type
        - data
      properties:
        id:
          type: integer
        name:
          type: string
          description: Variant name of the track. Falls back to the media type name, such as Video or Audio, when no variant name is set.
        type:
          type: string
          enum:
            - Data
        data:
          type: object
          required:
            - codec
          properties:
            codec:
              type: string

    SubtitleTrack:
      type: object
      required:
        - id
        - name
        - type
        - subtitle
      properties:
        id:
          type: integer
        name:
          type: string
          description: Variant name of the track. Falls back to the media type name, such as Video or Audio, when no variant name is set.
        type:
          type: string
          enum:
            - Subtitle
        subtitle:
          type: object
          required:
            - codec
            - autoSelect
            - default
            - forced
          properties:
            codec:
              type: string
            codecStatus:
              type: string
              description: A bypass track always reports Ready. Other tracks omit this field until a codec reports its state.
              enum:
                - Ready
                - Failed
            language:
              type: string
            characteristics:
              type: string
            autoSelect:
              type: boolean
            default:
              type: boolean
            forced:
              type: boolean
            timebase:
              $ref: '#/components/schemas/Timebase'
            stt:
              type: object
              description: Speech to text metadata. Present only when codec is WHISPER.
              required:
                - translation
              properties:
                engine:
                  type: string
                model:
                  type: string
                sourceLanguage:
                  type: string
                translation:
                  type: boolean
                outputLabel:
                  type: string

    Timebase:
      type: object
      description: Timebase of the track. The owning object omits this field when the track has no valid timebase.
      required:
        - num
        - den
      properties:
        num:
          type: integer
        den:
          type: integer

    OtherTrack:
      type: object
      description: Track kind that carries no media payload object. Rarely produced.
      required:
        - id
        - name
        - type
      properties:
        id:
          type: integer
        name:
          type: string
          description: Variant name of the track. Falls back to the media type name, such as Video or Audio, when no variant name is set.
        type:
          type: string
          enum:
            - Attachment
            - Unknown

    Track:
      oneOf:
        - $ref: '#/components/schemas/VideoTrack'
        - $ref: '#/components/schemas/AudioTrack'
        - $ref: '#/components/schemas/DataTrack'
        - $ref: '#/components/schemas/SubtitleTrack'
        - $ref: '#/components/schemas/OtherTrack'
      discriminator:
        propertyName: type
        mapping:
          Video: '#/components/schemas/VideoTrack'
          Audio: '#/components/schemas/AudioTrack'
          Data: '#/components/schemas/DataTrack'
          Subtitle: '#/components/schemas/SubtitleTrack'
          Attachment: '#/components/schemas/OtherTrack'
          Unknown: '#/components/schemas/OtherTrack'

    Rendition:
      type: object
      properties:
        name:
          type: string
          description: Omitted when the rendition has no name.
        videoVariantName:
          type: string
          description: Omitted when the rendition has no video variant.
        audioVariantName:
          type: string
          description: Omitted when the rendition has no audio variant.

    Playlist:
      type: object
      required:
        - options
        - renditions
      properties:
        name:
          type: string
          description: Omitted when the playlist has no name.
        fileName:
          type: string
          description: Omitted when the playlist has no file name.
        options:
          type: object
          required:
            - webrtcAutoAbr
            - hlsChunklistPathDepth
            - enableTsPackaging
          properties:
            webrtcAutoAbr:
              type: boolean
            hlsChunklistPathDepth:
              type: integer
            enableTsPackaging:
              type: boolean
        renditions:
          type: array
          items:
            $ref: '#/components/schemas/Rendition'

    Output:
      type: object
      required:
        - name
        - tracks
        - playlists
      properties:
        name:
          type: string
        tracks:
          type: array
          items:
            $ref: '#/components/schemas/Track'
        playlists:
          type: array
          description: Empty when the output stream has no playlist.
          items:
            $ref: '#/components/schemas/Playlist'

    Input:
      type: object
      required:
        - createdTime
        - sourceType
        - tracks
      properties:
        createdTime:
          type: string
          format: date-time
        sourceType:
          type: string
          description: Provider that ingested this stream.
          enum:
            - WebRTC
            - Ovt
            - Rtmp
            - RtmpPull
            - Rtsp
            - RtspPull
            - Transcoder
            - SRT
            - MPEGTS
            - Scheduled
            - Multiplex
            - File
            - Unknown
        sourceUrl:
          type: string
          description: Omitted when the provider has no source address.
        tracks:
          type: array
          items:
            $ref: '#/components/schemas/Track'
```

</details>

## Delete Stream

Delete Stream. This terminates the ingress connection.


:::warning

The sender can reconnect after the connection is terminated. To prevent reconnection, you must use [AccessControl](../../../../../access-control/README.md).

:::


> #### Request

<details>

<summary><span class="http-method http-method-delete">DELETE</span> /v1/vhosts/&#x7B;vhost&#x7D;/apps/&#x7B;app&#x7D;/streams/&#x7B;stream&#x7D;</summary>

**Header**

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
```

</details>

> #### Responses

<details>

<summary><span class="http-method http-method-200">200</span> Ok</summary>

The request has succeeded

**Header**

```
Content-Type: application/json
```

**Body**

```json
{
	"statusCode": 200,
	"message": "OK"
}


# statusCode
	Same as HTTP Status Code
# message
	A human-readable description of the response code
```

</details>

<details>

<summary><span class="http-method http-method-401">401</span> Unauthorized</summary>

Authentication required

**Header**

```http
WWW-Authenticate: Basic realm=”OvenMediaEngine”
```

**Body**

```json
{
    "message": "[HTTP] Authorization header is required to call API (401)",
    "statusCode": 401
}
```

</details>

<details>

<summary><span class="http-method http-method-404">404</span> Not Found</summary>

The given vhost name or app name could not be found.

**Header**

```json
Content-Type: application/json
```

**Body**

```json
{
    "message": "[HTTP] Could not find the stream: [default/#default#app/stream] (404)",
    "statusCode": 404
}
```

</details>
