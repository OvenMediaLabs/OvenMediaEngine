---
title: Reload Certificate
sidebar_position: 41
---

## Reload All Certificates

Batch reload certificates of all Virtual Hosts. In case of failure, the existing certificate will continue to be used.

> ### Request

<details>

<summary><span class="http-method http-method-post">POST</span> /v1/vhosts:reloadAllCertificates</summary>

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

**Header**

```
Content-Type: application/json
```

**Body**

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

<summary><span class="http-method http-method-500">500</span> Internal Server Error</summary>

Failed to reload certificate. Keep the existing certificate. The reason for the failure can be found in the server logs.

</details>

## Reload Certificate

Reload the certificate of the specified Virtual Hosts. In case of failure, the existing certificate will continue to be used.

> ### Request

<details>

<summary><span class="http-method http-method-post">POST</span> /v1/vhosts/&#x7B;vhost&#x7D;:reloadCertificate</summary>

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

<summary><span class="http-method http-method-500">500</span> Internal Server Error</summary>

Failed to reload certificate. Keep the existing certificate. The reason for the failure can be found in the server logs.

</details>
