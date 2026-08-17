---
title: Push
description: "Start and stop OvenMediaEngine push publishing over SRT, RTMP, or MPEG-2 TS through the v1 REST API."
sidebar_position: 45
---

## Start Push Publishing

Start push publishing the stream with SRT, RTMP or MPEG2-TS. If the requested stream does not exist on the server, this task is reserved. And when the stream is created, it automatically starts push publishing.

> #### Request

<details>

<summary><span class="http-method http-method-post">POST</span> /v1/vhosts/&#x7B;vhost&#x7D;/apps/&#x7B;app&#x7D;:startPush</summary>

**Header**

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
```

**Body : SRT**

```json
{
  "id": "{unique_push_id}",
  "stream": {
    "name": "{output_stream_name}",
    "variantNames": []
  },
  "protocol": "srt",
  "url": "srt://{host}[:port]?mode=caller&latency=120000&timeout=500000",
  "streamKey": ""
}

# id (required)
    unique ID to identify the task
    
# stream (required)
    ## name (required)
        output stream name
        
    ## variantNames (optional)
        Array of track names to publsh. 
        This value is Encodes.[Video|Audio|Data].Name in the OutputProfile
        setting.
        
        If empty, all tracks will be sent.

# protocol (required)
    srt
    
# url (required) 
    address of destination.
    options can be set in query-string format.
    
# streamKey (optional)
    not used with mpegts
```

In SRT Push Publisher, only the `caller` connection mode is supported.

**Body : RTMP**


```json
{
  "id": "{unique_push_id}",
  "stream": {
    "name": "{output_stream_name}",
    "variantNames": [ "h264_fhd", "aac" ]
  },
  "protocol": "rtmp",
  "url":"rtmp://{host}[:port]/{app_name}",
  "streamKey":"{stream_name}"
}

# id (required)
    unique ID to identify the task
    
# stream (required)
    ## name (required)
        output stream name
        
    ## variantNames (optional)
        Array of track names to publsh. 
        This value is Encodes.[Video|Audio|Data].Name in the OutputProfile
        setting.
        
        If empty, The first track among video tracks (by ID) and the first 
        track among audio tracks (by ID) are selected automatically.

# protocol (required)
    rtmp
    
# url (required) 
    address of destination
    
# streamKey (required)
    RTMP stream key
```


**Body : MPEG2-TS**

```json
{
  "id": "{unique_push_id}",
  "stream": {
    "name": "{output_stream_name}",
    "variantNames": []
  },
  "protocol": "mpegts",
  "url": "udp://{host}[:port]",
  "streamKey": ""
}

# id (required)
    unique ID to identify the task
    
# stream (required)
    ## name (required)
        output stream name
        
    ## variantNames (optional)
        Array of track names to publsh. 
        This value is Encodes.[Video|Audio|Data].Name in the OutputProfile
        setting.
        
        If empty, all tracks will be sent.

# protocol (required)
    mpegts
    
# url (required) 
    address of destination
    
# streamKey (optional)
    not used with mpegts
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

Please note that `responses` are incorrectly returned in Json array format for version 0.15.3 and earlier.

```json
{
    "statusCode": 200,
    "message": "OK",
    "response": {
        "id": "{unique_push_id}",
        "state": "ready",
            
        "vhost": "default",
        "app": "app",
        "stream": {
            "name": "{output_stream_name}",
            "trackIds": [],
            "variantNames": []
        },
            
        "protocol": "rtmp",
        "url": "rtmp://{host}[:port]/{app_name}",
        "streamKey": "{stream_name}",
            
        "sentBytes": 0,
        "sentTime": 0,
        "sequence": 0,
        "totalsentBytes": 0,
        "totalsentTime": 0,
            
        "createdTime": "2023-03-15T23:02:34.371+09:00",
        "startTime": "1970-01-01T09:00:00.000+09:00",
        "finishTime": "1970-01-01T09:00:00.000+09:00"
    }
}

# statusCode
    Same as HTTP Status Code
# message
    A human-readable description of the response code
# response
    Created push publishing task information
```

</details>

<details>

<summary><span class="http-method http-method-400">400</span> Bad Request</summary>

Invalid request.

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

The given vhost or application name could not be found.

**Body**

```json
{
    "message": "[HTTP] Could not find the application: [vhost/app1] (404)",
    "statusCode": 404
}
```

</details>

<details>

<summary><span class="http-method http-method-409">409</span> Conflict</summary>

duplicate ID

</details>

## Stop Push Publishing

> #### Request

<details>

<summary><span class="http-method http-method-post">POST</span> /v1/vhosts/&#x7B;vhost&#x7D;/apps/&#x7B;app&#x7D;:stopPush</summary>

**Header**

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
```

**Body**


```json
{
    "id": "{unique_push_id}"
}

# id (required)
    unique ID to identify the push publishing task
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
}

# statusCode
	Same as HTTP Status Code
# message
	A human-readable description of the response code
```

</details>

<details>

<summary><span class="http-method http-method-400">400</span> Bad Request</summary>

Invalid request.

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

The given vhost/application name or id of recording task could not be found.

**Body**

```json
{
    "message": "[HTTP] Could not find the application: [vhost/app1] (404)",
    "statusCode": 404
}
```

</details>

## Get Push Publishing State

> #### Request

<details>

<summary><span class="http-method http-method-post">POST</span> /v1/vhosts/&#x7B;vhost&#x7D;/apps/&#x7B;app&#x7D;:pushes</summary>

**Header**

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
```

**Body**


```json
{
    "id": "{unique_push_id}"
}

# id (optional)
    unique ID to identify the push publishing task. If no id is given in the request, the full list is returned.
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

The `response` is Json array format.

```json
{
    "statusCode": 200,
    "message": "OK",
    "response": [
        {
            "id": "{unique_push_id}",
            "state": "started",
            
            "vhost": "default",
            "app": "app",
            "stream": {
                "name": "{output_stream_name}",
                "trackIds": [],
                "variantNames": []
            },
            
            "protocol": "rtmp",
            "url": "rtmp://{host}[:port]/{app_name}",
            "streamKey": "{stream_name}",
            
            "sentBytes": 0,
            "sentTime": 0,
            "sequence": 0,
            "totalsentBytes": 0,
            "totalsentTime": 0,
            
            "createdTime": "2023-03-15T23:02:34.371+09:00",
            "startTime": "1970-01-01T09:00:00.000+09:00",
            "finishTime": "1970-01-01T09:00:00.000+09:00"
        },
        {
            "id": "4",
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

The given vhost or application name could not be found.

**Body**

```json
{
    "message": "[HTTP] Could not find the application: [vhost/app1] (404)",
    "statusCode": 404
}
```

</details>

## State of Push Publishing

The Push Publishing task has the state shown in the table below. You can get the `state` in the Start Push Publishing and Get Push Publishing State API response.

| State      | Description 						 |
| ---------- | ----------------------------------------------------------|
| ready      | Waiting for the stream to be created. 			 |
| connecting | Connecting to destination			         |
| pushing    | Connected and streaming                                   | 
| stopping   | Disconnection / stop in progress                          |
| stopped    | Push is disconnected / stopped                            |
| error      | Push encountered an error                                 |
