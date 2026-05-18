---
title: Reload Certificate
description: "Reload the OvenMediaEngine API server TLS certificate via REST without a restart; the old cert stays on failure."
sidebar_position: 58
---

## Reload Certificate

Reload the certificate of the API Server. In case of failure, the existing certificate will continue to be used.

> ### Request

<details>

<summary><span class="http-method http-method-post">POST</span> /v1/managers/api:reloadCertificate</summary>

#### Header

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
```

</details>

> ### Responses

<details>

<summary><span class="http-method http-method-200">200</span> OK</summary>

The request has succeeded

**Header**

```http
Content-Type: application/json
```

**Body**


```json
{
<strong>    "message": "OK",
</strong>    "statusCode": 200
}

# statusCode
    Same as HTTP Status Code
# message
    A human-readable description of the response code
```


</details>

<details>

<summary><span class="http-method http-method-400">400</span> Bad Request</summary>

TLS is not enabled for the API Server

**Header**

```http
Content-Type: application/json
```

**Body**

```json
{
    "message": "[HTTP] TLS is not enabled for the API Server (400)",
    "statusCode": 400
}
```

</details>

<details>

<summary><span class="http-method http-method-500">500</span> Internal Server Error</summary>

Failed to reload the certificate, so the existing one will be kept. The reason for the failure can be found in the server logs.

**Header**

```http
Content-Type: application/json
```

**Body**

```json
{
    "message": "[HTTP] Failed to create a certificate for API Server (500)",
    "statusCode": 500
}
```

</details>
