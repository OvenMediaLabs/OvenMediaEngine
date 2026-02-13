# MultiplexChannel

Using MultiplexChannel, you can combine multiple internal streams into one ABR stream, or duplicate the stream and send it to another application.

MultiplexChannel can be controlled by API or file. See below for more information about MultiplexChannel.

{% content-ref url="../../../../live-source/multiplex-channel.md" %}
[multiplex-channel.md](../../../../live-source/multiplex-channel.md)
{% endcontent-ref %}



The body of the API all has the same structure as the mux file.

## Get Channel List

Get all multiplex channels in the {vhost name}/{app name} application.

> **Request**

<details>

<summary><mark style="color:blue;">GET</mark> /v1/vhosts/{vhost}/apps/{app}/multiplex<strong>Channels</strong></summary>

**Header**

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
```

</details>

> **Responses**

<details>

<summary><mark style="color:blue;">200</mark> Ok</summary>

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

<summary><mark style="color:red;">401</mark> Unauthorized</summary>

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

<summary><mark style="color:red;">404</mark> Not Found</summary>

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

Create a multiplex channel.

> **Request**

<details>

<summary><mark style="color:blue;">POST</mark> /v1/vhosts/{vhost}/apps/{app}/multiplex<strong>Channels</strong></summary>

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
  "outputStream": {
    "name": "stream"
  },
  "sourceStreams": [
    {
      "name": "input1",
      "url": "stream://default/app/input1",
      "trackMap": [
        {
          "sourceTrackName": "bypass_video",
          "newTrackName": "input1_video",
          "bitrateConf": 5000000,
          "framerateConf": 30
        },
        {
          "sourceTrackName": "bypass_audio",
          "newTrackName": "input1_audio",
          "bitrateConf": 128000
        }
      ]
    },
    {
      "name": "input2",
      "url": "stream://default/app/input2",
      "trackMap": [
        {
          "sourceTrackName": "bypass_video",
          "newTrackName": "input2_video",
          "bitrateConf": 1000000,
          "framerateConf": 30
        },
        {
          "sourceTrackName": "bypass_audio",
          "newTrackName": "input2_audio",
          "bitrateConf": 128000
        }
      ]
    }
  ],
  "playlists": [
    {
      "name": "LLHLS ABR",
      "fileName": "abr",
      "options": {
        "webrtcAutoAbr": true,
        "hlsChunklistPathDepth": 0
      },
      "renditions": [
        {
          "name": "input1",
          "video": "input1_video",
          "audio": "input1_audio"
        },
        {
          "name": "input2",
          "video": "input2_video",
          "audio": "input2_audio"
        }
      ]
    }
  ]
}
```

</details>

> **Responses**

<details>

<summary><mark style="color:blue;">201</mark> Created</summary>

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

<summary><mark style="color:red;">400</mark> Bad Request</summary>

Invalid request. Body is not a Json Object or does not have a required value

</details>

<details>

<summary><mark style="color:red;">401</mark> Unauthorized</summary>

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

<summary><mark style="color:red;">404</mark> Not Found</summary>

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

<summary><mark style="color:red;">409</mark> Conflict</summary>

A stream with the same name already exists

</details>

<details>

<summary><mark style="color:red;">502</mark> Bad Gateway</summary>

Failed to pull provided URL

</details>

<details>

<summary><mark style="color:red;">500</mark> Internal Server Error</summary>

Unknown error

</details>

## Get Channel Info

Get detailed information of multiplex channel. It also provides information about the currently playing program and item.

> **Request**

<details>

<summary><mark style="color:blue;">GET</mark> /v1/vhosts/{vhost}/apps/{app}/multiplex<strong>Channels</strong>/{channel name}</summary>

**Header**

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
```

</details>

> **Responses**

<details>

<summary><mark style="color:blue;">200</mark> Ok</summary>

The request has succeeded

**Header**

```
Content-Type: application/json
```

**Body**

```json
{
    "message": "OK",
    "statusCode": 200,
    "response": {
        "state": "Pulling",
        "pullingMessage": "Multiplex Channel : #default#app/stream: Wait for stream input1",
        "outputStream": {
            "name": "stream"
        },
        "playlists": [
            {
                "fileName": "abr",
                "name": "LLHLS ABR",
                "options": {
                    "hlsChunklistPathDepth": 0,
                    "webrtcAutoAbr": true
                },
                "renditions": [
                    {
                        "audio": "input1_audio",
                        "name": "input1",
                        "video": "input1_video"
                    },
                    {
                        "audio": "input2_audio",
                        "name": "input2",
                        "video": "input2_video"
                    }
                ]
            }
        ],
        "sourceStreams": [
            {
                "name": "input1",
                "trackMap": [
                    {
                        "bitrateConf": 128000,
                        "newTrackName": "input1_audio",
                        "sourceTrackName": "bypass_audio"
                    },
                    {
                        "bitrateConf": 5000000,
                        "framerateConf": 30,
                        "newTrackName": "input1_video",
                        "sourceTrackName": "bypass_video"
                    }
                ],
                "url": "stream://default/app/input1"
            },
            {
                "name": "input2",
                "trackMap": [
                    {
                        "bitrateConf": 128000,
                        "newTrackName": "input2_audio",
                        "sourceTrackName": "bypass_audio"
                    },
                    {
                        "bitrateConf": 1000000,
                        "framerateConf": 30,
                        "newTrackName": "input2_video",
                        "sourceTrackName": "bypass_video"
                    }
                ],
                "url": "stream://default/app/input2"
            }
        ]
    }
}
```

</details>

<details>

<summary><mark style="color:red;">401</mark> Unauthorized</summary>

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

<summary><mark style="color:red;">404</mark> Not Found</summary>

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

Delete Multiplex Channel

> **Request**

<details>

<summary><mark style="color:blue;">DELETE</mark> /v1/vhosts/{vhost}/apps/{app}/multiplex<strong>Channels/{channel name}</strong></summary>

**Header**

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
```

</details>

> **Responses**

<details>

<summary><mark style="color:blue;">200</mark> Ok</summary>

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

<summary><mark style="color:red;">401</mark> Unauthorized</summary>

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

<summary><mark style="color:red;">404</mark> Not Found</summary>

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
