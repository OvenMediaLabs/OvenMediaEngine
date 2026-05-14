---
title: ScheduledChannel
sidebar_position: 52
---

ScheduledChannel allows you to create a live channel by scheduling pre-recorded files has been added to OvenMediaEngine. Other services or software call this Pre-recorded Live or File Live, but OvenMediaEngine plans to expand the function to organize live channels as a source, so we named it Scheduled Channel.

ScheduledChannel can be controlled by API or file. For more information about ScheduledChannel, see below.


[scheduled-channel.md](../../../../live-source/scheduled-channel.md)




The body of the API all has the same structure as the ScheduledChannel schedule file.

## Get Channel List

Get all scheduled channels in the &#x7B;vhost name&#x7D;/&#x7B;app name&#x7D; application.

> **Request**

<details>

<summary><span class="http-method http-method-get">GET</span> /v1/vhosts/&#x7B;vhost&#x7D;/apps/&#x7B;app&#x7D;/<strong>scheduledChannels</strong></summary>

**Header**

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
```

</details>

> **Responses**

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
    "message": "OK",
    "response": [
        "stream"
    ],
    "statusCode": 200
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

## Create Channel

Create a Scheduled channel.

> **Request**

<details>

<summary><span class="http-method http-method-post">POST</span> /v1/vhosts/&#x7B;vhost&#x7D;/apps/&#x7B;app&#x7D;/<strong>scheduledChannels</strong></summary>

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
    "stream": {
        "name": "stream2",
        "bypassTranscoder": false,
        "videoTrack": true,
        "audioTrack": true,
        "audioMap": [ // Optional
            {
                "name": "english",
                "language": "en",
                "characteristics": "1"
            },
            {
                "name": "Korean",
                "language": "ko",
                "characteristics": "2"
            },
            {
                "name": "Japanese",
                "language": "ja",
                "characteristics": "3"
            }
        ]
    },
    "fallbackProgram": {
        "items": [
            {
                "url": "file://video/sample.mp4",
                "start": 0,
                "duration": 60000
            }
        ]
    },
    "programs": [
        {
            "name": "1",
            "scheduled": "2023-11-13T20:57:00.000+09",
            "repeat": true,
            "items": [
                {
                    "url": "file://video/sample.mp4",
                    "start": 0,
                    "duration": 60000
                },
                {
                    "url": "file://video/1.mp4",
                    "start": 0,
                    "duration": 60000
                }
            ]
        },
        {
            "name": "2",
            "scheduled": "2023-11-14T20:57:00.000+09",
            "repeat": true,
            "items": [
                {
                    "url": "file://1.mp4",
                    "start": 0,
                    "duration": 60000
                },
                {
                    "url": "file://sample.mp4",
                    "start": 0,
                    "duration": 60000
                }
            ]
        }
    ]
}
```

</details>

> **Responses**

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

## Patch Schedule

Update the schedule. `<Stream>` cannot be PATCHed.

> ### Request

<details>

<summary><span class="http-method http-method-patch">PATCH</span> /v1/vhosts/&#x7B;vhost&#x7D;/apps/&#x7B;app&#x7D;/<strong>scheduledChannels/&#x7B;channel name&#x7D;</strong></summary>

#### **Header**

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
```

#### Body

Write the value you want to modify. However, name and outputProfiles cannot be modified.

```json
{
    "fallbackProgram": {
        "items": [
            {
                "url": "file://video/sample.mp4",
                "start": 5000,
                "duration": 30000
            }
        ]
    },
    "programs": [
        {
            "name": "1",
            "scheduled": "2023-11-10T20:57:00.000+09",
            "repeat": true,
            "items": [
                {
                    "url": "file://video/1.mp4",
                    "start": 0,
                    "duration": 60000
                }
            ]
        },
        {
            "name": "2",
            "scheduled": "2023-11-20T20:57:00.000+09",
            "repeat": true,
            "items": [
                {
                    "url": "file://video/1.mp4",
                    "start": 0,
                    "duration": 60000
                },
                {
                    "url": "file://video/sample.mp4",
                    "start": 0,
                    "duration": 60000
                }
            ]
        }
    ]
}
```

</details>

> ### Responses

<details>

<summary><span class="http-method http-method-200">200</span> Ok</summary>

The request has succeeded

#### **Header**

```
Content-Type: application/json
```

#### **Body**

```json
{
    "message": "Created",
    "statusCode": 201
}
```

</details>

<details>

<summary><span class="http-method http-method-400">400</span> Bad Request</summary>

Invalid request.&#x20;

```json
{
    "message": "[HTTP] Cannot change [name] using this API (400)",
    "statusCode": 400
}
```

</details>

<details>

<summary><span class="http-method http-method-401">401</span> Unauthorized</summary>

Authentication required

#### **Header**

```http
WWW-Authenticate: Basic realm=”OvenMediaEngine”
```

#### **Body**

```json
{
    "message": "[HTTP] Authorization header is required to call API (401)",
    "statusCode": 401
}
```

</details>

<details>

<summary><span class="http-method http-method-404">404</span> Not Found</summary>

The given vhost name or application name could not be found.

#### **Body**

```json
{
    "message": "[HTTP] Could not find the application: [default/app2] (404)",
    "statusCode": 404
}
```

</details>

## Get Channel Info

Get detailed information of scheduled channel. It also provides information about the currently playing program and item.

> **Request**

<details>

<summary><span class="http-method http-method-get">GET</span> /v1/vhosts/&#x7B;vhost&#x7D;/apps/&#x7B;app&#x7D;/<strong>scheduledChannels</strong>/&#x7B;channel name&#x7D;</summary>

**Header**

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
```

</details>

> **Responses**

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
    "message": "OK",
    "response": {
        "currentProgram": {
            "currentItem": {
                "currentPosition": 1700,
                "duration": 60000,
                "start": 0,
                "url": "file://video/1.mp4"
            },
            "duration": -1,
            "end": "2262-04-12T08:47:16.854+09:00",
            "name": "2",
            "repeat": true,
            "scheduled": "2023-11-20T20:57:00.000+09:00",
            "state": "onair"
        },
        "fallbackProgram": {
            "items": [
                {
                    "duration": -1,
                    "start": 0,
                    "url": "file://hevc.mov"
                },
                {
                    "duration": -1,
                    "start": 0,
                    "url": "file://avc.mov"
                }
            ],
            "name": "fallback",
            "repeat": true,
            "scheduled": "1970-01-01T00:00:00Z"
        },
        "programs": [
            {
                "name": "2",
                "repeat": true,
                "scheduled": "2023-11-20T20:57:00.000+09"
            }
        ],
        "stream": {
            "audioTrack": true,
            "bypassTranscoder": false,
            "name": "stream",
            "videoTrack": true
        }
    },
    "statusCode": 200
}
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
    "message": "Could not find the application or stream (404)"
}
```

</details>

## Delete Channel

Delete Scheduled Channel

> **Request**

<details>

<summary><span class="http-method http-method-delete">DELETE</span> /v1/vhosts/&#x7B;vhost&#x7D;/apps/&#x7B;app&#x7D;/<strong>scheduledChannels/&#x7B;channel name&#x7D;</strong></summary>

**Header**

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
```

</details>

> **Responses**

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
