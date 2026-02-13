# Application

## Get Application List

List all application names in the virtual host.&#x20;

> ### Request

<details>

<summary><mark style="color:blue;">GET</mark> /v1/vhosts/{vhost}/apps</summary>

#### **Header**

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
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
	"response": [
		"app",
		"app2",
		"app3"
	]
}

# statusCode
	Same as HTTP Status Code
# message
	A human-readable description of the response code
# response
	Json array containing a list of application names
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

The given vhost name could not be found.

#### **Body**

```json
{
    "message": "[HTTP] Could not find the virtual host: [default1] (404)",
    "statusCode": 404
}
```

</details>

## Create Application

Create application in the virtual host

> ### Request

<details>

<summary><mark style="color:blue;">POST</mark> /v1/vhosts/{vhost}/apps</summary>

#### Header

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
```

#### Body

Configure applications to be created in <mark style="color:green;">Json array</mark> format.&#x20;

{% code overflow="wrap" %}
```json
[
    {
        "name": "app",
        "type": "live",
        "outputProfiles": {
            "outputProfile": [
                {
                    "name": "default",
                    "outputStreamName": "${OriginStreamName}",
                    "encodes": {
                        "audios": [
                            {
                                "name": "opus",
                                "codec": "opus",
                                "samplerate": 48000,
                                "bitrate": 128000,
                                "channel": 2,
                                "bypassIfMatch": {
                                    "codec": "eq"
                                }
                            },
                            {
                                "name": "aac",
                                "codec": "aac",
                                "samplerate": 48000,
                                "bitrate": 128000,
                                "channel": 2,
                                "bypassIfMatch": {
                                    "codec": "eq"
                                }
                            }
                        ],
                        "videos": [
                            {
                                "name": "bypass_video",
                                "bypass": true
                            }
                        ]
                    }
                }
            ]
        },
        "providers": {
            "ovt": {},
            "rtmp": {},
            "rtspPull": {},
            "srt": {},
            "webrtc": {}
        },
        "publishers": {
            "llhls": {},
            "ovt": {},
            "webrtc": {}
        }
    }
]
    
# name (required)
    Application name to create
    
# type (required)
    live - currently only support live
    
# outputProfiles (optional)
   Set OutputProfile for Transcoding. See the ABR and Transcoding chapter for             more details. If no outputProfiles are present in the request, a default outputProfile as above is configured.
   
# providers (optional)
    Configure providers. See the Live Source chapter for details. If providers are not present in the request, they are configured with default providers as above.

# publishers (optional)
    Configure publishers. See the Streaming chapter for details. If publishers are not present in the request, they are configured with default publishers as above.
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

It responds with <mark style="color:green;">**Json array**</mark> for each request.

```json
[
    {
        "statusCode": 200,
        "message": "OK",
        "response": {
            "name": "app",
            "outputProfiles": {
            ...

            "providers": {
                "ovt": {},
                "rtmp": {},
            ...
    },
    {
        "statusCode": 200,
        "message": "OK",
        "response": {
            ...
        }
    }
}

# statusCode
    Same as HTTP Status Code
# message
    A human-readable description of the response code
# response
    Created application information
```

</details>

<details>

<summary><mark style="color:blue;">207</mark> Multi-Status</summary>

There might be a mixture of responses.

#### **Header**

```
Content-Type: application/json
```

#### **Body**

It responds with <mark style="color:green;">**Json array**</mark> for each request.

```json
[
    {
        "statusCode": 200,
        "message": "OK",
        "response": {
            "name": "app",
            "outputProfiles": {
            ...

            "providers": {
                "ovt": {},
                "rtmp": {},
            ...
    },
    {
        "statusCode": 409,
        "message": "Conflict",
        "response": {
            ...
        }
    }
}

# statusCode
    Same as HTTP Status Code
# message
    A human-readable description of the response code
# response
    Application information created when statusCode is 200
```

</details>

<details>

<summary><mark style="color:red;">400</mark> Bad Request</summary>

Invalid request. Body is not a Json array or does not have a required value

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

The given vhost name could not be found.

#### **Body**

```json
{
    "message": "[HTTP] Could not find the virtual host: [default1] (404)",
    "statusCode": 404
}
```

</details>

<details>

<summary><mark style="color:red;">409</mark> Conflict</summary>

An application name already exists

</details>

## Get Application Information

> ### Request

