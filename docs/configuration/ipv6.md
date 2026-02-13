# IPv6



Starting from version OME v0.15.1, IPv6 is supported.

To use IPv6, you need to change the settings of the `Server.xml` file as follows:

## 1. Configuration for listening

You can use `/<Server>/<IP>` to support IPv6. In versions prior to v0.15.0, only one `/<Server>/<IP>` setting could be specified, but in versions after v0.15.1, multiple settings can be specified. That is, if you add an `/<Server>/<IP>` element for IPv6 to the existing configuration as follows, you can accept IPv6 requests from clients:

```xml
<Server>
    ...
    <IP>*</IP>
    <!-- Listening the bind ports on IPv6 interfaces -->
    <IP>::</IP>
    ...
</Server>
```

{% hint style="info" %}
`*` means `0.0.0.0`(`INADDR_ANY`) in IPv4, and `::` means `::0`(`in6addr_any`) in IPv6.

Of course, you can also specify a specific IP address of an interface instead of `::`.
{% endhint %}

### Example 1) IPv4 Only

```xml
<Server>
    <Name>OvenMediaEngine</Name>
    <Type>origin</Type>
    <IP>*</IP>

    <Bind>
        <Providers>
            <RTMP>
                <Port>1935</Port>
            </RTMP>
        </Providers>
    </Bind>
</Server>
```

OME listens to the `1935` port for RTMP as follows:

```bash
$ sudo netstat -tulnp | grep "$(pgrep OvenMediaEngine)"
tcp        0      0 0.0.0.0:1935            0.0.0.0:*               LISTEN      xxx/OvenMediaEn
```

### Example 2) IPv6 Only

```xml
<Server version="8">
    <Name>OvenMediaEngine</Name>
    <Type>origin</Type>
    <IP>::</IP>

    <Bind>
        <Providers>
            <RTMP>
                <Port>1935</Port>
            </RTMP>
        </Providers>
    </Bind>
    ...
</Server>
```

OME listens to the `1935` port for RTMP as follows:

```bash
$ sudo netstat -tulnp | grep "$(pgrep OvenMediaEngine)"
tcp6       0      0 :::1935                 :::*                    LISTEN      xxx/OvenMediaEn
```

### Example 3) IPv4 + IPv6

```xml
<?xml version="1.0" encoding="UTF-8"?>
<Server version="8">
    <Name>OvenMediaEngine</Name>
    <Type>origin</Type>
    <IP>*</IP>
    <IP>::</IP>

    <Bind>
        <Providers>
            <RTMP>
                <Port>1935</Port>
            </RTMP>
        </Providers>
    </Bind>
    ...
</Server>

```

OME listens to the `1935` port for RTMP as follows:

```bash
$ sudo netstat -tulnp | grep "$(pgrep OvenMediaEngine)"
tcp        0      0 0.0.0.0:1935            0.0.0.0:*               LISTEN      xxx/OvenMediaEn
tcp6       0      0 :::1935                 :::*                    LISTEN      xxx/OvenMediaEn
```



## 2. Configuration for IceCandidates (for WebRTC)

When you specify IPv6 interface `/<Server>/<IP>`, most Providers/Publishers will work with IPv6, but WebRTC will not. While the WebSocket server used as the WebRTC Signalling server works well with the above setting, but more setting is required for ICE Candidates that actually transmit/receive data.

To use IPv6 ICE Candidate, you need to add an IPv6 `<IceCandidate>` to `/<Server>/<Bind>/(<Providers>|<Publishers>)/<WebRTC>/<IceCandidates>`.

```xml
<Server version="8">
    ...
    <Bind>
        <Providers>
            <WebRTC>
                ...
                <IceCandidates>
                    <IceCandidate>*:10000/udp</IceCandidate>
                    <IceCandidate>[::]:10000/udp</IceCandidate>
                </IceCandidates>
                ...
            </WebRTC>
        </Providers>
    </Bind>
    ...
</Server>
```

{% hint style="info" %}
To support IPv6 in URL format settings, use `[::]` instead of `::`
{% endhint %}

{% hint style="info" %}
The `<IceCandidate>` settings for Providers and Publishers are the same.
{% endhint %}

By setting up as above, OME is ready to use ICE Candidates for IPv6 as well as IPv4. The ICE Candidate generated here can be viewed in the signaling step of the web browser.

<figure><img src="../.gitbook/assets/image (3) (1) (1) (1).png" alt=""><figcaption></figcaption></figure>

## 3. Configuration for \<Origin>

Now you can set up the OME edge to look at an origin with an IPv6 IP address. To do this, you can set `/<Server>/<VirtualHosts>/<VirtualHost>/<Origins>/<Origin>/<Pass>/<Urls>/<Url>` as follows:

```xml
<Server version="8">
    ...
    <VirtualHosts>
        <VirtualHost>
            <Origins>
                <Origin>
                    <Location>/rtsp/stream</Location>
                    <Pass>
                        <Scheme>rtsp</Scheme>
                        <Urls>
                            <Url>airen:airen@[1:2:3:4:5:6:7:8]:1234/app/stream</Url>
                        </Urls>
                    </Pass>
                </Origin>
            </Origins>
        </VirtualHost>
    </VirtualHosts>
    ...
</Server>
```

This configuration creates a stream that refers an RTSP source provided on port 1234 of an origin which has an IPv6 address of `1:2:3:4:5:6:7:8`.

## 4. Configuration for \<AdmissionWebhooks>

You can also specify an IPv6 address for the server that `AdmissionWebhooks` is using. To do this, set the value of `/<Server>/<VirtualHosts>/<VirtualHost>/<AdmissionWebhooks>/<ControlServerUrl>` as follows:

```xml
<Server>
    ...
    <VirtualHosts>
        <VirtualHost>
            <AdmissionWebhooks>
                <ControlServerUrl>http://[1:2:3:4:5:6:7:8]:7000/a/b/c</ControlServerUrl>
                ...
            </AdmissionWebhooks>
        </VirtualHost>
    </VirtualHosts>
    ...
</Server>
```

The above configuration asks whether the client has the permission to publish or playback using `http://[1:2:3:4:5:6:7:8]:7000/a/b/c`.

\
