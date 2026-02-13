# Alert

## Overview

Alert is a module that can detect anomalies and patterns of interest in a stream or system and send notifications to users. Anomalies and patterns of interest can be set through predefined , and when detected, the module sends an HTTP(S) request to the user's notification server.

## Configuration

Alert can be set up on `<Server>`, as shown below.

```xml
<Server version="8">
	<Alert>
		<Url>http://192.168.0.161:9595/alert/notification</Url>
		<SecretKey>1234</SecretKey>
		<Timeout>3000</Timeout>
		<Rules>
			<Ingress>
				<MinBitrate>2000000</MinBitrate>
				<MaxBitrate>4000000</MaxBitrate>
				<MinFramerate>15</MinFramerate>
				<MaxFramerate>60</MaxFramerate>
				<MinWidth>1280</MinWidth>
				<MinHeight>720</MinHeight>
				<MaxWidth>1920</MaxWidth>
				<MaxHeight>1080</MaxHeight>
				<MinSamplerate>16000</MinSamplerate>
				<MaxSamplerate>50400</MaxSamplerate>
				<LongKeyFrameInterval />
				<HasBFrames />
			</Ingress>
		</Rules>
	</Alert>
</Server>
```

| Key       | Description                                                                                                                  |
| --------- | ---------------------------------------------------------------------------------------------------------------------------- |
| Url       | The HTTP Server to receive the notification. HTTP and HTTPS are available.                                                   |
| Secretkey | The secret key used when encrypting with HMAC-SHA1<br />For more information, see <a href="alert.md#security">Security</a>.  |
| Timeout   | Time to wait for a response after request. (in milliseconds)                                                                 |
| Rules     | Defines anomalies and patterns of interest to be detected.                                                                   |

### Rules

| Key     |                      | Description                                                                           |
| ------- | -------------------- | ------------------------------------------------------------------------------------- |
| Ingress | MinBitrate           | Detects when the ingress stream's bitrate is lower than the set value.                |
|         | MaxBitrate           | Detects when the ingress stream's bitrate is greater than the set value.              |
|         | MinFramerate         | Detects when the ingress stream's framerate is lower than the set value.              |
|         | MaxFramerate         | Detects when the ingress stream's framerate is greater than the set value.            |
|         | MinWidth             | Detects when the ingress stream's width is lower than the set value.                  |
|         | MaxWidth             | Detects when the ingress stream's width is greater than the set value.                |
|         | MinHeight            | Detects when the ingress stream's height is lower than the set value.                 |
|         | MaxHeight            | Detects when the ingress stream's height is greater than the set value.               |
|         | MinSamplerate        | Detects when the ingress stream's samplerate is lower than the set value.             |
|         | MaxSamplerate        | Detects when the ingress stream's samplerate is greater than the set value.           |
|         | LongKeyFrameInterval | Detects when the ingress stream's keyframe interval is too long (exceeds 4 seconds).  |
|         | HasBFrames           | Detects when there are B-frames in the ingress stream.                                |

## Notification

### Request

#### Format

```http
POST /configured/target/url HTTP/1.1
Content-Length: 1037
Content-Type: application/json
Accept: application/json
X-OME-Signature: f871jd991jj1929jsjd91pqa0amm1
{
	"sourceUri":"#default#app/stream",
	"messages":[
		{
			"code":"INGRESS_HAS_BFRAME",
			"description":"There are B-Frames in the ingress stream."
		},
		{
			"code":"INGRESS_BITRATE_LOW",
			"description":"The ingress stream's current bitrate (316228 bps) is lower than the configured bitrate (2000000 bps)"
		}
	],
	"sourceInfo":{
		"createdTime":"2023-04-07T21:15:24.487+09:00",
		"sourceType":"Rtmp",
		"sourceUrl":"TCP://192.168.0.220:10639",
		"tracks":[
			{
				"id":0,
				"name":"Video",
				"type":"Video",
				"video":{
					"bitrate":300000,
					"bypass":false,
					"codec":"H264",
					"framerate":30.0,
					"hasBframes":true,
					"height":1080,
					"keyFrameInterval":0,
					"width":1920
				}
			},
			{
				"audio":{
					"bitrate":160000,
					"bypass":false,
					"channel":1,
					"codec":"AAC",
					"samplerate":48000
				},
				"id":1,
				"name":"Audio",
				"type":"Audio"
			},
			{
				"id":2,
				"name":"Data",
				"type":"Data"
			}
		]
	},
	"type":"INGRESS"
}
```

Here is a detailed explanation of each element of JSON payload:

| Element    | Description                                                                                                                                                                       |
| ---------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| sourceUri  | URI information of the detected source.<br />`INGRESS`: #&#x3C;vhost>#&#x3C;application>/&#x3C;input_stream>                                                                      |
| messages   | List of messages detected by the Rules.                                                                                                                                           |
| sourceInfo | Detailed information about the source at the time of detection. It is identical to the response of the REST API's source information query for the detected source.               |
| type       | It represents the format of the JSON payload. The information of the JSON elements can vary depending on the value of the type.                                                   |

#### Messages

| Type    | Code                                               | Description                                                                                                                      |
| ------- | -------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------- |
| INGRESS | INGRESS\_BITRATE\_LOW                              | The ingress stream's current bitrate (`%d` bps) is lower than the configured bitrate (`%d` bps)                                  |
|         | INGRESS\_BITRATE\_HIGH                             | The ingress stream's current bitrate (`%d` bps) is higher than the configured bitrate (`%d` bps)                                 |
|         | INGRESS\_FRAMERATE\_LOW                            | The ingress stream's current framerate (`%.2f` fps) is lower than the configured framerate (`%.2f` fps)                          |
|         | INGRESS\_FRAMERATE\_HIGH                           | The ingress stream's current framerate (`%f` fps) is higher than the configured framerate (`%f` fps)                             |
|         | INGRESS\_WIDTH\_SMALL                              | The ingress stream's width (`%d`) is smaller than the configured width (`%d`)                                                    |
|         | INGRESS\_WIDTH\_LARGE                              | The ingress stream's width (`%d`) is larger than the configured width (`%d`)                                                     |
|         | INGRESS\_HEIGHT\_SMALL                             | The ingress stream's height (`%d`) is smaller than the configured height (`%d`)                                                  |
|         | INGRESS\_HEIGHT\_LARGE                             | The ingress stream's height (`%d`) is larger than the configured height (`%d`)                                                   |
|         | INGRESS\_SAMPLERATE\_LOW                           | The ingress stream's current samplerate (`%d`) is lower than the configured samplerate (`%d`)                                    |
|         | INGRESS\_SAMPLERATE\_HIGH                          | The ingress stream's current samplerate (`%d`) is higher than the configured samplerate (`%d`)                                   |
|         | INGRESS\_LONG\_KEY\_FRAME\_INTERVAL                | The ingress stream's current keyframe interval (`%.1f` seconds) is too long. Please use a keyframe interval of 4 seconds or less |
|         | INGRESS\_HAS\_BFRAME                               | There are B-Frames in the ingress stream                                                                                         |

#### Security

The control server may need to validate incoming http requests for security reasons. To do this, the Alert module puts the `X-OME-Signature` value in the HTTP request header. `X-OME-Signature` is a base64 url safe encoded value obtained by encrypting the payload of an HTTP request with the HMAC-SHA1 algorithm using the secret key set in `<Alert><SecretKey>` of the configuration.

### Response

The engine in the closing state does not need any parameter in response. The response payload is ignored.

```http
HTTP/1.1 200 OK
Content-Length: 0
Connection: Closed
```
