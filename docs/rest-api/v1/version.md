---
title: Version
description: "Get the current OvenMediaEngine version and git version via the v1 REST API."
sidebar_position: 40
---

Returns OvenMediaEngine version metadata.

> **Request**

<details>

<summary><span class="http-method http-method-get">GET</span> /v1/version</summary>

**Header**

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
```

</details>

> **Responses**

<details>

<summary><span class="http-method http-method-200">200</span> Ok</summary>

The request has succeeded.

**Header**

```http
Content-Type: application/json
```

**Body**

```json
{
    "statusCode": 200,
    "message": "OK",
    "response": {
        "version": "0.20.5",
        "gitVersion": "v0.20.5-0-g1234567"
    }
}
```

</details>

<details>

<summary><span class="http-method http-method-401">401</span> Unauthorized</summary>

Authentication required.

**Header**

```http
WWW-Authenticate: Basic realm="OvenMediaEngine"
```

**Body**

```json
{
    "message": "[HTTP] Authorization header is required to call API (401)",
    "statusCode": 401
}
```

</details>