<details>

<summary><mark style="color:blue;">GET</mark> /v1/vhosts/{vhost}/apps/{app}</summary>

#### **Header**

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
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
        "dynamic": false,
        "name": "app",
        "type": "live",
        "outputProfiles": {
            "outputProfile": [
                {
                    "encodes": {
                        "audios": [
                            {
                                "bitrate": 128000,
                                "bypassIfMatch": {
                                    "codec": "eq"
                                },
                                "channel": 2,
                                "codec": "opus",
                                "name": "opus",
                                "samplerate": 48000
                            },
                            {
                                "bitrate": 128000,
                                "bypassIfMatch": {
                                    "codec": "eq"
                                },
                                "channel": 2,
                                "codec": "aac",
                                "name": "aac",
                                "samplerate": 48000
                            }
                        ],
                        "videos": [
                            {
                                "bypass": true,
                                "name": "bypass_video"
                            }
                        ]
                    },
                    "name": "bypass",
                    "outputStreamName": "${OriginStreamName}"
                }
            ]
        },
        "providers": {
            "ovt": {},
            "rtmp": {},
            "rtspPull": {},
            "srt": {},
            "webrtc": {}
        },
        "publishers": {
            "llhls": {},
            "ovt": {},
            "webrtc": {}
        }
    }
}

# statusCode
    Same as HTTP Status Code
# message
    A human-readable description of the response code
# response
    Application information
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

The given vhost name or application name could not be found.

#### **Body**

```json
{
    "message": "[HTTP] Could not find the application: [default/app2] (404)",
    "statusCode": 404
}
```

</details>

## Patch Application Information

Modify application settings. If this request succeeds, the Application will be restarted.

> ### Request

<details>

<summary><mark style="color:blue;">PATCH</mark> /v1/vhosts/{vhost}/apps/{app}</summary>

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
    "providers": {
        "webrtc": {
            "timeout": 60000
        }
    }
}
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
        "dynamic": false,
        "name": "app",
        "type": "live",
        "outputProfiles": {
            "outputProfile": [
                {
                    "encodes": {
                        "audios": [
                            {
                                "bitrate": 128000,
                                "bypassIfMatch": {
                                    "codec": "eq"
                                },
                                "channel": 2,
                                "codec": "opus",
                                "name": "opus",
                                "samplerate": 48000
                            },
                            {
                                "bitrate": 128000,
                                "bypassIfMatch": {
                                    "codec": "eq"
                                },
                                "channel": 2,
                                "codec": "aac",
                                "name": "aac",
                                "samplerate": 48000
                            }
                        ],
                        "videos": [
                            {
                                "bypass": true,
                                "name": "bypass_video"
                            }
                        ]
                    },
                    "name": "bypass",
                    "outputStreamName": "${OriginStreamName}"
                }
            ]
        },
        "providers": {
            "ovt": {},
            "rtmp": {},
            "rtspPull": {},
            "srt": {},
            "webrtc": {
                "timeout": 60000
            }
        },
        "publishers": {
            "llhls": {},
            "ovt": {},
            "webrtc": {}
        }
    }
}

# statusCode
    Same as HTTP Status Code
# message
    A human-readable description of the response code
# response
    Mofified application information
```

</details>

<details>

<summary><mark style="color:red;">400</mark> Bad Request</summary>

Invalid request.&#x20;

```json
{
    "message": "[HTTP] Cannot change [name] using this API (400)",
    "statusCode": 400
}
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

The given vhost name or application name could not be found.

#### **Body**

```json
{
    "message": "[HTTP] Could not find the application: [default/app2] (404)",
    "statusCode": 404
}
```

</details>

## Delete Application

> ### Request

<details>

<summary><mark style="color:blue;">DELETE</mark> /v1/vhosts/{vhost}/apps/{app}</summary>

#### **Header**

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
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
    "message": "OK",
    "statusCode": 200
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

The given vhost name or application name could not be found.

#### **Body**

```json
{
    "message": "[HTTP] Could not find the application: [default/app2] (404)",
    "statusCode": 404
}
```

</details>

<details>

<summary><mark style="color:red;">500</mark> Internal Server Error</summary>

The request failed due to an error on the server. Check the server log for the reason of the error.

#### **Body**

```json
{
    "message": "[HTTP] Internal Server Error (500)",
    "statusCode": 500
}
```

</details>
