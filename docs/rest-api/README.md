# REST API

## Overview

The REST APIs provided by OME allow you to query or change settings such as `VirtualHost` and `Application/Stream`.

{% hint style="warning" %}
There are some limitations/considerations.

* If you add/change/delete the settings of the App/Output Profile by invoking the API, the app will be restarted. This means that all sessions connected to the app will be disconnected.
* VirtualHost settings in Server.xml cannot be modified through API. This rule also applies to Application/OutputStream, etc. within that VirtualHost. So, if you call a POST/PUT/DELETE API for VirtualHost/Application/OutputProfile declared in Server.xml, it will not work with a 403 Forbidden error.
{% endhint %}

By default, OvenMediaEngine's API Server is disabled, so the following settings are required to use the API.

## Setup API Server

### Port Binding

The API server's port can be set in `<Bind><Managers><API>`. `<Port>` is an unsecured port and `<TLSPort>` is a secured port. To use TLSPort, TLS certificate must be set in the [Managers](./#managers).

```markup
<Server version="8">
	...
	<Bind>
		<Managers>
			<API>
				<Port>8081</Port>
				<TLSPort>8082</TLSPort>
			</API>
		</Managers>
		...
	</Bind>
	...
</Server>
```

### Managers

In order to use the API server, you must configure `<Managers>` as well as port binding.

```xml
<Server version="8">
	<Bind>
		...
	</Bind>

	<Managers>
		<Host>
			<Names>
				<Name>*</Name>
			</Names>
			<TLS>
				<CertPath>airensoft_com.crt</CertPath>
				<KeyPath>airensoft_com.key</KeyPath>
				<ChainCertPath>airensoft_com_chain.crt</ChainCertPath>
			</TLS>
		</Host>
		<API>
			<AccessToken>your_access_token</AccessToken>
			<CrossDomains>
				<Url>*.airensoft.com</Url>
				<Url>http://*.sub-domain.airensoft.com</Url>
				<Url>http?://airensoft.*</Url>
			</CrossDomains>
		</API>
	</Managers>

	<VirtualHosts>
		...
	</VirtualHosts>
</Server>
```

#### Host

In `<Names>`, set the domain or IP that can access the API server. If `*` is set, any address is used. In order to access using the TLS Port, a certificate must be set in `<TLS>`.

#### AccessToken

API Server uses Basic HTTP Authentication Scheme to authenticate clients. An `AccessToken` is a plaintext credential string before base64 encoding. Setting the `AccessToken` to the form `user-id:password` per RFC7617 allows standard browsers to pass authentication, but it is not required.

For more information about HTTP Basic authentication, refer to the URL below.&#x20;

[https://developer.mozilla.org/en-US/docs/Web/HTTP/Authentication](https://developer.mozilla.org/en-US/docs/Web/HTTP/Authentication)

#### CrossDomains

To enable CORS on your API Server, you can add a setting. You can add `*` to allow all domains. If contains a scheme, such as https://, only that scheme can be allowed, or if the scheme is omitted, such as \*.airensoft.com, all schemes can be accepted.



## API Request

API endpoints are provided in the following format.

> <mark style="color:blue;">Method</mark> http://API.Server.Address\[:Port]/<mark style="color:orange;">v1</mark>/<mark style="color:purple;">Resource</mark>&#x20;
>
> <mark style="color:blue;">Method</mark> https://API.Server.Address\[:TLSPort]/<mark style="color:orange;">v1</mark>/<mark style="color:purple;">Resource</mark>

OvenMediaEngine supports <mark style="color:blue;">GET</mark>, <mark style="color:blue;">POST</mark>, and <mark style="color:blue;">DELETE</mark> methods, and sometimes supports <mark style="color:blue;">PATCH</mark> depending on the type of resource. For detailed API specifications, please check the subdirectory of this chapter.

### Action

In OvenMediaEngine's REST API, action is provided in the following format.

> <mark style="color:blue;">POST</mark> http://host/v1/resource<mark style="color:green;">:{action name}</mark>

For example, an action to send an ID3 Timedmeta event to an LLHLS stream is provided by the endpoint below.

> <mark style="color:blue;">POST</mark> http://-/v1/vhosts/{vhost}/apps/{app}/streams/{stream}:<mark style="color:green;">sendEvent</mark>

### Document format

In this API reference document, the API endpoint is described as follows. Note that scheme://Host\[:Port] is omitted for all endpoints.

<details>

<summary><mark style="color:blue;">METHOD</mark> /v1/&#x3C;API_PATH></summary>

#### Header

Describe the required header values.

```http
Header-Key: Value

# Header-Key
    Description
```

#### Body

Describe the request body content. The body of all APIs consists of Json content. Therefore, the `Content-Type` header value is always `application/json`, which can be omitted in the document.

{% code overflow="wrap" %}
```json
{
    "requestId": "value"
}
    
# key (required)
    The description of the key/value of the body content is provided like this.
```
{% endcode %}

</details>

Responses from API endpoints are provided in the following format.

<details>

<summary><mark style="color:blue;">HTTP_Status_Code</mark> Code_Description</summary>

#### **Header**

Description of response headers

```http
Header-Key: Value
```

#### **Body**

Description the response body content. The body of all response consists of Json content. Therefore, the `Content-Type` header value is always `application/json`, which can be omitted in the document.

```json
{
	"statusCode": 200,
	"message": "OK",
	"response": {
	}
}

# statusCode
	Same as HTTP Status Code
# message
	A human-readable description of the response code
# response
	Response Contents
```

</details>

