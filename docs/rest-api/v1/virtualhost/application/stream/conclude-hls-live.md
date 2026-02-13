# Conclude HLS Live

For live streaming of certain events, it may be necessary to immediately stop the HLS live stream and switch to VoD after the HLS live broadcast ends. This API transitions to VoD by stopping segment updates for LL-HLS and HLS streams and inserting #EXT-X-ENDLIST. By using this API with a [Scheduled Channel](../../../../../live-source/scheduled-channel.md), you can implement additional application services.

> ### Request

<details>

<summary><mark style="color:blue;">POST</mark> v1/vhosts/{vhost}/apps/{app}/streams/{stream}:concludeHlsLive</summary>

#### Header

```http
Authorization: Basic {credentials}

# Authorization
    Credentials for HTTP Basic Authentication created with <AccessToken>
```

#### Body

```json
{}
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
