# Current

Provides statistics of virtual host, application, and stream.

## Get Statistics of Virtual Host

> **Request**

<details>

<summary><mark style="color:blue;">GET</mark> /v1/stats/current/vhosts/{vhost}</summary>

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
    "response": {
        "connections": {
            "file": 0,
            "hlsv3": 0,
            "llhls": 0,
            "ovt": 0,
            "push": 0,
            "srt": 0,
            "thumbnail": 0,
            "webrtc": 0
        },
        "createdTime": "2023-03-15T19:46:13.728+09:00",
        "lastRecvTime": "2023-03-15T19:46:13.728+09:00",
        "lastSentTime": "2023-03-15T19:46:13.728+09:00",
        "lastUpdatedTime": "2023-03-15T19:46:13.728+09:00",
        "lastThroughputIn": 0,
        "lastThroughputOut": 0,
        "maxTotalConnectionTime": "2023-03-15T19:46:13.728+09:00",
        "maxTotalConnections": 0,
        "totalBytesIn": 0,
        "totalBytesOut": 0,
        "totalConnections": 0,
        "avgThroughputIn": 0,
        "avgThroughputOut": 0,        
        "maxThroughputIn": 0,
        "maxThroughputOut": 0
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

The given vhost name could not be found.

**Body**

```json
{
    "message": "[HTTP] Could not find the virtual host: [default1] (404)",
    "statusCode": 404
}
```

</details>

## Get Statistics of Application

> **Request**

<details>

<summary><mark style="color:blue;">GET</mark> /v1/stats/current/vhosts/{vhost}/apps/{app}</summary>

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
    "response": {
        "connections": {
            "file": 0,
            "hlsv3": 0,
            "llhls": 0,
            "ovt": 0,
            "push": 0,
            "srt": 0,
            "thumbnail": 0,
            "webrtc": 0
        },
        "createdTime": "2023-03-15T19:46:13.728+09:00",
        "lastRecvTime": "2023-03-15T19:46:13.728+09:00",
        "lastSentTime": "2023-03-15T19:46:13.728+09:00",
        "lastUpdatedTime": "2023-03-15T19:46:13.728+09:00",
        "lastThroughputIn": 0,
        "lastThroughputOut": 0,
        "maxTotalConnectionTime": "2023-03-15T19:46:13.728+09:00",
        "maxTotalConnections": 0,
        "totalBytesIn": 0,
        "totalBytesOut": 0,
        "totalConnections": 0,
        "avgThroughputIn": 0,
        "avgThroughputOut": 0,        
        "maxThroughputIn": 0,
        "maxThroughputOut": 0   
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

The given vhost or application name could not be found.

**Body**

```json
{
    "message": "[HTTP] Could not find the application: [default/app1] (404)",
    "statusCode": 404
}
```

</details>

## Get Statistics of Stream

> **Request**

<details>

<summary><mark style="color:blue;">GET</mark> /v1/stats/current/vhosts/{vhost}/apps/{app}/streams/{stream}</summary>

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
    "response": {
        "connections": {
            "file": 0,
            "hlsv3": 0,
            "llhls": 0,
            "ovt": 0,
            "push": 0,
            "srt": 0,
            "thumbnail": 0,
            "webrtc": 0
        },
        "createdTime": "2023-03-15T19:46:13.728+09:00",
        "lastRecvTime": "2023-03-15T19:46:13.728+09:00",
        "lastSentTime": "2023-03-15T19:46:13.728+09:00",
        "lastUpdatedTime": "2023-03-15T19:46:13.728+09:00",
        "lastThroughputIn": 0,
        "lastThroughputOut": 0,
        "maxTotalConnectionTime": "2023-03-15T19:46:13.728+09:00",
        "maxTotalConnections": 0,
        "totalBytesIn": 0,
        "totalBytesOut": 0,
        "totalConnections": 0,
        "avgThroughputIn": 0,
        "avgThroughputOut": 0,        
        "maxThroughputIn": 0,
        "maxThroughputOut": 0       
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

The given vhost or application or stream name could not be found.

**Body**

```json
{
    "message": "[HTTP] Could not find the stream: [default/#default#app/stream] (404)",
    "statusCode": 404
}
```

</details>
