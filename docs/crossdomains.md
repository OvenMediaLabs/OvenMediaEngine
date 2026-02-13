# CrossDomains

Most browsers and players prohibit accessing other domain resources in the currently running domain. You can control this situation through Cross-Origin Resource Sharing (CORS) or Cross-Domain (CrossDomain). You can set CORS and Cross-Domain as `<CrossDomains>` element.

CrossDomain settings are available for HTTP-based APIs, HLS, LLHLS and Thumbnail Publishers and WebRTC Provider(WHIP).

```xml
<CrossDomains>
    <Url>*</Url>
    <Url>*.airensoft.com</Url>
    <Url>http://*.ovenplayer.com</Url>
    <Url>https://demo.ovenplayer.com</Url>
    <Header>
        <Key>Access-Control-Expose-Headers</Key>
        <Value>Date, Server, Content-Type, Content-Length</Value>
    </Header>
    <Header>
        <Key>custom-header</Key>
        <Value>airensoft</Value>
    </Header>
</CrossDomains>
```

You can set it using the `<Url>` element as shown above, and you can use the following values:

<table><thead><tr><th width="290">Url Value</th><th>Description</th></tr></thead><tbody><tr><td><code>*</code></td><td>Allows requests from all Domains.</td></tr><tr><td>domain</td><td>Allows both HTTP and HTTPS requests from the specified Domain.</td></tr><tr><td>http://domain</td><td>Allows HTTP requests from the specified Domain.</td></tr><tr><td>https://domain</td><td>Allows HTTPS requests from the specified Domain.</td></tr></tbody></table>

You can respond with custom HTTP headers via `<CrossDomains>`. You can use them by adding multiple `<Header>/<Key>` and `<Header>/<Value>` as in the example above.

## Default(Global) CORS Settings

If all Applications share the same CORS policy, you can configure CORS under `<Server><Defaults><CrossDomains>` to apply it globally. This default CORS setting is also applied to invalid requests, such as `400 Bad Request` responses or requests to non-existent Applications.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<Server version="8">
    <Name>OvenMediaEngine</Name>
    ... omitted ...

    <!-- Default Settings for all Virtual Hosts -->
    <Defaults>
        <CrossDomains>
            <Url>*.ovenplayer.com</Url>
        </CrossDomains>
    </Defaults>

    <VirtualHosts>
        <VirtualHost>
            <Name>default</Name>
    ... omitted ...
```

{% hint style="warning" %}
Up to version 0.19.0, the default CORS setting was supported per VirtualHost and had to be configured at `<Server><VirtualHosts><VirtualHost><CrossDomain>`. However, this method is now deprecated and no longer applied. The configuration has been consolidated under `<Server><Defaults><CrossDomain>`.
{% endhint %}

## Application-Specific CORS Settings

If a particular Application requires a different CORS policy, you can add a CORS configuration to that Application. In this case, the Application-specific CORS setting takes precedence. For example, you can configure CORS for the current WebRTC Provider (WHIP), LLHLS, HLS, and Thumbnail Publisher as follows:

```xml
<VirtualHost>
    <Name>default</Name>
    ... omitted ...
    <Applications>
        <Application>
            <Name>app</Name>
            <Type>live</Type>
            ... omitted ...
            <Providers>
                <WebRTC>
                    <CrossDomains>
                        <Url>*</Url>
                    </CrossDomains>
                </WebRTC>
            </Providers>
            <Publishers>
                <LLHLS>
                    <ChunkDuration>0.5</ChunkDuration>
                    <PartHoldBack>1.5</PartHoldBack>
                    <SegmentDuration>6</SegmentDuration>
                    <SegmentCount>10</SegmentCount>
                    <CrossDomains>
                        <Url>*</Url>
                    </CrossDomains>
                </LLHLS>
                <HLS>
                    <SegmentCount>4</SegmentCount>
                    <SegmentDuration>4</SegmentDuration>
                    <CrossDomains>
                        <Url>*</Url>
                    </CrossDomains>
                </HLS>
                <Thumbnail>
                    <CrossDomains>
                        <Url>*</Url>
                    </CrossDomains>
                </Thumbnail>
            </Publishers>
        </Application>
    </Applications>
</VirtualHost>

```



