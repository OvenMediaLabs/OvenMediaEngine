# Send Event

It allows you to insert events into streams. Right now events only support the ID3v2 format and only the LLHLS publisher handles it. Events delivered to LLHLS Publisher are inserted as emsg boxes within the m4s container.

> ### Request

<details>

<summary><mark style="color:blue;">POST</mark> v1/vhosts/{vhost}/apps/{app}/streams/{stream}:sendEvent</summary>

#### Header

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
```

#### Body

```json
{
  "eventFormat": "id3v2",
  "eventType": "video",
  "events":[
      {
        "frameType": "TXXX",
        "info": "AirenSoft",
        "data": "OvenMediaEngine"
      },
      {
        "frameType": "TIT2",
        "data": "OvenMediaEngine 123"
      }
  ]
}

# eventFormat
  Currently only id3v2 is supported.
# eventType (Optional, Default : event)
  Select one of event, video, and audio. event inserts an event into every track. 
  video inserts events only on tracks of video type. 
  audio inserts events only on tracks of audio type.
# events
  It accepts only Json array format and can contain multiple events.
 
  ## frameType
    Currently, only TXXX and T??? (Text Information Frames, e.g. TIT2) are supported.
 ## info
    This field is used only in TXXX and is entered in the Description field of TXXX.
 ## data
    If the frameType is TXXX, it is entered in the Value field, 
    and if the frameType is "T???", it is entered in the Information field.
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
