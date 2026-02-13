# Send Subtitles

Using this API, you can insert subtitles into a stream in real time. By specifying the label of the subtitle track defined in the [application config](../../../../../subtitles/), you can insert multiple subtitles at once.

> ### Request

<details>

<summary><mark style="color:blue;">POST</mark> v1/vhosts/{vhost}/apps/{app}/streams/{stream}:sendSubtitles</summary>

#### Header

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
```

#### Body

```json
{
  "format": "webvtt",
  "data": [
    {
      "label": "Korean", // Required
      "subtitles": [
        { 
            "startOffset": 0, // Optional, current + offset
            "durationMs": 3000, // Optional, default: 1000
            "settings": "line:80%", // Optional
            "text": "안녕 OvenMediaEngine의 자막" // Required
        }
      ]
    },
    {
      "label": "English", // Required
      "subtitles": [
        { 
            "startOffset": 0, // Optional, current + offset
            "durationMs": 3000, // Optional, default: 1000
            "settings": "line:80%", // Optional
            "text": "Hello OvenMediaEngine Subtitles" // Required
        }
      ]
    }
  ]
}

# format: Currently only "webvtt" is supported.
# data: An array containing subtitle data grouped by label.
# label: The label of the subtitle track defined in the application config. 
(Required)
# subtitles : An array of subtitle cues. You can insert multiple cues at once. 
If the first cue has startOffset set to 0, it will be inserted immediately. 
Each following cue will be inserted after the previous one finishes. (Required)
# startOffset: How long (in ms) from the current time to delay the start of the 
subtitle. Negative values are not allowed. (Optional, default: 0)
# durationMs: How long (in ms) the subtitle will be displayed. 
(Optional, default: 1000)
# settings: A WebVTT settings string to specify position or style of the 
subtitle. (Optional)
# text: The actual subtitle text. (Required)
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

<summary><mark style="color:red;">400</mark> Bad Request</summary>

Invalid request. Body is not a Json Object or does not have a required value

</details>

<details>

<summary><mark style="color:red;">401</mark> Unauthorized</summary>

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

<summary><mark style="color:red;">404</mark> Not Found</summary>

The given vhost name or app name could not be found.

#### **Body**

```json
{
    "statusCode": 404,
    "message": "Could not find the application: [default/non-exists] (404)"
}
```

</details>
