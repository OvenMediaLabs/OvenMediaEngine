---
title: Output Profile
description: "Manage OvenMediaEngine output profiles (transcoding renditions) for an application through the v1 REST API."
sidebar_position: 43
---

## Get Output Profile List

> ### Request

<details>

<summary><span class="http-method http-method-get">GET</span> /v1/vhosts/&#x7B;vhost&#x7D;/apps/&#x7B;app&#x7D;/outputProfiles</summary>

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
		"audio_only"
	]
}

# statusCode
	Same as HTTP Status Code
# message
	A human-readable description of the response code
# response
	Json array containing a list of output profile names
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

The given vhost or application name could not be found.

#### **Body**

```json
{
    "message": "[HTTP] Could not find the application: [vhost/app1] (404)",
    "statusCode": 404
}
```

</details>

## Create Output Profile

Add an Output Profile to the Application. If this request succeeds, the application will be restarted.

> ### Request

<details>

<summary><span class="http-method http-method-post">POST</span> /v1/vhosts/&#x7B;vhost&#x7D;/apps/&#x7B;app&#x7D;/outputProfiles</summary>

#### Header

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
```

#### Body

Configure output profiles to be created in Json array format.&#x20;


```json
[
  {
    "name": "bypass_profile",
    "outputStreamName": "${OriginStreamName}_bypass",
    "encodes": {
      "videos": [
        {
          "bypass": true
        }
      ],
      "audios": [
        {
          "bypass": true
        }
      ]
    }
  }
]

# name (required)
  The name of the output profile. cannot be duplicated

# outputStreamName (required)
  The name of the output stream to be created through this profile.
  
# encodes (required)
  encode profile list. It must have videos or audios, and must have at least one item.
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
        "statusCode": 200,
        "message": "OK",
        "response": {
            "name": "bypass_profile",
            "outputStreamName": "${OriginStreamName}_bypass",
            "encodes": {
                "audios": [],
                "videos": [
                    {
                        "bypass": true
                    }
                ]
            }
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
    Created output profile information
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
    Output profile information created when statusCode is 200
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

<summary><span class="http-method http-method-404">404</span> Not Found</summary>

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

<summary><span class="http-method http-method-409">409</span> Conflict</summary>

A output profile name already exists

</details>

## Get Output Profile Information

> ### Request

<details>

<summary><span class="http-method http-method-get">GET</span> /v1/vhosts/&#x7B;vhost&#x7D;/apps/&#x7B;app&#x7D;/outputProfiles/&#x7B;profile&#x7D;</summary>

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
    "response": {
        "name": "bypass_profile2",
        "outputStreamName": "${OriginStreamName}_bypass"
        "encodes": {
            "audios": [],
            "videos": [
                {
                    "bypass": true
                }
            ]
        }
    }
}

# statusCode
	Same as HTTP Status Code
# message
	A human-readable description of the response code
# response
	Output Profile information
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

The given vhost or application name could not be found.

#### **Body**

```json
{
    "message": "[HTTP] Could not find the application: [vhost/app1] (404)",
    "statusCode": 404
}
```

</details>

## Delete Output Profile&#x20;

Delete output profile settings. If this request succeeds, the Application will be restarted.

> ### Request

<details>

<summary><span class="http-method http-method-delete">DELETE</span> /v1/vhosts/&#x7B;vhost&#x7D;/apps/&#x7B;app&#x7D;/outputProfiles/&#x7B;profile&#x7D;</summary>

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

The given vhost or application name could not be found.

#### **Body**

```json
{
    "message": "[HTTP] Could not find the application: [vhost/app1] (404)",
    "statusCode": 404
}
```

</details>
