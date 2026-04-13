# STT Control

These APIs pause and resume real-time Speech-to-Text (STT) inference for a specific stream at runtime, without restarting the server or recreating the stream.

For full STT configuration, see [Realtime Speech-to-Text](../../../../../subtitles/realtime-speech-to-text.md).

## Enable STT

Resumes STT inference for the stream. Audio frames are passed to the Whisper model and subtitle cues are generated again.

> ### Request

<details>

<summary><mark style="color:blue;">POST</mark> v1/vhosts/{vhost}/apps/{app}/streams/{stream}:enableStt</summary>

#### Header

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
```

#### Body

```json
{}
```

</details>

> ### Responses

<details>

<summary><mark style="color:blue;">200</mark> Ok</summary>

The request has succeeded

#### **Header**

```
Content-Type: application/json
```

#### **Body**

```json
{
    "statusCode": 200,
    "message": "OK",
    "response": {
        "enabled": true
    }
}

# statusCode
    Same as HTTP Status Code
# message
    A human-readable description of the response code
# response.enabled
    true if STT is now running
```

</details>

<details>

<summary><mark style="color:red;">401</mark> Unauthorized</summary>

Authentication required

#### **Header**

```http
WWW-Authenticate: Basic realm="OvenMediaEngine"
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

<summary><mark style="color:red;">404</mark> Not Found</summary>

The given vhost, app, or stream name could not be found, or no STT encoder exists for this stream.

#### **Body**

```json
{
    "statusCode": 404,
    "message": "Could not find STT encoder for stream: [default/app/stream]"
}
```

</details>

<details>

<summary><mark style="color:red;">503</mark> Service Unavailable</summary>

The Transcoder module is not running.

</details>

## Disable STT

Pauses STT inference. Audio frames are dropped without processing, and subtitle renditions remain in the stream but stop receiving new cues. The underlying STT model and GPU allocations remain loaded.

> ### Request

<details>

<summary><mark style="color:blue;">POST</mark> v1/vhosts/{vhost}/apps/{app}/streams/{stream}:disableStt</summary>

#### Header

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
```

#### Body

```json
{}
```

</details>

> ### Responses

<details>

<summary><mark style="color:blue;">200</mark> Ok</summary>

The request has succeeded

#### **Header**

```
Content-Type: application/json
```

#### **Body**

```json
{
    "statusCode": 200,
    "message": "OK",
    "response": {
        "enabled": false
    }
}

# statusCode
    Same as HTTP Status Code
# message
    A human-readable description of the response code
# response.enabled
    false if STT is now paused
```

</details>

<details>

<summary><mark style="color:red;">401</mark> Unauthorized</summary>

Authentication required

#### **Header**

```http
WWW-Authenticate: Basic realm="OvenMediaEngine"
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

<summary><mark style="color:red;">404</mark> Not Found</summary>

The given vhost, app, or stream name could not be found, or no STT encoder exists for this stream.

#### **Body**

```json
{
    "statusCode": 404,
    "message": "Could not find STT encoder for stream: [default/app/stream]"
}
```

</details>

<details>

<summary><mark style="color:red;">503</mark> Service Unavailable</summary>

The Transcoder module is not running.

</details>

## Get STT Status

Returns the current enabled state and configuration of all active STT renditions for the stream.

> ### Request

<details>

<summary><mark style="color:blue;">POST</mark> v1/vhosts/{vhost}/apps/{app}/streams/{stream}:sttStatus</summary>

#### Header

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
```

#### Body

```json
{}
```

</details>

> ### Responses

<details>

<summary><mark style="color:blue;">200</mark> Ok</summary>

The request has succeeded

#### **Header**

```
Content-Type: application/json
```

#### **Body**

```json
{
    "statusCode": 200,
    "message": "OK",
    "response": {
        "enabled": true,
        "renditions": [
            {
                "codec": "WHISPER",
                "label": "Korean",
                "model": "whisper_model/ggml-small.bin",
                "language": "auto",
                "translation": "false"
            },
            {
                "codec": "WHISPER",
                "label": "English",
                "model": "whisper_model/ggml-small.bin",
                "language": "auto",
                "translation": "true"
            }
        ]
    }
}

# statusCode
    Same as HTTP Status Code
# message
    A human-readable description of the response code
# response.enabled
    true if STT inference is currently running, false if paused
# response.renditions
    List of active STT encoder instances for this stream
    ## codec
        STT engine in use. Currently always "whisper".
    ## label
        Subtitle rendition label this instance writes to.
    ## model
        Model file path used by this instance.
    ## language
        Configured source language ("auto" or a language code).
    ## translation
        "true" if translation to English is enabled.
```

</details>

<details>

<summary><mark style="color:red;">401</mark> Unauthorized</summary>

Authentication required

#### **Header**

```http
WWW-Authenticate: Basic realm="OvenMediaEngine"
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

<summary><mark style="color:red;">503</mark> Service Unavailable</summary>

The Transcoder module is not running.

</details>
