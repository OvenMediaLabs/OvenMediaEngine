---
title: VirtualHost
sidebar_position: 40
---

## Get Virtual Host List

> ### Request

<details>

<summary><span class="http-method http-method-get">GET</span> /v1/vhosts</summary>

#### **Header**

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
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
	"statusCode": 200,
	"message": "OK",
	"response": [
		"default",
		"service",
		"poc"
	]
}

# statusCode
	Same as HTTP Status Code
# message
	A human-readable description of the response code
# response
	Json array containing a list of virtual host names
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

## Create Virtual Host

> ### Request

<details>

<summary><span class="http-method http-method-post">POST</span> /v1/vhosts</summary>

#### **Header**

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
```

#### Body

Configure virtual hosts to be created in Json array format.&#x20;

```json
[
    {
        "name": "vhost",
        "host": {
            "names": [
                "ome-dev.ovenmedialabs.com",
                "prod.ovenmedialabs.com"
            ],
            "tls": {
                "certPath": "/etc/pki/ovenmedialabs.com/_ovenmedialabs_com.crt",
                "chainCertPath": "/etc/pki/ovenmedialabs.com/_ovenmedialabs_com.ca-bundle",
                "keyPath": "/etc/pki/ovenmedialabs.com/_ovenmedialabs_com.key"
            }
        },

        "signedPolicy": {
            "enables": {
                "providers": "rtmp,webrtc,srt",
                "publishers": "webrtc,llhls"
            },
            "policyQueryKeyName": "policy",
            "secretKey": "aKq#1kj",
            "signatureQueryKeyName": "signature"
        },

        "admissionWebhooks": {
            "controlServerUrl": "https://control.server/admission",
            "enables": {
                "providers": "rtmp,webrtc,srt",
                "publishers": "webrtc,llhls"
            },
            "secretKey": "",
            "timeout": 3000
        },
        
        "origins": {
            "origin": [
                {
                    "location": "/app/rtsp",
                    "pass": {
                        "scheme": "rtsp",
                        "urls": {
                            "url": [
                                "rtsp.server:8554/ca-01"
                            ]
                        }
                    }
                }
            ]
        },

        "originMapStore": {
            "originHostName": "ome-dev.ovenmedialabs.com",
            "redisServer": {
                "auth": "!@#ovenmediaengine",
                "host": "redis.server:6379"
            }
        }
    },
    {
        "name": "vhost2",
        "host": {
            "names": [
                "ovenmediaengine.com"
            ],
            "tls": {
                "certPath": "/etc/pki/ovenmediaengine.com/_ovenmediaengine_com.crt",
                "chainCertPath": "/etc/pki/ovenmediaengine.com/_ovenmediaengine_com.ca-bundle",
                "keyPath": "/etc/pki/ovenmediaengine.com/_ovenmediaengine_com.key"
            }
        }
    }
]

# name (required)
    The virtual host name. Cannot be duplicated.

# host (required)
    ## names (required)
        The addresses(IP or Domain)of the host. 
    ## tls (optional)
        The certificate file path. Required if using TLS. 
        
# signedPolicy (optional)
    The SignedPolicy setting. Please refer to the manual for details.
    
# admissionWebhooks (optional)
    The AdmissionWebhooks setting. Please refer to the manual for details.
    
# origins (optional)
    The Origins setting. Please refer to the manual for details.

# originMapStore (optional)
    The OriginMapStore setting. Please refer to the manual for details.
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

It responds with **Json array** for each request.

