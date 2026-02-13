# Record

## Start Recording

Start recording the stream. If the requested stream does not exist on the server, this recording task is reserved. And when the stream is created, it automatically starts recording.

> ### Request

<details>

<summary><mark style="color:blue;">POST</mark> /v1/vhosts/{vhost}/apps/{app}:startRecord</summary>

#### **Header**

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
```

#### Body :  Single file recording&#x20;

#### &#x20;    {

{% code overflow="wrap" %}
```json
    "id": "{unique_record_id}",
    "stream": {
        "name": "{output_stream_name}",
        "variantNames": []
    }
}

# id (required)
    unique ID to identify the recording task
    
# stream (required)
    ## name (required)
        output stream name
        
    ## variantNames (optional)
        Array of track names to record. If empty, all tracks will be 
        recorded. This value is Encodes.[Video|Audio|Data].Name in the
        OutputProfile setting.
```
{% endcode %}

#### Body : Interval based split recording

```json
{
  "id": "{unique_record_id}",
  "stream": {
    "name": "{output_stream_name}"
  },
  "interval": 60000,
  "segmentationRule": "discontinuity"
}

# interval (optional)
    Recording time per file (milliseconds). Not allowed to use with schedule
    
# segmentationRule (optional)
    Define the policy for continuously or discontinuously timestamp 
    in divided recorded files.
    
    continuity : timestamp of recorded files is continuous
    discontinuity(default) : timestamp starts anew for each recorded file
```

#### Body : Schedule based split recording

```json
{
  "id": "{unique_record_id}",
  "stream": {
    "name": "{output_stream_name}"
  },
  "schedule" : "0 */1 *"
  "segmentationRule": "continuity"
}

# schedule (optional)
    <Second Minute Hour> format, same as crontab syntax
    "10 */1 *" means to output the recorded file every 10 minutes of the hour
    Not allowed to use with schedule
```

#### Body : Recoding file path setting

```json
{
  "id": "{unique_record_id}",
  "stream": {
    "name": "{output_stream_name}"
  },
  "filePath" : "/path/to/file_name.mp4"
  "infoPath": "/path/to/file_name.xml"
}

# filePath(optional)
    The path for recorded files can be specified. 
    Please refer to the Recording Guide for details on using macros

# infoPath(optional)
    The path for recorded infomation file can be specified. 
    Please refer to the Recording Guide for details on using macros
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

Please note that `responses` are incorrectly returned in Json array format for version 0.15.3 and earlier.

```json
{
    "statusCode": 200,
    "message": "OK",
    "response": {
        "id": "2",
        "state": "ready",
        "vhost": "default",
        "app": "app",
        "stream": {
            "name": "stream",
            "trackIds": [],
            "variantNames": []
        },
        "interval": 60000,
        "segmentationRule": "discontinuity",
        "createdTime": "2023-03-15T21:15:20.113+09:00",
    }
}

# statusCode
	Same as HTTP Status Code
# message
	A human-readable description of the response code
# response
	Created recording task information
```

</details>

<details>

<summary><mark style="color:red;">400</mark> Bad Request</summary>

Invalid request.

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

The given vhost or application name could not be found.

#### **Body**

```json
{
    "message": "[HTTP] Could not find the application: [vhost/app1] (404)",
    "statusCode": 404
}
```

</details>

<details>

<summary><mark style="color:red;">409</mark> Conflict</summary>

duplicate ID

</details>

## Stop Recording

> ### Request

<details>

<summary><mark style="color:blue;">POST</mark> /v1/vhosts/{vhost}/apps/{app}:stopRecord</summary>

#### **Header**

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
```

#### Body&#x20;

{% code overflow="wrap" %}
```json
{
    "id": "{unique_record_id}"
}

# id (required)
    unique ID to identify the recording task
```
{% endcode %}

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
```

</details>

<details>

<summary><mark style="color:red;">400</mark> Bad Request</summary>

Invalid request.

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

The given vhost/application name or id of recording task could not be found.

#### **Body**

```json
{
    "message": "[HTTP] Could not find the application: [vhost/app1] (404)",
    "statusCode": 404
}
```

</details>

## Get Recording State

> ### Request

<details>

<summary><mark style="color:blue;">POST</mark> /v1/vhosts/{vhost}/apps/{app}:records</summary>

#### **Header**

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
```

#### Body&#x20;

{% code overflow="wrap" %}
```json
{
    "id": "{unique_record_id}"
}

# id (optional)
    unique ID to identify the recording task. If no id is given in the request, the full list is returned.
```
{% endcode %}

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

The `response` is <mark style="color:green;">Json array</mark> format.

```json
{
    "statusCode": 200,
    "message": "OK",
    "response": [
        {
            "id": "2",
            "state": "recording",
            "vhost": "default",
            "app": "app",
            "stream": {
                "name": "stream",
                "trackIds": [],
                "variantNames": []
            },
            "interval": 60000,
            "segmentationRule": "discontinuity",
            "createdTime": "2023-03-15T21:15:20.113+09:00",
        },
        {
            "id": "3",
            ...
        }
    ]
}

# statusCode
	Same as HTTP Status Code
# message
	A human-readable description of the response code
# response
	Information of recording tasks. If there is no recording task, 
	response with empty array ("response": [])
```

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

The given vhost or application name could not be found.

#### **Body**

```json
{
    "message": "[HTTP] Could not find the application: [vhost/app1] (404)",
    "statusCode": 404
}
```

</details>

## State of Recording

The Recording task has the state shown in the table below. You can get the `state` in the Start Recording and Get Recording State API response.

<table data-header-hidden><thead><tr><th width="157"></th><th></th></tr></thead><tbody><tr><td>Ready</td><td>Preparing to start or waiting for the stream to be created.</td></tr><tr><td>Started</td><td>In Progress</td></tr><tr><td>Stopping</td><td>Is stopping</td></tr><tr><td>Stopped</td><td>Stopped</td></tr><tr><td>Error</td><td>Error</td></tr></tbody></table>
