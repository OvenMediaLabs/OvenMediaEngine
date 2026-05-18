---
title: REST API
description: "Query and change OvenMediaEngine settings — virtual hosts, applications, and streams — through the REST API."
sidebar_position: 38
---

## Overview

The REST APIs provided by OME allow you to query or change settings such as `VirtualHost` and `Application/Stream`.


:::warning

There are some limitations/considerations.

* If you add/change/delete the settings of the App/Output Profile by invoking the API, the app will be restarted. This means that all sessions connected to the app will be disconnected.
* VirtualHost settings in Server.xml cannot be modified through API. This rule also applies to Application/OutputStream, etc. within that VirtualHost. So, if you call a POST/PUT/DELETE API for VirtualHost/Application/OutputProfile declared in Server.xml, it will not work with a 403 Forbidden error.

:::


By default, OvenMediaEngine's API Server is disabled, so the following settings are required to use the API.

## Setup API Server

### Port Binding

The API server's port can be set in `<Bind><Managers><API>`. `<Port>` is an unsecured port and `<TLSPort>` is a secured port. To use TLSPort, TLS certificate must be set in the [Managers](./README.md#managers).

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
				<CertPath>ovenmedialabs_com.crt</CertPath>
				<KeyPath>ovenmedialabs_com.key</KeyPath>
				<ChainCertPath>ovenmedialabs_com_chain.crt</ChainCertPath>
			</TLS>
		</Host>
		<API>
			<AccessToken>your_access_token</AccessToken>
			<CrossDomains>
				<Url>*.ovenmedialabs.com</Url>
				<Url>http://*.sub-domain.ovenmedialabs.com</Url>
				<Url>http?://ovenmedialabs.*</Url>
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

To enable CORS on your API Server, you can add a setting. You can add `*` to allow all domains. If contains a scheme, such as https://, only that scheme can be allowed, or if the scheme is omitted, such as \*.ovenmedialabs.com, all schemes can be accepted.




## API Request

API endpoints are provided in the following format.

> Method http://API.Server.Address\[:Port]/v1/Resource&#x20;
>
> Method https://API.Server.Address\[:TLSPort]/v1/Resource

OvenMediaEngine supports GET, POST, and DELETE methods, and sometimes supports PATCH depending on the type of resource. For detailed API specifications, please check the subdirectory of this chapter.

### Action

In OvenMediaEngine's REST API, action is provided in the following format.

> POST http://host/v1/resource:&#x7B;action name&#x7D;

For example, an action to send an ID3 Timedmeta event to an LLHLS stream is provided by the endpoint below.

> POST http://-/v1/vhosts/&#x7B;vhost&#x7D;/apps/&#x7B;app&#x7D;/streams/&#x7B;stream&#x7D;:sendEvent

### Document format

In this API reference document, the API endpoint is described as follows. Note that scheme://Host\[:Port] is omitted for all endpoints.

<details>

<summary>METHOD /v1/&#x3C;API_PATH></summary>

#### Header

Describe the required header values.

```http
Header-Key: Value

# Header-Key
    Description
```

#### Body

Describe the request body content. The body of all APIs consists of Json content. Therefore, the `Content-Type` header value is always `application/json`, which can be omitted in the document.


```json
{
    "requestId": "value"
}
    
# key (required)
    The description of the key/value of the body content is provided like this.
```


</details>

Responses from API endpoints are provided in the following format.

<details>

<summary>HTTP_Status_Code Code_Description</summary>

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