```json
[
    {
        "message": "OK",
        "statusCode": 200,
        "response": {
            "name": "enterprise",

            "host": {
                "names": [
                    "ome-dev.ovenmedialabs.com",
                    "prod.ovenmedialabs.com"
                ],
                "tls": {
                    "certPath": "/etc/pki/ovenmedialabs.com/_ovenmedialabs_com.crt",
                    "chainCertPath": "/etc/pki/ovenmedialabs.com/_ovenmedialabs_com.ca-bundle",
                    "keyPath": "/etc/pki/ovenmedialabs.com/_ovenmedialabs_com.key"
                }
            },
            "signedPolicy": {
                "enables": {
                    "providers": "rtmp,webrtc,srt",
                    "publishers": "webrtc,llhls"
                },
                "policyQueryKeyName": "policy",
                "secretKey": "aKq#1kj",
                "signatureQueryKeyName": "signature"
            },
            "admissionWebhooks": {
                "controlServerUrl": "https://control.server/admission",
                "enables": {
                    "providers": "rtmp,webrtc,srt",
                    "publishers": "webrtc,llhls"
                },
                "secretKey": "",
                "timeout": 3000
            },
            "origins": {
                "origin": [
                    {
                        "location": "/app/rtsp",
                        "pass": {
                            "scheme": "rtsp",
                            "urls": {
                                "url": [
                                    "rtsp.server:8554/ca-01"
                                ]
                            }
                        }
                    }
                ]
            },
            "originMapStore": {
                "originHostName": "ome-dev.ovenmedialabs.com",
                "redisServer": {
                    "auth": "!@#ovenmediaengine",
                    "host": "redis.server:6379"
                }
            }
        }
    },
    {
        "message": "OK",
        "statusCode": 200,
        "response": {
            "name": "free",
            "host": {
                "names": [
                    "ovenmediaengine.com"
                ],
                "tls": {
                    "certPath": "/etc/pki/ovenmediaengine.com/_ovenmediaengine_com.crt",
                    "chainCertPath": "/etc/pki/ovenmediaengine.com/_ovenmediaengine_com.ca-bundle",
                    "keyPath": "/etc/pki/ovenmediaengine.com/_ovenmediaengine_com.key"
                }
            }
        }
    }
]

# statusCode
	Same as HTTP Status Code
# message
	A human-readable description of the response code
# response
	Created virtual host information
```

</details>

<details>

<summary><span class="http-method http-method-207">207</span> Multi-Status</summary>

There might be a mixture of responses.

#### **Header**

```
Content-Type: application/json
```

#### **Body**

It responds with **Json array** for each request.

```json
[
    {
        "statusCode": 200,
        "message": "OK",
        "response": {
            "name": "enterprise",
            "host": {
                "names": [
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
    Virtual host information created when statusCode is 200
```

</details>

<details>

<summary><span class="http-method http-method-400">400</span> Bad Request</summary>

Invalid request. Body is not a Json array or does not have a required value

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

<summary><span class="http-method http-method-409">409</span> Conflict</summary>

A virtual host with that name already exists

</details>

## Get Virtual Host Information

> ### Request

<details>

<summary><span class="http-method http-method-get">GET</span> /v1/vhosts/&#x7B;vhost&#x7D;</summary>

#### Header

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
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

    "message": "OK",
    "statusCode": 200
    "response": {
        "name": "default",
        "distribution": "ovenServer",

    "host": {
        "name": "default",
        "distribution": "ovenServer",

        "host": {
            "names": [
                "ome-dev.ovenmedialabs.com",
                "*"
            ],
            "tls": {
                "certPath": "/etc/pki/ovenmedialabs.com/_ovenmedialabs_com.crt",
                "chainCertPath": "/etc/pki/ovenmedialabs.com/_ovenmedialabs_com.ca-bundle",
                "keyPath": "/etc/pki/ovenmedialabs.com/_ovenmedialabs_com.key"
            }
        },
        
        "signedPolicy": {
            "enables": {
                "providers": "rtmp,webrtc,srt",
                "publishers": "webrtc,llhls"
            },
            "policyQueryKeyName": "policy",
            "secretKey": "aKq#1kj",
            "signatureQueryKeyName": "signature"
        },
        
        "admissionWebhooks": {
            "controlServerUrl": "https://control.server/admission",
            "enables": {
                "providers": "rtmp,webrtc,srt",
                "publishers": "webrtc,llhls"
            },
            "secretKey": "",
            "timeout": 3000
        },
        
        "origins": {
            "origin": [
                {
                    "location": "/app/rtsp",
                    "pass": {
                        "scheme": "rtsp",
                        "urls": {
                            "url": [
                                "rtsp.server:8554/ca-01"
                            ]
                        }
                    }
                }
            ]
        },

        "originMapStore": {
            "originHostName": "ome-dev.ovenmedialabs.com",
            "redisServer": {
                "auth": "!@#ovenmediaengine",
                "host": "redis.server:6379"
            }
        }
    }

# statusCode
	Same as HTTP Status Code
# message
	A human-readable description of the response code
# response
	Virtual host information
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

The given vhost name could not be found.

#### **Body**

```json
{
    "message": "[HTTP] Could not find the virtual host: [default1] (404)",
    "statusCode": 404
}
```

</details>

## Delete Virtual Host

> ### Request

<details>

<summary><span class="http-method http-method-delete">DELETE</span> /v1/vhosts/&#x7B;vhost&#x7D;</summary>

#### Header

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
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

<summary><span class="http-method http-method-500">500</span> Internal Server Error</summary>

The request failed due to an error on the server. Check the server log for the reason of the error.

#### **Body**

```json
{
    "message": "[HTTP] Internal Server Error (500)",
    "statusCode": 500
}
```

</details>

